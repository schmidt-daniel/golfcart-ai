# Plan: Speed Zones (Speed Limiting by Zone)

**TL;DR** — Automatically limit the trolley's maximum speed inside defined
polygon zones on the course (near greens, tees, cart paths, water hazards,
steep areas). A `speed_zone_node` monitors the trolley's fused position against
the active hole's speed-limit zones and publishes a **speed limit** that the
Safety Controller applies as a motion limit. This is the **software-only**
remaining feature — it reuses the course-map zone data, the geofence
point-in-polygon math, and the Safety Controller's existing limit mechanism. No
new hardware.

**User decisions (confirmed)**
- Zone source: **per-hole speed zones in the course map** (the map editor's
  typed shapes), loaded by `course_loader_node`/`course_registry_node` and
  delivered to `speed_zone_node` via `CourseMap`. This is the single source of
  truth — no separate config file (unlike geofence's per-hole boundary).
- Zone type: a new `SPEED_ZONE` shape type in the map editor, carrying a
  `max_speed_mps` property. Zones are polygons (>=3 vertices).
- Enforcement: **Safety Controller applies the limit** (not the behavior
  layer). `speed_zone_node` publishes the active limit; the Safety Controller
  clamps requested velocity to it. This mirrors how `max_roll_rad` /
  `max_pitch_rad` already work.
- Stale position: **treat as invalid** — if the fused pose is stale, the
  speed-zone node publishes a conservative (low) limit so the trolley never
  speeds up on unknown data. (Matches the geofence "stale GPS = invalid"
  principle.)
- Limit semantics: the zone limit is a **cap** on the Safety Controller's
  `max_linear_velocity_mps`. When inside a zone, the effective max is
  `min(max_linear_velocity_mps, zone_max_speed_mps)`. Outside all zones, the
  configured max applies. The **most restrictive** zone wins when overlapping.
- Surface: `/speed_zone/status` (SpeedZoneStatus) for the web/HMI to display
  the active limit + zone name. No operator control UI needed (zones are
  always active once loaded).

---

## Current state (verified)

- **Course map carries typed zones.** `CourseMap.msg`
  (`src/golfcart_msgs/msg/CourseMap.msg`) has `forbidden_zones` (Polygon[]) and
  `features` (CourseFeature[]). `CourseFeature.msg` has `type` (TEE_BOX / HOLE /
  EXIT_POINT / GREEN / FAIRWAY / HAZARD), `label`, `x`, `y`, `theta`,
  `hole_number`, `tee_id`. The map editor's `Shape` model
  (`tools/map_editor/map_editor/model.py`) has `FORBIDDEN_TYPES` and a flexible
  `type` string; `to_geojson()`/`to_feature()` serialize geometry + props.
- **Course loading is done.** `course_loader_node` / `course_registry_node`
  (`src/golfcart_navigation/src/`) parse the exported course Zip (via
  `course_zip.hpp`) and publish `CourseMap` on `/course/map` (transient-local)
  and `CourseSelected` on `/course/selected`. The registry also publishes the
  costmap on `/map` for Nav2.
- **Point-in-polygon + distance math exists and is unit-tested.**
  `geofence_math.hpp` (`src/golfcart_geofence/src/`) has `latlon_to_enu`,
  `point_in_polygon` (ray-cast, concave-safe), and `distance_to_boundary`.
  These are pure (no ROS deps) and gtest-tested in
  `test/test_geofence_math.cpp`.
- **Safety Controller applies motion limits.**
  `safety_controller_node.cpp` (`src/golfcart_control/src/`) clamps requested
  linear/angular velocity to `max_linear_`/`max_angular_` in `handle_request()`
  (50 Hz). It already subscribes to limit-style inputs (`/slope/status`,
  `/obstacles/state`) and has hard-stop flags (battery / roll / predicted
  roll-pitch / obstacle) in the 50 Hz timer. `max_roll_rad_`/`max_pitch_rad_`
  are declared params.
- **Fused pose.** `/odometry/filtered` (`nav_msgs/Odometry`) is the fused
  pose (EKF: wheel odom + IMU + GPS) in the map frame — the right input for
  zone checks (better than raw `/gps/fix`). `slope_node` already subscribes to
  it with `SensorDataQoS`.
- **Map editor exports zones.** `exporter.py` writes `holes/holeN.yaml` (via
  `Hole.to_yaml()`) + `holes/geojson/holeN.geojson` (via `Shape.to_geojson()`)
  into the course Zip. Adding a `SPEED_ZONE` type flows through automatically.

### Key gaps for Speed Zones (what this plan fills)
1. **No speed-zone data model.** `CourseMap` has no speed-limit zones; the map
   editor has no `SPEED_ZONE` shape type or `max_speed_mps` property.
2. **No speed_zone_node.** Nothing monitors position against zones and emits an
   active speed limit.
3. **No speed-limit input to the Safety Controller.** The controller clamps to a
   static `max_linear_`; it has no way to receive a dynamic zone limit.
4. **No status surface.** No `/speed_zone/status`, no web/HMI display of the
   active limit.

---

## Architecture

```text
course zip (holes/holeN.yaml: SPEED_ZONE shapes + max_speed_mps)
        │  course_loader_node / course_registry_node
        ↓
   /course/map (CourseMap: speed_zones)   /course/selected (CourseSelected)
        │
        ↓
   speed_zone_node
        │   ├─ subscribe /course/selected (transient-local) → load zones
        │   ├─ subscribe /odometry/filtered (SensorDataQoS) → fused pose
        │   ├─ point-in-polygon vs active hole's speed zones
        │   ├─ pick most-restrictive zone (min max_speed_mps)
        │   ├─ stale pose → conservative (low) limit
        │   ├─ publish /speed_zone/status (SpeedZoneStatus)
        │   └─ publish /speed_zone/limit (SpeedZoneLimit)  [or fold into status]
        ↓
   Safety Controller (clamp requested velocity to active limit)
        ↓
   motion/safe_command → ODrive
```

The zone data lives in the **course map** (single source of truth), unlike the
geofence boundary which is a separate per-hole config. `speed_zone_node` gets
everything it needs from `/course/selected` (which carries the `CourseMap` +
`zip_path`), so there is no new config file and no new deployment step.

### Zone data model

Add a `SPEED_ZONE` shape type to the map editor and a `speed_zones` field to
`CourseMap`:

```yaml
# holes/holeN.yaml (map editor export) — a SPEED_ZONE feature
features:
  - type: SPEED_ZONE
    label: "Near green 5"
    max_speed_mps: 0.5
    geometry:
      type: Polygon
      coordinates: [[lon, lat], ...]   # >=3 vertices
```

`CourseMap.msg` gains a parallel structure (mirroring `forbidden_zones`):

```text
# Speed-limit zones (polygons) + their max speed, in the map frame.
geometry_msgs/Polygon[] speed_zones
float64[] speed_zone_limits_mps     # parallel to speed_zones
string[] speed_zone_labels          # parallel, for display
```

`course_zip.hpp` / `course_loader_node` / `course_registry_node` parse
`SPEED_ZONE` features from `holes/holeN.yaml` and populate these arrays. The
map editor's `Shape` model gains `max_speed_mps: Optional[float]` (serialized
only when set) and `SPEED_ZONE` is added to the allowed types (NOT in
`FORBIDDEN_TYPES` — a speed zone is a limit, not a no-go).

