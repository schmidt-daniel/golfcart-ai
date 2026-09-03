# Plan: Autonomous Navigation to Target (Golf Course)

**TL;DR** — Implement the full navigation stack so the trolley can drive to a target set on the web map or HMI: wheel odometry → sensor fusion (TF2) → SLAM mapping → Nav2 (costmap/planner/controller) → a `navigation_node` that converts Nav2 `cmd_vel` into `MotionRequest` → Safety Controller. Add Nav2 + Gazebo to the Docker image. Wire the existing web SUMMON buttonand HMI to a goal service.

**User decisions (confirmed))**
- Scope: Full stack (localization → Nav2 → target)
- Nav2: Add `ros-lyrical-nav2-*` to Docker image
- Target source: Both web map + HMI
- Simulation: Gazebo
- **Priority arbitration:** Safety Controller uses `priority` to arbitrate (option B). Manual override always wins.



- **Dead reckoning:** IMU is fused into the EKF for dead reckoning (so the trolley can navigate short distances without GPS, e.g. under trees). GPS loss degrades gracefully to odom+IMU.



- **Forbidden zones:** the trolley **stops in front of** a forbidden zone (green/tee/water/steep)rather than entering it. Nav2 costmap marks forbidden zones as lethal; the planner avoids them; if the goal is inside one, the goal is rejected.



- **No/incomplete map:** if no map exists or the map is incomplete, the trolley takes the **direct route** to the target, watching for obstacles and slopes,and **mapping the course as it drives**(SLAM-in-the-loop). It does not wait for a pre-built map.



- **Georeferencing:** a `georeference_node` converts web lat/lon targets → map-frame goals(using map origin lat/lon + rotation from GPS anchoring).



- **Sensor mounting calibration:** URDF static transforms use measured sensor offsets; calibration is a hardware prerequisite(added later, considered in plan).
- **Arrival:** no explicit "arrived" report needed; the operator sees the trolley arrive(no extra state machine).
- **Range/battery:** no max-distance check needed; routes are usually ≤200 m(not in scope).
- **Zone speed limits:** speed-by-zone(narrow passes, greens)is deferred; Nav2 velocity smoother handles global max speed(considered in plan.



**Current state(verified))**
- No wheel odometry node exists; no TF2; no nav_msgs; no localization.



