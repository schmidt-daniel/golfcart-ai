# Course-Aware Speed Governor

Slow the trolley automatically near course features that warrant caution
(greens, tees, water hazards, rough, bunkers) using their **polygon geometry**
from the course map — not just explicit speed-limit zones.

> **Status:** Implemented. See `plans/plan-course-governor.prompt.md` for the
> full design.

## Purpose

The existing speed zones only slow the trolley *inside* explicit `SPEED_ZONE`
polygons. This feature extends that: the trolley also slows when it is **near**
a forbidden zone (green, tee, water hazard, rough, bunker), which is where
caution matters most on a golf course.

## Overview

```text
/course/selected (zip) → parse_course_zip → forbidden_zones + types + labels
        ↓
   speed_zone_node (extended)
        │   ├─ inside a SPEED_ZONE → that zone's limit
        │   ├─ within slow_radius_m of a forbidden zone → feature_limit_mps
        │   ├─ most-restrictive limit wins
        │   └─ publish /speed_zone/status
        ↓
   Safety Controller (clamps to min(max, limit))
```

## How it works

- **Typed forbidden zones:** `CourseMap` now carries `forbidden_zone_types` /
  `forbidden_zone_labels` parallel to `forbidden_zones`, so each polygon keeps
  its type (GREEN, TEE_BOX, WATER_HAZARD, ...) and label. Previously this info
  was lost when the parser pushed the polygons.
- **Proximity:** `distance_to_polygon(p, poly)` returns the minimum distance
  from the trolley to a forbidden-zone polygon (0 if inside). When within
  `slow_radius_m`, the governor applies `feature_limit_mps`.
- **Most-restrictive wins:** if the trolley is inside a `SPEED_ZONE` *and* near
  a forbidden zone, the lower limit applies.
- **Label:** the status reports the source, e.g. `"near Green 5"`.

## Safety considerations

- The governor is a **limit**, not a stop — it goes through the Safety
  Controller's existing clamp (`min(max, limit)`), so it never overrides safety.
- It only *reduces* speed near features; it never increases it.
- Stale pose → conservative low limit (never speed up on unknown data).

## Configuration

See `config/golfcart.yaml`:

```yaml
speed_zone_node:
  pose_timeout_s: 2.0               # pose older than this = stale (s)
  stale_limit_mps: 0.3              # conservative limit when pose is stale (m/s)
  slow_radius_m: 3.0                # slow within this distance of a forbidden zone (m)
  feature_limit_mps: 0.5            # limit applied near a green/tee/water/etc. (m/s)
```

## Messages

- `golfcart_msgs/CourseMap` — gained `forbidden_zone_types` /
  `forbidden_zone_labels` (parallel to `forbidden_zones`).
- `golfcart_msgs/SpeedZoneStatus` — unchanged (reused).

## Validation

- `distance_to_polygon` unit-tested (inside → 0, near edge, near corner, on
  edge).
- Full workspace test suite passes.
- Headless Gazebo smoke test: drive near a green, verify `/speed_zone/status`
  reports the reduced limit.