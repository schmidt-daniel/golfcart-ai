# Plan: Golf Course Mapping Subsystem (GPS + Camera + IMU + LiDAR)

**TL;DR** — Build a ROS 2 mapping subsystem that produces a **metric SLAM map** of the golf course, used for **both** HMI displayand future Nav2 navigation. LiDAR (FHL-LD19P)is the primary metric-SLAM input; GPS anchors the map globally; IMU + wheel odometry provide motion(and IMU provides dead reckoning for GPS-loss navigation); the camera adds **visual odometry** and **semantic labeling** (greens, tees, fairways, hazards). This is a **plan only** — no implementation.



> **Alignment with the navigation plan** (`plan-autonomous-navigation.prompt.md`):
> - **SLAM-in-the-loop:** mapping can run **while driving** the direct route to a target (no-map/incomplete-map case), not only as a separate pre-mapping phase.

> - **Dead reckoning:** IMU is fused into the EKF (`sensor_fusion_node`)for dead reckoning, so the trolley can navigate short distances without GPS(e.g. under trees; GPS loss degrades gracefully to odom+IMU.

> - **Priority arbitration:** the Safety Controller uses `MotionRequest.priority` to arbitrate(option B); manual > navigation. This is a prerequisite for safe autonomous driving and is implemented in the navigation plan's Phase  5.
>
> - **Sensor mounting calibration:** measured sensor offsets(lidar/imu/gps/camera relative to base_link)are a hardware prerequisite for accurate SLAM/localization; added later, but the URDF static-transform structure is defined now.

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
5. `sensor_fusion_node`: fuse IMU + wheel odometry + GPS → pose; publish `odom→base_link`. **IMU provides dead reckoning** so the trolley can navigate short distances without GPS(e.g. under trees); GPS loss degrades gracefully to odom+IMU.

**Phase  ́4 — Metric SLAM** *(depends on 5)*
6. `mapping_node` (slam_toolbox):2D LiDAR SLAM → occupancy grid; GPS anchoring; publish `map→odom`. **SLAM-in-the-loop:** mapping can run **while driving** the direct route to a target(no-map/incomplete-map case,, building the course map as it goes(see navigation plan Phase  4/5.
6b. **Per-cell slope data:** while mapping, record **per-cell slope** from the IMU(static inclination, not dynamic acceleration)onto the metric map. **Record roll and pitch separately** — roll(side slope)is the primary tip-over risk, so it gets its own layer with a lower lethal threshold and higher cost weight; pitch(fore/aft slope)gets a separate layer with a higher lethal threshold and lower cost weight. This feeds the navigation plan's **slope cost layer**(see navigation plan Phase  5, step  7b.. Store as roll/pitch slope layers alongside the occupancy grid.

**Phase 5 — Semantic course mapping** *(depends on 5, camera)*
7. `course_mapper_node` (Python): detect greens/tees/fairways/hazards; geo-reference onto metric map via fused pose; output `CourseMap`. **Course features:** also record the exact location of **tee boxes**, the **hole**,the **hole number**,the **assignment to a golf course**,and optional **"exit points"** (where the trolley can leave the course). These are stored in the `CourseMap` and used by the HMI and navigation(see navigation plan Phase  1, step  2b..

**Phase 6 — Map serving & HMI** *(depends on 6, 7)*
8. `map_server`; HMI display of course map.

**Phase 7 — Recording, replay & verification** *(depends on all)*
9. ROS 2 bags; RViz; unit + integration tests; manual course drive.
   **Implemented:** off-board map building via `scripts/record_bag.sh`,
   `scripts/build_map_offline.sh`, and `golfcart_mapping/offline_mapping.launch.py`
   (see `docs/architecture.md` §34.2).

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
- **SLAM library:** **confirmed** — `slam_toolbox` (2D LiDAR, ROS 2 native, lightweight for RPi 5, handles large maps, simple per-course map save/load, available for Lyrical.. GPS anchoring is handled by the `robot_localization` EKF, so cartographer's built-in fusion is redundant..
- **Camera visual odometry:** **deferred** — VO is a later refinement to fill GPS/IMU gaps (e.g., under trees, wheel slip); **not in initial scope**. The core stack (wheel odom + IMU + GPS)already covers the main cases. Semantic labelingis the primary camera role in the initial scope..
- **Map outputs:** occupancy grid (for Nav2) + semantic `CourseMap` (for HMI).
- **Scope:** plan only; no implementation now.
- **Dead reckoning:** IMU is fused into the EKF for dead reckoning; GPS loss degrades gracefully to odom+IMU(short-distance navigation under trees.
- **SLAM-in-the-loop:** mapping can run while driving the direct route to a target(no-map/incomplete-map case,, building the course map as it goes(see navigation plan.
- **Priority arbitration:** Safety Controller uses `MotionRequest.priority` to arbitrate(option B); manual > navigation(implemented in navigation plan Phase  5.
- **Per-cell slope data:** record per-cell slope from the IMU(static inclination)onto the metric map as slope layers;; **roll and pitch recorded separately**(roll = side slope, primary tip-over risk, lower lethal threshold + higher cost weight; pitch = fore/aft slope, higher lethal threshold + lower cost weight.. Feeds the navigation plan's slope cost layer(see navigation plan Phase  5, step  7b..
- **Course features:** record the exact location of **tee boxes**,the **hole**,the **hole number**,the **assignment to a golf course**,and optional **"exit points"** (where the trolley can leave the course)in the `CourseMap`; used by the HMI and navigation(see navigation plan Phase  1, step  2b..



**Further Considerations**
1. **LiDAR already in docs** — I'll treat it as a confirmed sensor and make its mapping role explicit. Option A: keep as-is and just document. Option B: add a dedicated LiDAR mapping section.