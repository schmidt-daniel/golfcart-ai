# Plan: Geofencing / Stay-on-Course

**TL;DR** — Keep the trolley inside the playable course boundary. A
`geofence_node` monitors the trolley's GPS position against the **outer course
boundary polygon** (configured **per hole** as a single source of truth). When
autonomous driving approaches/crosses the boundary it is stopped (priority-3
safety override); when driving **manually** it warns the operator instead of
taking control. Scope is `boundary-only`, and it reuses the existing course-map
origin / geometry code.

**User decisions (confirmed)**
- Boundary source: **one boundary config file per hole** as the single source of
  truth (decoupled from `CourseMap`). E.g. `config/hole5.yaml`.
- Scope: **boundary-only**. Inner forbidden zones (water, steep, greens) stay in
  `CourseMap.forbidden_zones` (navigation already stops on them); geofence
  enforces the outer "cannot leave the course" polygon.
- Violation action: **warn near the boundary; stop only for autonomous when the
  polygon is actually crossed; manual = warn only.**
- Bounds: concave / multi-vertex polygon (ray-casting point-in-polygon).
- Geometry: lat/lon stored in the config/web (human-readable); converted to
  **map-frame** (global-metric XY, via the existing course map origin) for the
  in-node integer check.
- Trigger / surface: `/geofence` service (Trigger-style enable/disable) +
  `/geofence/status`. **Always armed on startup.** Web/HMI show a **notification**
  when the geofence is hit (NEAR/CROSSED/OUT_OF_FIX) — no full control UI.

---

## Current state (verified)

- **GPS:** `gps_node` publishes `GpsFix` on `/gps/fix`
  (`src/golfcart_gps/src/gps_node.cpp`). Fields: `latitude_deg, longitude_deg,
  altitude_m, speed_mps, heading_rad, valid, timestamp`. Quality:
  `SensorDataQoS` (best-effort).
- **Course origin / map-frame**: `georeference_node`
  (`src/golfcart_navigation/src/georeference_node.cpp`) holds the map origin
  (lat/lon + rotation) and exposes `latlon_to_map()` — a simple equirectangular
  approximation (valid for course-scale distances). Map-frame = local **meters**
  XY (x = east, y = north) rotated to the map origin.
- **Arbitration**: `safety_controller_node`
  (`src/golfcart_control/src/safety_controller_node.cpp`) implements priority
  arbitration (option B): `MotionRequest.priority` (0=autonomous, 1=manual,
  2=behavior, 3=safety override) and hard-stop flags (battery / roll / obstacle)
  in a 50 Hz timer. A zero `MotionRequest` releases the priority lock.
- **MotionRequest** (`golfcart_msgs`): `{linear_velocity_mps,
  angular_velocity_radps, source, priority, timestamp}`.
- **Web app** (`src/golfcart_teleop/web/index.html`): already has SUMMON button,
  geolocation, /gps/fix marker, nav status; pattern to extend for geofence.
- **HMI** (`src/golfcart_teleop/golfcart_teleop/hmi_node.py`): scaffold with
  `Trigger`-style method calls.
- **Sim** (`golfcart_gazebo`): course world with a `navsat` GPS sensor at
  `gps_link` (~0.7 m height) publishing to `/gps` bridged to `/gps/fix`; a
  `gps_dropout_node.py` already simulates GPS loss for Summon.

### Key gaps for Geofence (what this plan fills)
1. **No per-hole boundary data model.** There is no single source of truth for
   "the outer course boundary for hole N."
2. **No geofence node.** Nothing monitors GPS position vs a boundary polygon and
   enforces it.
3. **No boundary enforcement into safety.** A violated boundary should reduce to
   a safety-layer decision, but nothing emits that now.
4. **No geofence notification surface.** No /geofence/status, no web/HMI enable /
   warn / status.

---

## Architecture

```text
config/hole5.yaml (boundary polygon, lat/lon, per hole)
            │  load on start (declared config file param)
            ↓
       geofence_node
            │   ├─ subscribe /gps/fix   (lat/lon)  → map-frame via origin
            │   ├─ point-in-polygon check + distance-to-boundary margin
            │   ├─ classification: INSIDE / NEAR / CROSSED / NO_FIX
            │   ├─ publish /geofence/status (GeofenceStatus)
            │   └─ [autonomous only] publish MotionRequest priority=3 (stop)
            │                         on crossing; warn on NEAR
            ↓
   Safety Controller (priority 3 safety override → stop)
   HMI / web (enable toggle + status badge)
```

The boundary config is the **source of truth**; `CourseMap` is deliberately not
used for the outer polygon (it is used for inner `forbidden_zones` and features,
separately). The only thing geofence needs from elsewhere is the **course-map
origin** (lat/lon + rotation) to convert lat/lon → map-frame — same parameters
the `georeference_node`/`summon_node` use.