### Speed limit semantics

- `speed_zone_node` computes the active limit each pose update:
  - If the pose is stale (`age > pose_timeout_s_`, default 2.0 s) or invalid →
    publish a **conservative limit** (`stale_limit_mps_`, default 0.3) so the
    trolley never speeds up on unknown data.
  - Else, find all zones containing the position; the active limit is the
    **minimum** `max_speed_mps` over containing zones (most restrictive wins).
  - If no zone contains the position → publish `limit = -1` (meaning "no zone
    limit"; the Safety Controller uses its configured `max_linear_`).
- The Safety Controller clamps requested velocity to the active limit:
  `effective_max = (zone_limit >= 0) ? min(max_linear_, zone_limit) : max_linear_`.
  This is a **cap**, not a hard stop — the trolley can still move, just slower.

### Safety Controller integration

Add a `SpeedZoneLimit` subscription (or reuse a single `SpeedZoneStatus` msg
with a `limit_mps` field). In `handle_request()`, clamp:

```cpp
double linear = msg->linear_velocity_mps;
double angular = msg->angular_velocity_radps;
double max_lin = max_linear_;
if (speed_zone_limit_ >= 0.0) {
  max_lin = std::min(max_lin, speed_zone_limit_);
}
linear = std::clamp(linear, -max_lin, max_lin);
angular = std::clamp(angular, -max_angular_, max_angular_);
```

This keeps the limit **in the Safety Controller** (per the architecture: safety
decisions surface through the controller, never bypass it). The zone limit is a
soft cap; the existing hard-stop flags (battery / roll / obstacle / geofence
crossing) still take precedence.

---

## Phases

### Phase 0 — Messages (golfcart_msgs)
1. **`SpeedZoneStatus.msg`** (new) published by `speed_zone_node` on
   `/speed_zone/status`:
   - `bool valid`, `float64 limit_mps` (`-1` = no zone limit), `string zone`,
     `float64 pose_age_s`, `builtin_interfaces/Time timestamp`.
   - Register in `CMakeLists.txt`.
2. **`CourseMap.msg`** — add `speed_zones` (Polygon[]), `speed_zone_limits_mps`
   (float64[]), `speed_zone_labels` (string[]).

### Phase 1 — Map editor: SPEED_ZONE shape type
3. `tools/map_editor/map_editor/model.py` — add `max_speed_mps: Optional[float]`
   to `Shape`; serialize in `to_feature()`/`to_geojson()` (props) and parse in
   `loader.py` `_shape_from_geojson()`. Add `SPEED_ZONE` to the allowed types
   (not `FORBIDDEN_TYPES`).
4. `tools/map_editor/map_editor/canvas.py` / `main_window.py` — add a
   "Speed zone" draw tool (reuses the polygon draw mode) + a `max_speed_mps`
   editor (spinbox) for the selected speed-zone shape.
5. `tools/map_editor/map_editor/exporter.py` — `SPEED_ZONE` flows through
   `to_feature()`/`to_geojson()` automatically; verify `holes/holeN.yaml` +
   `holes/geojson/holeN.geojson` include it.
6. Tests: `test_speed_zone_roundtrip` (shape → yaml → shape preserves
   `max_speed_mps`), `test_speed_zone_not_forbidden`. **Editor tests pass.**

### Phase 2 — Course loading: parse SPEED_ZONE into CourseMap
7. `src/golfcart_navigation/src/course_zip.hpp` — parse `SPEED_ZONE` features
   from `holes/holeN.yaml` into `speed_zones` / `speed_zone_limits_mps` /
   `speed_zone_labels` on `CourseMap` (map-frame polygons via the origin).
8. `course_loader_node.cpp` / `course_registry_node.cpp` — populate the new
   fields (they already build `CourseMap` from the zip).
9. Tests: extend the course-loading test to assert a `SPEED_ZONE` feature
   appears in `CourseMap.speed_zones` with the right limit.

### Phase 3 — speed_zone_node (C++)
10. **Create `src/golfcart_navigation/src/speed_zone_node.cpp`** (or a new
    `golfcart_speed_zone` package — decide based on where it best fits; it
    needs `geofence_math.hpp`'s `point_in_polygon`, so either depend on
    `golfcart_geofence` or move the pure math to a shared location). It:
    - Subscribes `/course/selected` (transient-local) → stores the active
      `CourseMap` speed zones (converted to ENU/map-frame polygons).
    - Subscribes `/odometry/filtered` (`nav_msgs/Odometry`, SensorDataQoS) →
      fused pose.
    - On each pose update: stale/invalid → conservative limit; else
      point-in-polygon vs zones → min limit; else `-1`.
    - Publishes `/speed_zone/status` (SensorDataQoS).
11. Add `speed_zone_node` to `CMakeLists.txt` + `navigation.launch.py` (or
    `core.launch.py`).

### Phase 4 — Safety Controller limit input
12. `safety_controller_node.cpp` — subscribe `/speed_zone/status`; store
    `speed_zone_limit_` (default `-1`). In `handle_request()`, clamp linear to
    `min(max_linear_, speed_zone_limit_)` when `speed_zone_limit_ >= 0`.
    Add `speed_zone_timeout_s_` param (if no status received recently, treat as
    no limit — fail-open to the configured max, since the zone node already
    handles staleness conservatively).

### Phase 5 — Verify
13. `scripts/speed_zone_check.sh` headless e2e: load a course with a
    `SPEED_ZONE`, publish odometry inside the zone → expect
    `/speed_zone/status.limit_mps == zone limit`; outside → `-1`; stale pose →
    conservative limit. **PASS.**
14. Full workspace build (17 pkgs) + all tests pass; editor tests pass.

---

## Gotchas
- **QoS:** `speed_zone_node` uses `SensorDataQoS` (best-effort) for
  `/odometry/filtered` and `/speed_zone/status`. Test subscribers/publishers
  MUST use `QoSProfile(depth=10, reliability=BEST_EFFORT)` or messages are
  silently dropped (same trap as slope_node / summon_node).
- **Frame consistency:** speed zones must be converted to the SAME map/ENU
  frame as the fused pose. Reuse `latlon_to_enu` + the course origin (same as
  geofence / slope_node). Never mix raw lat/lon with map-frame XY.
- **Most-restrictive wins:** overlapping zones → take the minimum limit. Do not
  average.
- **Stale pose = conservative, not fail-open:** the zone node publishes a low
  limit on stale data (the trolley must not speed up when it doesn't know where
  it is). The Safety Controller's own timeout is fail-open to the configured
  max as a backstop, but the zone node's conservative value is the primary
  protection.
- **Limit is a cap, not a stop:** a speed zone slows the trolley; it does not
  halt it. Hard stops remain the job of the existing flags (battery / roll /
  obstacle / geofence crossing).
- **`SPEED_ZONE` is not forbidden:** it must NOT be added to `FORBIDDEN_TYPES`
  (that would make Nav2 treat it as a no-go). It is a limit, not an obstacle.
- **Pure math reuse:** `point_in_polygon` lives in `golfcart_geofence`. Either
  add a `golfcart_geofence` dependency to the speed-zone package or extract the
  pure geometry into a shared location (e.g. `golfcart_navigation` or a small
  `golfcart_geometry` package) to avoid duplicating it.