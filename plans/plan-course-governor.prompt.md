# Plan: Course-Aware Speed Governor

**TL;DR** — Slow the trolley automatically near course features that warrant
caution (greens, tees, water hazards, rough, bunkers) using their **polygon
geometry** from the course map, not just explicit speed-limit zones. This
extends the existing `speed_zone_node` pattern: instead of only applying limits
*inside* a `SPEED_ZONE` polygon, the governor applies a reduced limit when the
trolley is **near** (within a configurable distance of) a forbidden-zone
polygon.

**User decisions (confirmed)**
- Scope: Course-aware speed governor only.
- Greens and other areas are **polygons** — the governor uses their actual
  polygon geometry (point-in-polygon + distance-to-polygon), not a point+radius
  approximation.
- Reuses the existing `speed_zone_node` / `SpeedZoneStatus` / Safety Controller
  clamp path (the Safety Controller already clamps to `min(max, limit)`).

---

## Current state (verified)

- **`CourseMap.msg`** has `forbidden_zones` (`geometry_msgs/Polygon[]`) but the
  polygons carried **no type/label** — the type was lost when the parser pushed
  them. **Fixed:** added parallel `forbidden_zone_types` / `forbidden_zone_labels`
  arrays (mirroring `speed_zones` / `speed_zone_limits_mps` / `speed_zone_labels`).
- **`course_zip.hpp`** parser now populates the new parallel arrays when it
  pushes a non-`SPEED_ZONE` polygon into `forbidden_zones`.
- **`speed_zone_node`** (`golfcart_navigation`) — subscribes `/course/selected`
  (loads zones from the course zip) + `/odometry/filtered`; publishes
  `SpeedZoneStatus` on `/speed_zone/status` (limit = most-restrictive containing
  zone, or `-1` outside all, or a conservative low limit on stale pose).
- **`speed_zone_math.hpp`** — `point_in_polygon` (ray-casting, concave-safe).
- **Safety Controller** — subscribes `/speed_zone/status` and clamps requested
  velocity to `min(max_linear_velocity_mps, limit_mps)`.

### Key gaps for the course-aware governor (what this plan fills)
1. **No proximity-based slowing** — the current `speed_zone_node` only slows
   *inside* explicit `SPEED_ZONE` polygons. It does nothing near greens/tees/
   water hazards.
2. **No distance-to-polygon** — need a `distance_to_polygon` helper (the
   existing math only has point-in-polygon).
3. **No typed forbidden zones** — fixed by the `CourseMap` change above.

---

## Architecture

```text
/course/selected (zip) → parse_course_zip → forbidden_zones + types + labels
        ↓
   course_governor_node
        │   ├─ for each forbidden zone (green/tee/water/rough/bunker):
        │   │     if trolley within slow_radius_m of the polygon → apply limit
        │   ├─ most-restrictive limit wins
        │   └─ publish /speed_zone/status (reuse SpeedZoneStatus)
        ↓
   Safety Controller (clamps to min(max, limit))
```

### Design decision: extend `speed_zone_node` vs new node
The governor is a natural extension of `speed_zone_node` — both publish
`SpeedZoneStatus` and both are consumed by the Safety Controller's clamp. To
keep the two concerns (explicit zones vs proximity-to-features) cleanly
separated and independently tunable, I'll **extend `speed_zone_node`** to also
apply proximity limits from forbidden zones. This avoids a second publisher on
the same topic and a second subscription in the Safety Controller.

### Behavior
- Load forbidden zones (typed polygons) from the course zip.
- On each fused pose:
  - If inside a `SPEED_ZONE` → that zone's limit (existing behavior).
  - Else if within `slow_radius_m` of a forbidden-zone polygon (green/tee/
    water/rough/bunker) → apply that feature's limit (`feature_limit_mps`).
  - Most-restrictive limit wins.
  - Stale pose → conservative low limit (existing behavior).
- Publish `SpeedZoneStatus` with the active limit + a label describing the
  source (e.g. `"near Green 5"`).

### Config (`config/golfcart.yaml`)
Add to `speed_zone_node`:
```yaml
speed_zone_node:
  pose_timeout_s: 2.0
  stale_limit_mps: 0.3
  slow_radius_m: 3.0          # slow within this distance of a forbidden zone (m)
  feature_limit_mps: 0.5      # limit applied near a green/tee/water/etc. (m/s)
```

### Pure math (`speed_zone_math.hpp`)
Add `distance_to_polygon(p, poly)` — the minimum distance from a point to a
polygon (0 if inside). Unit-tested.

---

## Safety notes
- The governor is a **limit**, not a stop — it goes through the Safety
  Controller's existing clamp (`min(max, limit)`), so it never overrides safety.
- It only *reduces* speed near features; it never increases it.
- Stale pose → conservative low limit (never speed up on unknown data).

## Effort
Medium. Extend `speed_zone_node` + add `distance_to_polygon` to
`speed_zone_math.hpp` + the `CourseMap` typed-forbidden-zones change + config.

## Validation
- Unit tests for `distance_to_polygon` (inside, near edge, far).
- Unit tests for the proximity-limit decision (near a green → limit applied;
  far → no limit; most-restrictive wins).
- Build + existing tests pass.
- Headless Gazebo smoke test: drive near a green, verify `/speed_zone/status`
  reports the reduced limit.