### State machine (always armed)
```
ARMED (on startup)
  OUT_FIX  → OUT_OF_SERVICE   (stale GPS; autonomous: safe stop — can't confirm inside)
  INSIDE   → normal
  NEAR (within warn_distance_m) → publish NEAR + notify (manual & autonomous)
  CROSSED  → autonomous: publish MotionRequest(priority 3, zero) → stop
             manual: publish CROSSED + notify only (no stop)
  back inside → INSIDE (auto-resume)
```

> **Why a priority-3 MotionRequest (not a direct motor command):** per the
> architecture, safety decision surfaces by holding a motion request through
> the Safety Controller's arbitration, not by commanding motors directly. A
> zero priority-3 request takes precedence and stops autonomous driving; it also
> releases when it stops.

### Boundary config (one file per hole)

Supported top-level keys:

```yaml
# golfcart_geofence/config/hole5.yaml
course_name: "Red course"
hole_number: 5
origin_latitude_deg: 48.12345   # course map origin (lat)
origin_longitude_deg: 11.67890   # course map origin (lon)
origin_rotation_rad: 0.0
# Outer playable boundary (lat, lon). Order can be CW or CCW; must be closed
# (>3 vertices). Concave allowed (edge-crossing ray-cast handles it).
boundary:
  - {lat: 48.12350, lon: 11.67900}
  - {lat: 48.12400, lon: 11.67920}
  - {lat: 48.12410, lon: 11.67800}
  - {lat: 48.12360, lon: 11.67780}
  - {lat: 48.12340, lon: 11.67840}
# Optional per-hole inner zones (tee box, green marker). Not enforced by
# geofence (navigation's forbidden_zones own internal hazards); kept for
# humans/web display.
zones:
  - {type: TEE, lat: 48.12355, lon: 11.67830, radius_m: 2.0}
  - {type: HOLE, lat: 48.12390, lon: 11.67860, radius_m: 1.0}
```

### GeofenceStatus message (`golfcart_msgs`)

```text
bool      enabled         # false while temporarily disarmed (operator override)
string    state          # ARMED / NEAR / CROSSED / OUT_OF_FIX / DISARMED
bool      inside         # inside the polygon
float64   distance_to_boundary_m   # signed: negative = outside
float64   latest_lat
float64   latest_lon
float64   last_fix_age_s
string    source         # hole5
builtin_interfaces/Time timestamp
```

### Node: `geofence_node` (C++)

Pure, unit-testable math separated into `geofence_math.hpp` (like
`summon_math.hpp`).

- `point_in_polygon(x, y, poly)` — ray-casting, O(v), concave-safe.
- `distance_point_to_boundary(x, y, poly)` — min distance to any segment; used
  for the `NEAR` margin and for "distance outside" reporting.
- v1 uses a **fixed** `stop_margin_m` distance cushion (cart speed is capped
  ~1 m/s, so the distance is sufficient). Reserved: a future velocity-based
  look-ahead margin can compute `stop_margin + v * t_stop` from `GpsFix.speed_mps`
  without changing the node interface (parameterized).

Subscriptions:
- `/gps/fix` (`GpsFix`) — trolley position (the only position source for v1).
  Freshness via `valid` + a stale-age threshold (e.g. `gps_timeout_s`, default
  3 s). On stale/invalid → `OUT_OF_FIX` (autonomous: stop - treat as cannot
  confirm inside).

Publications:
- `/geofence/status` (`GeofenceStatus`)
- `/motion/request` (`MotionRequest`) — only when autonomous && CROSSED; source=
  `geofence`, priority=3, linear=angular=0.0.

Services:
- `/geofence` (`std_srvs/srv/Trigger` style; or a dedicated `GeofenceTrigger.srv`
  with `cancel` like `FollowTrigger`) — **arm/disarm override** (optional).
  Starts ARMED; an operator may temporarily disarm (e.g. maintenance) and re-arm.
  Status reports `state=ARMED`/`DISARMED`.

Parameters:
- `config_directory` (path to `config/*.yaml`), `hole_name` / `config_file`.
- `warn_distance_m` (default 5.0) — how close before `NEAR`.
- `stop_margin_m` (default 2.0) — fixed distance cushion before the boundary (v1).
  (Reserved: future speed-lookahead uses `stop_margin + v * t_stop`.)
- `gps_timeout_s` (default 3.0).
- `autonomous_only` (default true) — whether stop action applies to autonomous
  requests only (hard), vs all sources. Manual → warn only regardless.
- `origin_*` params — from the loaded config (not duplicated).

