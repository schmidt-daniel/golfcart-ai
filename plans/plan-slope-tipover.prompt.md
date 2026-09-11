# Plan: Runtime Roll/Pitch Slope (Tip-Over Prevention)

**TL;DR** — Make the trolley predict the terrain slope under/around it and stop
before it can tip over. Two complementary mechanisms: (1) Nav2 path avoidance
using a slope-derived costmap, and (2) a predictive tip-over guard that
projects the ground-fixed terrain gradient onto the trolley's heading to get
trolley-relative roll/pitch, and commands a stop if either exceeds a limit.

## Design

The editor already computes a slope costmap from a DEM. The key insight for
tip-over prevention is that **roll/pitch are trolley-relative** and depend on
heading (yaw), while the terrain gradient is **ground-fixed** (east/north).
So we export the ground-fixed gradient and let the runtime project it onto the
current yaw.

```
DEM (editor) --compute_gradient--> dz/dx, dz/dy (ground-fixed, east/north)
        --write_gradient--> holeN_slope_gradx.pgm / _grady.pgm / _grad.yaml
        --exporter--> holes/holeN_slope_gradx.pgm ... (in course zip)

course_registry_node --select--> publishes costmap on /map (Nav2 static_layer)
slope_node --/course/selected + /odometry/filtered-->
        pitch = dzdx*cos(yaw) + dzdy*sin(yaw)
        roll  = -dzdx*sin(yaw) + dzdy*cos(yaw)
        --/slope/status (SlopeStatus)-->
safety_controller --|roll|>max_roll OR |pitch|>max_pitch while moving--> STOP
```

## Steps

### Phase 1 — Editor gradient export
1. `dem.py`: `compute_gradient(z, cell_size_m)` -> `(dzdx, dzdy)` (Horn's
   method, ground-fixed east/north). Refactor `compute_slope` to use it.
2. `dem.py`: `write_gradient(dzdx, dzdy, resolution, origin_x, origin_y,
   base_path)` -> float32 P5 PGM `{base}_gradx.pgm`/`{base}_grady.pgm` +
   `{base}_grad.yaml`.
3. `main_window._generate_slope_costmap`: call `compute_gradient` +
   `write_gradient` alongside the existing costmap write.
4. `exporter.py`: bundle `holes/holeN_slope_gradx.pgm`/`_grady.pgm`/`_grad.yaml`
   into the zip (siblings of the costmap pgm).
5. Tests: `test_compute_gradient`, `test_project_onto_heading` (ROS yaw
   convention: yaw=0 faces east), `test_write_gradient_roundtrip`,
   `test_export_includes_gradient`. **25 editor tests pass.**

### Phase 2 — Runtime slope_node
6. `golfcart_msgs/msg/SlopeStatus.msg`: `roll_rad, pitch_rad, slope_deg, valid,
   timestamp`. Register in CMakeLists.
7. `slope_node.cpp`: subscribe `/course/selected` (transient-local) +
   `/odometry/filtered` (SensorDataQoS); publish `/slope/status`
   (SensorDataQoS). `load_gradient()` finds `holes/*_gradx.pgm` in the zip,
   derives `_grady.pgm`. `parse_gradient_pgm()` reads P5/65535/float32.
   `publish_slope()` maps position to grid cell, projects gradient onto yaw.
8. Add `slope_node` to `CMakeLists.txt` + `course.launch.py`.

### Phase 3 — Predictive tip-over guard
9. `safety_controller_node.cpp`: subscribe `/slope/status`; if
   `|roll|>max_roll_rad_` OR `|pitch|>max_pitch_rad_` (default 0.6) while
   MOVING/LIMITED -> READY + `publish_safe(0,0)`. Add `max_pitch_rad_` param.

### Phase 4 — Nav2 avoidance (already wired)
10. Registry publishes the slope costmap on `/map` (transient-local);
    `global_costmap.yaml` static_layer consumes `/map`
    (`map_subscribe_transient_local: true`). Verified present.

### Phase 5 — Verify
11. `scripts/test_slope_node.py` headless e2e: select course, publish odometry
    at (5,5) yaw=0 with gradient dzdx=0.1 -> expect pitch=0.100 rad (5.7deg),
    roll=0.000. **PASS.**
12. Full workspace build (17 pkgs) + 66 tests pass; 25 editor tests pass.

## Gotchas
- **QoS:** slope_node uses SensorDataQoS (best-effort). Test subscribers and
  publishers MUST use `QoSProfile(depth=10, reliability=BEST_EFFORT)` or
  messages are silently dropped.
- **ROS yaw convention:** yaw=0 = +x/east (NOT north), CCW positive. The
  projection formulas assume this.
- **Gradient is ground-fixed; roll/pitch are yaw-dependent.** Do not store
  roll/pitch in the map; compute at runtime from gradient + yaw.
- **PGM format:** float32 P5 with maxval 65535, raw little-endian float32
  payload. Must match between `write_gradient` and `parse_gradient_pgm`.