- `MotionRequest`(source/priority) → Safety Controller → `Twist` → Motion Controller → ODrive. Nav2 outputs `Twist`; we need a bridge node to convert Nav2 `cmd_vel` → `MotionRequest` with source=`navigation`, priority=low(below user.
- Safety priority order(architecture §8): Emergency/Fault > Safety Stop > Obstacle Stop > Rollback/Descent > Motion Limits > User Command > Autonomous Behavior. So autonomous commands must be lowest priority. Manual override must always win.



**Steps**

**Phase  0 — Dependencies(Docker)**
1. Add to `docker/Dockerfile`: `ros-${ROS_DISTRO}-nav2-*`(nav2-bringup, nav2-core, nav2-costmap-2d, nav2-planner, nav2-controller, nav2-velocity-smoother, nav2-amcl, nav2-map-server,nav2-behavior-tree,nav2-msgs), `ros-${ROS_DISTRO}-tf2`, `ros-${ROS_DISTRO}-tf2-ros`, `ros-${ROS_DISTRO}-nav-msgs`, `ros-${ROS_DISTRO}-robot-localization`, `ros-${ROS_DISTRO}-slam-toolbox`, `ros-${ROS_DISTRO}-gazebo-ros-pkgs`, `ros-${ROS_DISTRO}-gazebo-ros`, `ros-${ROS_DISTRO}-joint-state-publisher-gui`, `ros-${ROS_DISTRO}-rviz2`, `ros-${ROS_DISTRO}-xacro`, `ros-${ROS_DISTRO}-robot-state-publisher`. Rebuild image.



**Phase  1 — Messages + TF2 frames**
2. Add messages to `golfcart_msgs`: `GoalPose.msg`(x,y,theta,frame_id,source,timestamp), `NavigationStatus.msg`(state,progress,error,timestamp), `CourseMap.msg`. Add `nav_msgs/Odometry` usage(standard,no custom msg needed for odom..
2b. **`CourseMap` minimum contents:** the per-course map stores: occupancy grid + slope layers(roll/pitch) + forbidden zones + map origin(lat/lon + rotation). **Course features:** also store the exact location of **tee boxes**, the **hole**, the **hole number**, the **assignment to a golf course**, and optional **"exit points"** (where the trolley can leave the course). These course features are used by the HMI and future course-mapping semantic layer(see course-mapping plan).
3. Document TF2 frame tree in architecture §17: `map→odom→base_link→{lidar,camera,imu,gps}_link`. Add static transforms for sensor mounts(URDF/xacro in a new `golfcart_description` package). Sensor offsets measured during hardware calibration(added later, considered here.



**Phase  2 — Wheel odometry**(depends on 1)
4. New `golfcart_localization` package: `wheel_odometry_node`(C++): subscribes `/motor/state`(encoder feedback), computes differential-drive odometry, publishes `nav_msgs/Odometry` on `/wheel/odometry` + TF2 `odom→base_link`. Reuse `DifferentialDrive` from `golfcart_control`. Unit-testthe integration.



**Phase  3 — Sensor fusion / localization**(depends on 2)
5. `sensor_fusion_node`(C++, `robot_localization` EKF): fuse `/wheel/odometry` + `/imu/data` + `/gps/fix` → continuous pose; publish `odom→base_link`. **GPS is fused into the EKF** so the fused pose is globally-anchored(no drift over time). **IMU provides dead reckoning** so the trolley can navigate short distances without GPS(e.g. under trees); GPS loss degrades gracefully to odom+IMU. Configure via YAML. Unit-testwith mocked sensor inputs.
5b. **Localization-quality gate:** monitor the fused pose uncertainty from the EKF. If uncertainty exceeds a threshold(e.g. GPS lost for too long, or odom/IMU drift too high), navigation **pauses** and asks the operator(proposal: pause + ask, since the target is defined in GPS coordinates and we can't reliably reach it blind.. Thresholds are tunable parameters.



**Phase  4 — Mapping(SLAM)**(depends on 3)
6. `mapping_node`(`slam_toolbox`): 2D LiDAR SLAM → occupancy grid; publish `map→odom`. **GPS anchoring:**the fused GPS-anchored pose from Phase  3 anchors the SLAM map globally(so the metric map aligns with real-world GPS coordinates, enabling web-map targets in lat/lon to map to map-frame goals). **Map origin:** store the **map origin** (lat/lon + rotation) with each per-course map; this is the datum the `georeference_node` uses to convert web lat/lon targets → map-frame goals. **SLAM-in-the-loop:**if no map exists or the map is incomplete, mapping runs while driving the direct route(see Phase  5), building the course map as it goes. Save/load per-course maps.(This is the course-mapping plan's Phase  4; implement here as the map source for Nav2.)



**Phase  5 — Nav2 stack**(depends on 3, 4)
7. Configure Nav2: costmap(static from course map + live LiDAR obstacles; forbidden zones marked lethal; planner(NavFn/Smac; controller(**Regulated Pure Pursuit (RPP)**; velocity smoother(global max speed; speed-by-zone deferred. Launch via `golfcart_bringup`(new `navigation.launch.py`.7a. **Obstacle integration (replan ad-hoc):** feed the LiDAR `/scan` into Nav2's obstacle costmap layer(in the correct frame, `lidar_link`→`base_link`→`map`.. When an obstacle appears mid-route, Nav2 **replans around it ad-hoc**; only if it cannot find a path does it stop and ask the operator(retry/replan/stop)via the web app..7b. **Slope cost layer:** add a costmap layer that assigns a **graded cost** to cells based on slope(from the course map's per-cell slope data), with a **lethal threshold** for steep/tip-over zones. **Roll vs pitch asymmetry:** the trolley is far more likely to tip over **sideways (roll)** than forward/backward (pitch), so the costmap treats them differently:
   - **Roll (side slope):** lower lethal threshold(e.g. >8–10°and higher cost per degree — side-slope is the primary tip-over risk.

   - **Pitch (fore/aft slope):** higher lethal threshold(e.g. >15–20°and lower cost per degree — the trolley can handle steeper fore/aft slopes safely..
   - Mild slopes(within thresholds)get a small graded cost(flat =  ́0 cost.. Tune the cost weights so the planner **prefers flat surfaces when detours are short**, but **doesn't take long detours** to avoid mild slopes(Nav2's planner minimizes total cost, so it naturally balances this tradeoff..
8. `navigation_node`(C++, bridge): subscribes Nav2 `cmd_vel` → publishes `MotionRequest`(source=`navigation`, priority=lowest.. Also provides a **`/set_goal` service** (taking a `GoalPose`)→ forwards to Nav2 `navigate_to_pose` action; publishes `NavigationStatus`. Enforces: only navigate when safety state is READY; stop on fault/obstacle; manual override wins(priority. **Pause on manual override:** when a manual request arrives, navigation **pauses**(holds its state); when manual stops, navigation **resumes**(if still valid..



9. **Priority arbitrationin Safety Controller:** modify `safety_controller_node` to use `MotionRequest.priority` for arbitration(option B): when a higher-priority source is active, lower-priority requests are ignored(so navigation cannot override manual control.. **Concrete priority numbers:** assign fixed priority values: `0` = autonomous/navigation(lowest), `1` = manual(joystick/keyboard/web), `2` = behavior(hill assist, rollback protection,, `3` = safety override(highest.. Document as a permanent decisionin the architecture. Unit-testthe arbitration logic.



**Phase  6 — Target interface + georeferencing**(depends on  ́5)
10. `georeference_node`(C++/Python): converts web lat/lon targets → map-frame goals(using map origin lat/lon + rotation from GPS anchoring). Validates the goal: rejects goals inside forbidden zones or unreachable(with timeout; returns a clear error message.
.
11. Web: wire the existing SUMMON button + map click in `web/index.html` to call a `/set_goal` service(via rosbridge)withthe tapped lat/lon. Add a "Cancel" button. Show navigation status(planning/driving/stopped/error.
.
12. HMI: add a "Navigate to target" menu item in `hmi_node`(Python)that sets a goal via the same `/set_goal` service.(HMI spec already defines the menu.)



**Phase  7 — Gazebo simulation**(depends on 5)
13. Create a `golfcart_gazebo` package: URDF/xacro model of the cart(base_link, wheels, lidar, imu, gps, camera),a simple course world(flat,with obstacles/greens/tees/water/steep zones; Gazebo plugins for differential drive + LiDAR + IMU + GPS. Launch a simulated course. Verify: navigation to a target avoiding obstacles; stops in front of forbidden zones; manual override stops it; obstacle mid-route stops + asks operator; no-map case takes direct route + maps as it drives.

13b. **Simulation fidelity note:**the simulated course world should include **slopes**(to test the roll/pitch slope costmap behavior)and **GPS dropouts**(to test dead reckoning + localization-quality gate). These are important for validating the terrain-aware planningand GPS-loss handling.



**Phase  8 — Deployment (Option D, Hybrid)**
14. **systemd units:** create systemd units for each launch file(e.g. `golfcart-navigation.service`, `golfcart-teleop.service`, etc.)that auto-start on boot and restart on crash. Run the ROS 2 nodes as services on the RPi 5.
15. **`scripts/deploy.sh`:** a deploy script that: builds in Docker, rsyncs code+maps to the Pi, installs/updates systemd units,, restarts services.. **Code via git** (versioned, rollback easy); **maps/config via rsync** (per-course maps are large binary data, kept out of git to avoid repo bloat, pushed when updated..



**Relevant files**
- `docker/Dockerfile` — add Nav2/tf2/nav_msgs/robot_localization/slam_toolbox/gazebo/rviz/xacro deps..
- `src/golfcart_msgs/msg/` — add `GoalPose.msg`, `NavigationStatus.msg`, `CourseMap.msg`.
- `src/golfcart_localization/`(new) — `wheel_odometry_node`, `sensor_fusion_node`.
- `src/golfcart_mapping/`(new) — `mapping_node`(slam_toolbox.
- `src/golfcart_navigation/`(new) — `navigation_node`(Nav2 bridge,, `georeference_node`, Nav2 config YAMLs..
- `src/golfcart_control/src/safety_controller_node.cpp` — add priority-based arbitration(option B..
- `src/golfcart_description/`(new) — URDF/xacro + static TF2 sensor mounts..
- `src/golfcart_gazebo/`(new) — simulated cart + course world..
- `src/golfcart_bringup/launch/navigation.launch.py`(new) — Nav2 + navigation_node + georeference + mapping..
- `src/golfcart_teleop/web/index.html` — wire SUMMON to `/set_goal` service; add Cancel; show status..
- `src/golfcart_teleop/golfcart_teleop/hmi_node.py`(new) — add "Navigate to target" menu item..
- `scripts/deploy.sh`(new) — build in Docker, rsync code+maps to the Pi, install/update systemd units,, restart services..
- `systemd/`(new) — systemd unit files for each launch(e.g. `golfcart-navigation.service`, `golfcart-teleop.service`..
- `docs/architecture.md` — document TF2 frames, Nav2 integration, priority arbitration, forbidden-zone stop, no-map direct-route behavior, update §34 open decisions(localization approach,, simulation environment,, deployment).
- `docs/features/navigation.md`, `summon.md`, `route-replay.md`, `geofencing.md` — update to reflect implemented stack..
- `FEATURES.md` — mark Autonomous Navigation / Nav2, Localization, Course Mapping as in-progress/implemented..



**Verification**
1. `colcon build` + `colcon test` — all packages build; unit tests pass(odometry integration,, sensor fusion,, priority arbitration,, navigation_node stop logic,, georeference validation).
2. Gazebo: launch simulated course;; set a target via web map;; verify the trolley plans a path avoiding obstaclesand reaches the target;; verify it stops in front of forbidden zones;; verify manual override stops it;; verify obstacle mid-route stops + asks operator;; verify no-map case takes direct route + maps as it drives..
3. Replay a recorded bag through the pipeline;; verify occupancy grid + odom + pose output..
4. RViz: visualize `/map`, `/scan`, TF tree, costmap, planned path,and trolley pose..
5. Manual course test(real hardware): drive to a target;; verify safety stops on fault/obstacle/steep zone;; verify GPS-loss dead reckoning(odom+IMU)keeps it on course short distances..



**Decisions**
- **Nav2** for planning/control(per existing plan + user confirmation.
- **Localization:** `robot_localization` EKF fusing wheel odom + IMU + GPS; SLAM(`slam_toolbox`)for local precision + map..
- **SLAM library:** **confirmed** — `slam_toolbox` (2D LiDAR, ROS 2 native, lightweight for RPi 5, excellent for large maps, simple per-course map save/load, available for Lyrical.. GPS anchoring is handled by the `robot_localization` EKF, so cartographer's built-in fusion is redundant..
- **Nav2 controller:** **confirmed** — **Regulated Pure Pursuit (RPP)** (slow, precise tracking for a slow cart; lightweight on RPi 5; simpler to tune; obstacle handling is already via costmap replanning, so DWB's local-avoidance advantage is redundant; more predictable for a safety-critical vehicle..
- **Deployment/update:** **confirmed (Option D, Hybrid)** — **code via git + systemd** (versioned, rollback easy, auto-start on boot, restart on crash); **maps/config via rsync** (per-course maps are large binary data, kept out of git to avoid repo bloat, pushed when updated.. A `scripts/deploy.sh` builds in Docker, rsyncs code+maps to the Pi, installs/updates systemd units,, restarts services..
- **GPS anchoring:** **confirmed** — GPS is fused into the EKF pose, which anchors the SLAM map globally(so the metric map aligns with real-world GPS coordinates, enabling web-map targets in lat/lon to map to map-frame goals).
- **Dead reckoning:** **confirmed** — IMU is fused into the EKF for dead reckoning; GPS loss degrades gracefully to odom+IMU(short-distance navigation under trees..
- **Priority arbitration:** **confirmed(option B)** — Safety Controller uses `MotionRequest.priority` to arbitrate; manual(joystick/keyboard/web)has higher priority than navigation; navigation cannot override manual control.. **Concrete priority numbers:** `0` = autonomous/navigation(lowest), `1` = manual, `2` = behavior(hill assist, rollback protection,, `3` = safety override(highest.. Document as a permanent decisionin the architecture..
- **Pause on manual override:** **confirmed** — when a manual request arrives, navigation **pauses**(holds its state); when manual stops, navigation **resumes**(if still valid..
- **Obstacle replanning:** **confirmed** — feed LiDAR `/scan` into Nav2's obstacle costmap layer; when an obstacle appears mid-route, Nav2 **replans around it ad-hoc**; only if it cannot find a path does it stop and ask the operator(retry/replan/stop)via the web app..
- **Localization-quality gate:** **confirmed(proposal)** — monitor fused pose uncertainty from the EKF; if it exceeds a threshold(e.g. GPS lost too long, odom/IMU drift too high,, navigation **pauses** and asks the operator. Thresholds are tunable parameters..
- **Map origin:** **confirmed** — store the **map origin** (lat/lon + rotation)with each per-course map; this is the datum the `georeference_node` uses to convert web lat/lon targets → map-frame goals..
- **CourseMap contents:** **confirmed(proposal)** — per-course map stores: occupancy grid + slope layers(roll/pitch) + forbidden zones + map origin(lat/lon + rotation). **Course features:** also store the exact location of **tee boxes**, the **hole**,the **hole number**,the **assignment to a golf course**,and optional **"exit points"** (where the trolley can leave the course). Used by the HMI and future course-mapping semantic layer..
- **Forbidden zones:** **confirmed** — trolley stops in front of forbidden zones(green/tee/water/steep); Nav2 costmap marks them lethal;; planner avoids them;; goal inside a forbidden zone is rejected..
- **No/incomplete map:** **confirmed** — if no map exists or incomplete, trolley takes direct route to target, watching obstacles/slopes,and mapping the course as it drives(SLAM-in-the-loop..
- **Georeferencing:** **confirmed** — `georeference_node` converts web lat/lon → map-frame goals(using map origin + rotation from GPS anchoring); validates goals(rejects forbidden/unreachable,, with timeout..
- **Map:** per-course occupancy grid from SLAM + GPS anchoring;; semantic `CourseMap` for HMI(deferred to course-mapping plan.
- **Slope cost layer:** **confirmed** — costmap assigns a **graded cost** to cells based on slope(from per-cell slope data in the course map), with a **lethal threshold** for steep/tip-over zones. **Roll vs pitch asymmetry:** roll(side slope)has a lower lethal threshold(e.g. >8–10°and higher cost per degree(primary tip-over risk); pitch(fore/aft slope)has a higher lethal threshold(e.g. >15–20°and lower cost per degree. Mild slopes get a small graded cost(flat =  ́0 cost.. Tune the cost weights so the planner prefers flat surfaces when detours are short, but doesn't take long detours to avoid mild slopes(Nav2 minimizes total cost, balancing this tradeoff..

- **Target interface:** both web map(SUMMON)and HMI menu, via a shared `/set_goal` service..
- **Autonomous priority:** lowest(below user/manual,, per architecture §8 safety priority order.. Manual override always wins..
- **Simulation:** Gazebo(simulated cart + course world.
- **Scope:** full stack;; excludes negative-obstacle depth camera(GIXVISION,, learning/route-replay refinement, summon phone-side app(web SUMMON covers it,, geofencing enforcement(deferred; Nav2 costmap handles boundaries,, speed-by-zone(deferred; velocity smoother handles global max,, max-distance/battery check(not needed; routes ≤200 m,, explicit arrival report(not needed; operator sees arrival..



**Further Considerations**
1. **SLAM library** — **confirmed: `slam_toolbox`** (2D LiDAR, ROS 2 native, lightweight for RPi 5, excellent for large maps, simple per-course map save/load, available for Lyrical.. GPS anchoring is handled by the `robot_localization` EKF, so cartographer's built-in fusion is redundant..
2. **Nav2 controller** — **confirmed: Regulated Pure Pursuit (RPP)** (slow, precise tracking for a slow cart; lightweight on RPi 5; simpler to tune; obstacle handling is already via costmap replanning, so DWB's local-avoidance advantage is redundant; more predictable for a safety-critical vehicle..
3. **Deployment/update mechanism** — **confirmed: Option D (Hybrid)** — **code via git + systemd** (versioned, rollback easy, auto-start on boot, restart on crash); **maps/config via rsync** (per-course maps are large binary data, kept out of git to avoid repo bloat, pushed when updated.. A `scripts/deploy.sh` builds in Docker, rsyncs code+maps to the Pi, installs/updates systemd units, restarts services..
4. **Sensor mounting calibration** — measured sensor offsets(lidar/imu/gps/camera relative to base_link)are a hardware prerequisite for accurate SLAM/localization; added later, but the URDF static-transform structure is defined now..
5. **Speed-by-zone** — narrow-pass/green speed limits deferred;; Nav2 velocity smoother handles global max speed for now; revisit with course-mapping semantic layer..
6. **Obstacle mid-route human-in-the-loop** — resolved: Nav2 replans around obstacles ad-hoc first; only if it cannot find a path does it stop and ask the operator(retry/replan/stop)via the web app. Concrete UX + navigation_node state machine to be detailed during implementation..