Autonomous vs manual:
- The node knows the trolley's driving mode. Simplest robust proxy: watch
  `/motion/request` sources, or an `autonomous_active` boolean from the
  autonomy/control stack. If `autonomous_only` and the current autonomous source
  is active → enforce stop. If manual/teleop → never block, but still publish
  CROSSED/NEAR to the web/HMI.

### Launch + integration

- `launch/geofence.launch.py` — starts `geofence_node` with a hole config.
- Wire into `golfcart_bringup` core launch (an `golfcart-geofence` entry) and a
  `systemd/golfcart-geofence.service`.
- `scripts/geofence_check.sh` — headless sim smoke test: launch sim + core +
  geofence; move the cart (or patch /gps/fix) to be inside, near, and outside;
  assert `/geofence/status` publishes the expected states and that a priority-3
  zero MotionRequest is emitted only when autonomous && crossed.

### Web / HMI

Web/HMI display a **notification** when the geofence is hit (NEAR / CROSSED /
OUT_OF_FIX) — a status badge/alert driven by `/geofence/status`. No full
control UI (enable/disable stays on the service/CLI). The status message exposes
a clean interface for the badge.

---

## Phases

### Phase 0 — Plan + docs scope
Update `docs/features/geofencing.md` (replace stub), `FEATURES.md` (move
"Geofencing" from "Not yet implemented" to an implemented section stub),
`docs/architecture.md` (table entry + data-flow note). Add the plan file (this
file). No code yet.

### Phase 1 — Messages + config + math
1. `golfcart_msgs`: add `GeofenceStatus.msg`; add `GeofenceTrigger.srv` (or reuse
   `std_srvs/srv/Trigger`; documented). Register in `CMakeLists.txt`.
2. `golfcart_geofence` package: `package.xml`, `CMakeLists.txt`, `launch/`,
   `config/` (sample `hole1.yaml`), and `src/geofence_math.hpp` with:
   `point_in_polygon`, `dist_point_to_segment`, `dist_point_to_polygon`,
   `project_cartesian_between_latlon` (or reuse georef). Unit-test the math
   (`test/test_geofence_math.cpp`): square, concave, collinear-edge, outside,
   degenerate-polygon cases.

### Phase 2 — Node
- `src/geofence_node.cpp`: load config (YAML), subscribe `/gps/fix`,
  point-in-polygon check, NEAR/CROSSED thresholds, stale-GPS → OUT_OF_FIX,
  publish `/geofence/status`, serve `/geofence`, and on autonomous+CROSSED
  publish priority-3 zero `MotionRequest`.
- Smoke: build + run in a headless sim manually first.

### Phase 3 — Integration + notification surface
- Launch + `golfcart-geofence.service` + bringup wiring.
- `scripts/geofence_check.sh` (sim smoke test).
- Web app: subscribe `/geofence/status` and show a **notification** when
  NEAR / CROSSED / OUT_OF_FIX (no enable toggle, no polygon overlay).
- HMI: subscribe `/geofence/status` and show the same notification.

### Phase 4 — Validation + docs
- Unit + integration tests; `geofence_check.sh` results pass.
- Export/sim demo: place cart inside/outside in `course.sdf` (or move via
  `gz`/GPS fixture) and confirm enforcement.
- Finalize FEATURES.md, geofencing.md, architecture.
- Commit.

---

### Safety considerations

- **Boundary crossing = stop (autonomous).** Implemented as a priority-3
  MotionRequest through the Safety Controller's arbitration — not a direct motor
  call. Manual teleop is never forcibly stopped; the operator is told via
  status/warn.
- **Stale/no GPS** = `OUT_OF_FIX`; for autonomous this is treated as "cannot
  prove we're inside" → safe stop, so the cart never drives blind/past the edge.
- **Warn/stop margins** keep the cart from coasting over the line.
- **Always armed on startup.** In manual mode the operator is only notified;
  in autonomous mode a boundary crossing forces a safe stop through the Safety
  Controller's priority-3 arbitration (never a direct motor command).
- **Boundary config is a plain file** so an operator can review/correct each
  hole's boundary before use.

### Effort
Small-to-medium. Reuses the GPS origin + existing `MotionRequest` arbitration;
core new work is the static polygon check + a boundary config + the node
wrapper + CLI/service surface.

### Open decisions (flagged for confirmation)
1. ~~Start state~~ **RESOLVED:** always armed on startup (notification in manual
   mode, stop in autonomous mode).
2. ~~Predictive stop margin~~ **RESOLVED:** v1 uses a fixed `stop_margin_m`
   (cart speeds are low); a future speed-lookahead is parameterized but not built now.
3. ~~GPS source envelope~~ **RESOLVED:** `/gps/fix` only for v1 (`OUT_OF_FIX`
   already fails safe).