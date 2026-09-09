# Geofencing / Stay-on-Course

Keep the trolley inside the playable course boundary. A `geofence_node` monitors
the trolley's GPS position against the **outer course boundary polygon**
(configured **per hole** as a single source of truth). When autonomous driving
crosses the boundary it is stopped (priority-3 safety override); when driving
**manually** the operator is notified instead of taking control.

> **Status:** Implemented (validated in simulation). Scope is **boundary-only** —
> inner hazards (water, steep, greens) remain in `CourseMap.forbidden_zones`,
> which navigation already uses to stop.

## Purpose

Prevent the trolley from driving off the course, into hazards, or out of the
playable area.

## Approach

- Define the **outer course boundary** as a concave/multi-vertex polygon in a
  **per-hole YAML config** (the single source of truth, decoupled from
  `CourseMap`).
- Monitor the trolley's GPS position (`/gps/fix`) against the boundary.
- Classify the position: `ARMED` (inside), `NEAR` (within a warn margin),
  `CROSSED` (outside), `OUT_OF_FIX` (stale/invalid GPS), `DISARMED` (operator
  override).
- On a boundary crossing:
  - **autonomous** → publish a **priority-3 zero `MotionRequest`** so the Safety
    Controller stops the trolley;
  - **manual** → notify only (the operator keeps control).
- Always armed on startup; an operator may temporarily disarm via `/geofence`.

## Architecture

```text
config/hole5.yaml (boundary polygon, lat/lon, per hole)
            │  load on start
            ↓
       geofence_node
            │   ├─ subscribe /gps/fix   (lat/lon)  → ENU via course origin
            │   ├─ point-in-polygon + distance-to-boundary
            │   ├─ publish /geofence/status (GeofenceStatus)
            │   └─ [autonomous only] publish MotionRequest priority=3 (stop)
            ↓
   Safety Controller (priority 3 safety override → stop)
   Web / HMI (notification badge on NEAR / CROSSED / OUT_OF_FIX)
```

## Boundary config (one file per hole)

```yaml
# golfcart_geofence/config/course.yaml  (map origin, shared by all holes)
schema_version: 1
course:
  name: "Red Course"
  origin:
    latitude_deg: 48.12345
    longitude_deg: 11.67890
    rotation_rad: 0.0
holes:
  - holes/hole5.yaml

# golfcart_geofence/config/hole5.yaml  (boundary + hole-level props)
schema_version: 1
hole_number: 5
par: 4
handicap: 7
distances: {red: 380, white: 350}
# Outer playable boundary (lat, lon). Concave allowed; >3 vertices.
boundary:
  - {lat: 48.12350, lon: 11.67900}
  - {lat: 48.12400, lon: 11.67920}
  - {lat: 48.12410, lon: 11.67800}
  - {lat: 48.12360, lon: 11.67780}
  - {lat: 48.12340, lon: 11.67840}
```

> **Format:** The map origin lives in `course.yaml` (`course.origin.*`); the
> per-hole file holds only the boundary + hole-level properties (par, handicap,
> per-tee-color distances) + typed features (geometry only). The geofence reads
> the origin from `course_file` (param) and the boundary from `config_file`.
> Legacy per-hole `origin_*` fields are still supported as a fallback. The
> schemas live in `tools/map_editor/schema/` and validate both files.

## Node: `geofence_node` (`golfcart_geofence`)

- **Inputs:** `/gps/fix` (`GpsFix`), `/motion/request` (to detect autonomous
  mode = priority 0).
- **Outputs:** `/geofence/status` (`GeofenceStatus`), and a priority-3 zero
  `MotionRequest` on `/motion/request` when autonomous && crossed.
- **Service:** `/geofence` (`GeofenceTrigger`) — arm/disarm override.
- **Pure math** in `geofence_math.hpp` (unit-tested): `latlon_to_enu`,
  `point_in_polygon` (ray-casting, concave-safe, on-boundary = inside),
  `distance_to_boundary`.
- **Parameters:** `warn_distance_m` (default 5.0), `stop_margin_m` (default 2.0,
  fixed for v1), `gps_timeout_s` (default 3.0), `autonomous_only` (default true).

## Safety

- **Boundary crossing = stop (autonomous).** Implemented as a priority-3
  `MotionRequest` through the Safety Controller's arbitration — not a direct
  motor command. Manual teleop is never forcibly stopped; the operator is
  notified via status.
- **Stale/no GPS** = `OUT_OF_FIX`; for autonomous this is treated as "cannot
  prove we're inside" → safe stop, so the trolley never drives blind past the
  edge.
- **Warn/stop margins** keep the cart from coasting over the line.
- **Boundary config is a plain file** so an operator can review/correct each
  hole's boundary before use.

## Messages

- `golfcart_msgs/GeofenceStatus` — `enabled`, `state`
  (ARMED/NEAR/CROSSED/OUT_OF_FIX/DISARMED), `inside`, `distance_to_boundary_m`
  (signed, negative = outside), lat/lon, fix age, `source`, timestamp.
- `golfcart_msgs/GeofenceTrigger` — arm/disarm (`cancel` flag).

## Validation

- Unit tests for `geofence_math.hpp` (square, concave, collinear-edge, outside,
  degenerate-polygon, on-boundary).
- `scripts/geofence_check.sh` + `scripts/gps_fix_pub.py` — headless smoke test
  driving `/gps/fix` through inside / near / outside and asserting the status
  transitions and the priority-3 stop emission.