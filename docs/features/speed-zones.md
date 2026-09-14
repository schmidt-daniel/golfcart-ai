# Speed Limiting by Zone

Automatically limit the trolley's speed in specific areas.

## Purpose

Slow the trolley near greens, tees, cart paths, or other sensitive areas.

## Approach

- Define speed-limit zones (polygons) on the course map.
- Monitor the trolley's position.
- When inside a zone, reduce the maximum allowed speed.

## Architecture

```text
course zip (holes/holeN.yaml: SPEED_ZONE shapes + max_speed_mps)
        ↓  course_loader_node / course_registry_node
   /course/map (CourseMap.speed_zones)   /course/selected
        ↓
   speed_zone_node
        │   ├─ subscribe /course/selected (transient-local) → load zones
        │   ├─ subscribe /odometry/filtered (SensorDataQoS) → fused pose
        │   ├─ point-in-polygon vs active hole's speed zones
        │   ├─ pick most-restrictive zone (min max_speed_mps)
        │   ├─ stale pose → conservative low limit
        │   └─ publish /speed_zone/status (SpeedZoneStatus)
        ↓
   Safety Controller (clamp requested velocity to active limit)
        ↓
   motion/safe_command → ODrive
```

The Safety Controller applies the zone speed limit as a motion limit.

## Implementation

- **Zone data:** a `SPEED_ZONE` shape type in the map editor carries a
  `max_speed_mps` property (polygon, >=3 vertices). Exported into
  `holes/holeN.yaml` and loaded into `CourseMap.speed_zones` /
  `speed_zone_limits_mps` / `speed_zone_labels` (parallel arrays, map frame).
- **`speed_zone_node`** (`golfcart_navigation`): subscribes `/course/selected`
  (transient-local) + `/odometry/filtered` (SensorDataQoS); publishes
  `/speed_zone/status` (SensorDataQoS).
- **Limit semantics:** inside a zone → that zone's `max_speed_mps`
  (most-restrictive wins on overlap); outside all zones → `-1` (no limit);
  stale pose (`pose_timeout_s`, default 2 s) → conservative `stale_limit_mps`
  (default 0.3 m/s).
- **Safety Controller:** subscribes `/speed_zone/status`; clamps requested
  linear velocity to `min(max_linear_velocity_mps, limit_mps)` when
  `limit_mps >= 0`. The limit is a **cap**, not a stop.

## Safety

- Speed limits are enforced by the Safety Controller, not by the behavior
  layer.
- Requires reliable localization; stale GPS must be treated as invalid (the
  zone node publishes a conservative low limit on stale pose).
- `SPEED_ZONE` is a limit, not a forbidden zone — it is NOT added to
  `FORBIDDEN_TYPES` (Nav2 does not treat it as a no-go).

## Effort

Medium. Requires GPS/localization plus a zone-checking node that feeds speed
limits to the Safety Controller.