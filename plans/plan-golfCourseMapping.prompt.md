# Plan: Golf Course Mapping Subsystem (GPS + Camera + IMU + LiDAR)

**TL;DR** — Build a ROS 2 mapping subsystem that produces a **metric SLAM map** of the golf course, used for **both** HMI display and future Nav2 navigation. LiDAR (FHL-LD19P) is the primary metric-SLAM input; GPS anchors the map globally; IMU + wheel odometry provide motion; the camera adds **visual odometry** and **semantic labeling** (greens, tees, fairways, hazards). This is a **plan only** — no implementation.

**Key finding:** LiDAR is **already specified** in the architecture (`lidar_node`, `/scan`, FHL-LD19P in §2/§5). The plan incorporates it as the core SLAM sensor and will make its mapping role explicit in the docs.

**Steps**

**Phase 0 — Architecture docs (LiDAR + mapping subsystem)**
1. Update `docs/architecture.md`: document the mapping subsystem (inputs, nodes, data flow, map outputs, SLAM library choice, GPS anchoring, semantic layer). Note LiDAR's role in mapping. Update §41 open decisions.

**Phase 1 — Workspace scaffolding**
2. Create colcon workspace `src/` with packages: `golfcart_msgs`, `golfcart_sensors`, `golfcart_localization`, `golfcart_mapping`, `golfcart_course_mapper`, `golfcart_bringup`.
3. Define message types: `CourseMap`, `CourseFeature` (green/tee/fairway/hazard), `MapPose`. Establish TF2 frame tree `map→odom→base_link→{lidar,camera,imu,gps}_link`.

**Phase 2 — Sensor nodes** *(prereq for all later phases)*
4. `lidar_node`→`/scan`, `gps_node`→`/gps/fix`, `imu_node`→`/imu/data`, `camera_node`→`/camera/image_raw`, `wheel_odometry_node`→`/wheel/odometry`.

**Phase 3 — Localization / sensor fusion** *(depends on 4)*
5. `sensor_fusion_node`: fuse IMU + wheel odometry + GPS → pose; publish `odom→base_link`.

**Phase 4 — Metric SLAM** *(depends on 5)*
6. `mapping_node` (slam_toolbox): 2D LiDAR SLAM → occupancy grid; GPS anchoring; publish `map→odom`.

**Phase 5 — Semantic course mapping** *(depends on 5, camera)*
7. `course_mapper_node` (Python): detect greens/tees/fairways/hazards; geo-reference onto metric map via fused pose; output `CourseMap`.

**Phase 6 — Map serving & HMI** *(depends on 6, 7)*
8. `map_server`; HMI display of course map.

**Phase 7 — Recording, replay & verification** *(depends on all)*
9. ROS 2 bags; RViz; unit + integration tests; manual course drive.

**Relevant files**
- `docs/architecture.md` — add mapping subsystem spec; update §41 decisions.
- `AGENT.md` — confirm LiDAR/mapping node responsibilities (already lists `lidar_node`).
- New packages under `src/` (see Phase 1).

**Verification**
1. `colcon build` succeeds for all packages.
2. Unit tests for sensor fusion and course feature geo-referencing.
3. Integration test: replay a recorded ROS 2 bag through the pipeline; verify occupancy grid + `CourseMap` output.
4. RViz: visualize `/map`, `/scan`, TF tree, and semantic features.
5. Manual: drive the course, confirm map is globally consistent (GPS-anchored) and features are correctly placed.

**Decisions**
- **SLAM library:** recommend `slam_toolbox` (2D LiDAR, ROS 2 native, handles large maps); `cartographer` as alternative for tighter GPS/IMU fusion.
- **Camera visual odometry:** a later refinement to fill GPS/IMU gaps (e.g., under trees); semantic labeling is the primary camera role.
- **Map outputs:** occupancy grid (for Nav2) + semantic `CourseMap` (for HMI).
- **Scope:** plan only; no implementation now.

**Further Considerations**
1. **LiDAR already in docs** — I'll treat it as a confirmed sensor and make its mapping role explicit. Option A: keep as-is and just document. Option B: add a dedicated LiDAR mapping section.
2. **SLAM library choice** — `slam_toolbox` (recommended) vs `cartographer`. Option A / Option B.
3. **Camera VO priority** — include in initial scope, or defer to a later refinement phase?