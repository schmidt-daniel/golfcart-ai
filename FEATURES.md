# Feature Implementation Tracker

Tracks which golf cart features have been implemented. Update this file as
features are added.

## Implemented

### MVP: Joystick Motor Control
- `golfcart_msgs` — MotionRequest, MotorCommand, MotorState, BatteryState messages
- `golfcart_odrive` — MotorController interface, ODrive driver (USB, JSON protocol), mock, serial port, `odrive_node`, `battery_node`
- `golfcart_control` — `motion_controller_node` (differential drive, 50 Hz), `safety_controller_node` (state machine, 50 Hz, request timeout, battery-critical stop)
- `golfcart_teleop` — `joystick_node` (USB gamepad), `arduino_joystick_node` (Arduino Uno, serial), `keyboard_teleop_node`, `web_teleop_server`
- `golfcart_bringup` — launch files: `joystick_control`, `keyboard_control`, `web_teleop`
- Arduino sketch: `arduino/joystick_interface/joystick_interface.ino` (2 axes + button, 50 Hz serial)
- Web teleop: `web/index.html` (roslib.js, rosbridge_server, directional buttons, enable/stop, live status)
- Docker build/test: `docker/` (Dockerfile, build.sh, docker-compose.yml)

### Remote E-Stop + Live Telemetry Dashboard
- `web/dashboard.html` — phone/web page with a large **hold-to-confirm E-STOP**
  button (1.5 s hold → `/safety/stop`) and a **RE-ENABLE** button
  (`/safety/enable`)
- Live telemetry panel: safety state, speed (`/motor/state`), battery
  (voltage + charge %), GPS position, navigation status, geofence state,
  active speed-zone limit, slope
- Fault badges: battery low (<20%), geofence CROSSED/OUT_OF_FIX/NEAR, obstacle
  in zone, steep slope (>10°)
- Linked from `index.html`; added to `setup.py` data_files so it's installed
  and served by the web teleop server

### Battery Monitoring
- `battery_node` (INA219 over I2C, publishes `BatteryState` on `/battery/state`)
- Safety Controller stops motion on critical battery
- **Voltage divider required:** INA219 max bus voltage is 26 V, but the 36 V battery reaches ~42 V. A divider (e.g. R1=R2=100k, divide by 2)keeps VBUS ≤ 26 V; `battery_node` scales the measured voltage via the `voltage_scale` param (default 2.0).
- **Note:** INA219 I2C register read is a scaffold (returns `valid=false` until `/dev/i2c-N` read implemented)

### ODrive Driver (implemented, pending hardware validation)
- USB serial, ODrive 0.5.6 native protocol (CRC32, endpoint hash, JSON framing)
- axis0 = left, axis1 = right (verify against physical wiring)
- **Note:** JSON interface (slower); binary endpoint interface possible later
- **Status:** code complete and tested; not yet validated against a physical ODrive

### IMU Integration
- `golfcart_imu` package — `ImuSensor` interface, `MockImuSensor`, `ImuSensorImpl` (scaffold), `imu_node`
- Publishes `ImuData` (roll/pitch) on `/imu/data` at 50 Hz
- Safety Controller stops motion on excessive roll (tip-over risk, `max_roll_rad` param)
- **Note:** real IMU I2C driver is a scaffold (uses mock until implemented)

### GPS Integration
- `golfcart_gps` package — `GpsSensor` interface, `MockGpsSensor`, `GpsSensorImpl` (scaffold), `gps_node`
- Publishes `GpsFix` (lat/lon/alt/speed/heading) on `/gps/fix` at 1 Hz
- **Note:** real GPS NMEA driver is a scaffold (uses mock until implemented)

### LiDAR + Obstacle Detection
- `golfcart_lidar` package — `LidarSensor` interface, `MockLidarSensor`, `LidarSensorImpl` (scaffold), `lidar_node`
- `lidar_node` publishes standard `sensor_msgs/LaserScan` on `/scan` at 10 Hz
- `obstacle_detection_node` publishes `ObstacleState` on `/obstacles/state` (stopping zone)
- Safety Controller stops motion on obstacle in the stopping zone
- **Note:** real FHL-LD19P driver is a scaffold (uses mock until implemented)

### Auto-Shutdown
- `golfcart_power` package — `auto_shutdown_node` (watchdog)
- Shuts down after configurable idle period
- **Suppressed on roll-away risk** (slope from IMU, or motion detected)
- Publishes `power/status`

### Energy-Saving Mode
- `golfcart_power` package — `energy_saver_node`
- Enters SLEEP after `sleep_timeout_s` idle; publishes `PowerState` on `/power/state`
- Publishes `sleep`/`wake` on `/power/sleep_cmd` so other nodes reduce rates
- **IMU stays active** and wakes the cart on motion (inclination change)
- **Suppressed on roll-away risk** (never sleeps on a slope / while moving)
- Lighter than auto-shutdown: automatic wake, shorter timeout (see `docs/features/energy-saving.md`)

### Hill Assist, Hill Descent Brake, Rollback Protection
- `golfcart_behavior` package — `hill_rollback_node`
- **Rollback Protection:** detects unintended backward movement on a slope (from wheel encoders) and requests a brake
- **Hill Descent Brake:** limits speed on downhill slopes to prevent uncontrolled acceleration
- **Hill Assist:** provides propulsion assistance on uphill slopes
- Publishes `MotionRequest` (higher priority than manual) and `behavior/status`
- Uses IMU pitch + wheel velocity with hysteresis

### Push Assist (Pedelec-Style)
- `push_assist_node` (`golfcart_behavior`) — detects how hard the user
  pushes/brakes the handle (load cell in the handle unit) and requests
  proportional motor assistance (pedelec principle)
- **Slope compensation** — subtracts the gravity component (IMU pitch) so
  assist is based on user force only, not the hill
- **Dead zone + hysteresis** — activates above `deadzone_n + hysteresis_n`,
  stays on until `deadzone_n - hysteresis_n` (no on/off flapping)
- Proportional assist clamped to `max_assist_mps`; brake on negative force
- Publishes `MotionRequest` (priority 2, above teleop, below safety)
- `golfcart_msgs` — `HandleForce.msg` (calibrated push/brake force, N);
  `handle_gateway` publishes it on `/handle/force`
- Pure math in `test_push_assist.cpp` (effective force, dead zone/hysteresis,
  assist clamp) — unit-tested

### Localization (sensor fusion)
- `golfcart_localization` package
- `wheel_odometry_node` — subscribes `/motor/state`, publishes `nav_msgs/Odometry` on `/wheel/odometry` + TF `odom→base_link`
- `sensor_fusion_node` — bridges custom `ImuData`→`sensor_msgs/Imu` (`/imu/data_raw`) and `GpsFix`→`sensor_msgs/NavSatFix` (`/gps/fix_std`)
- `robot_localization` EKF (`config/ekf.yaml`) fuses wheel odom + IMU + GPS → `/odometry/filtered`
- `localization_quality_node` — monitors EKF covariance, publishes `NavigationStatus` DEGRADED/OK on `/localization/quality`

### Course Mapping (SLAM)
- `golfcart_mapping` package — `slam_toolbox` online async mapping
- `config/mapper_params_online_async.yaml` (base_frame=base_link, scan_topic=/scan)
- `launch/mapping.launch.py` — `sync_slam_toolbox_node`
- **Off-board map building:** `config/mapper_params_offline.yaml` +
  `launch/offline_mapping.launch.py` (replay recorded bag through slam_toolbox)
- `scripts/record_bag.sh` (record SLAM topics on the cart) and
  `scripts/build_map_offline.sh` (replay bag → occupancy grid via `map_saver_cli`) —
  see `docs/architecture.md` §34.2

### Autonomous Navigation / Nav2
- `golfcart_navigation` package
- `navigation_node` — bridges `cmd_vel`→`MotionRequest` (priority 0), `/set_goal` service→`navigate_to_pose` action, publishes `NavigationStatus`, pauses on manual override
- `georeference_node` — converts lat/lon→map-frame via `/set_goal_geo` service
- Nav2 stack (manual bringup): planner (NavFn), controller (Regulated Pure Pursuit), bt_navigator, velocity_smoother, global/local costmaps
- `golfcart_msgs` — `GoalPose`, `NavigationStatus`, `CourseMap`, `SetGoal.srv`

### Summon (Drive to Operator's Phone)
- `golfcart_navigation/summon_node` — tracks the operator's live phone GPS position and drives the trolley to it via the navigation stack
- Two targeting modes: **CURRENT** (live position) and **PREDICT** (intercept — estimates operator velocity and aims at their predicted position at the trolley's ETA, with a "hold if approaching" rule)
- Single `/summon` service (`SummonTrigger.srv`, `cancel` flag): start/restart/cancel
- Safety: target-loss stop, accuracy gate (≤ 2.5 m), max distance, timeout, arrival radius, slow-down on approach, physical stop override, network-loss → drive to last planned location + phone notification
- Phone page: `web/summon.html` (publishes `/phone/gps`, mode toggle, SUMMON/CANCEL, route + progress display, arrival/obstacle notifications)
- HMI: `hmi_node` `summon_to()`/`cancel_summon()` + summon status subscription
- `golfcart_msgs` — `PhoneFix.msg`, `SummonStatus.msg`, `SummonTrigger.srv`
- Pure math in `summon_math.hpp` (velocity estimation, predictive target, approach-cone) — unit-tested
- Gazebo: `gps_dropout_node.py` (simulate GPS loss); `scripts/summon_check.sh` (sim smoke test)

### Follow Me (Person Following)
- LiDAR-first person following behind a `PersonTarget` abstraction (camera/fusion later)
- `person_detection_node` (`golfcart_lidar`) — /scan → /person/target; sector gate → clustering → cluster filter, motion + leg-pair disambiguation, temporal validation, EMA smoothing
- **Lock-on acquisition** — waits for the first person within 2 m, then continuously tracks that person (never switches to a closer stranger)
- `follow_controller_node` (`golfcart_follow`) — follow-behind control law → `MotionRequest` (priority 0); potential-field obstacle steering, hold-on-stop/resume
- `obstacle_awareness_node` (`golfcart_follow`) — /scan → /obstacles/awareness (soft steering obstacle view, separate from safety stop)
- `/follow` service + `/follow/status`; `golfcart_msgs` — `PersonTarget.msg`, `FollowStatus.msg`, `FollowTrigger.srv`; `Obstacle.msg` gained a `source` field
- Gazebo: person model in course world; `scripts/follow_check.sh` (sim smoke test)

### Operating Mode + Obstacle Steering Assist (Manual)
- `mode_node` (`golfcart_control`) — publishes `/mode/state` (MANUAL/FOLLOW/
  AUTONOMOUS/TELEOP); `/mode/set` service; gateway maps HMI mode selection
- Safety Controller — obstacle hard-stop only when NOT in MANUAL mode (operator
  has full control in manual); subscribes `/mode/state`
- `steering_assist_node` (`golfcart_follow`) — in MANUAL mode, gently steers
  away from nearby obstacles (priority-1 angular nudge) instead of hard-stopping;
  subscribes `/obstacles/awareness` + `/mode/state`
- HMI toggle: `AssistConfig.msg` + `/assist/config`; gateway `_menu_assist`
  toggles steering assist; `steering_assist_node` only nudges when enabled; HMI
  Assist screen reflects ON/OFF via `ST_STEERING_ASSIST`
- `golfcart_msgs` — `ModeState.msg`, `AssistConfig.msg`
- Pure nudge math in `test_steering_assist.cpp` — unit-tested

### Geofencing (Stay-on-Course)
- `geofence_node` (`golfcart_geofence`) — /gps/fix → /geofence/status; keeps the trolley inside the outer course boundary polygon
- **Per-hole boundary config** (`config/hole5.yaml`) as the single source of truth (lat/lon polygon, decoupled from CourseMap)
- States: ARMED / NEAR / CROSSED / OUT_OF_FIX / DISARMED; always armed on startup
- **Autonomous crossing → priority-3 zero MotionRequest** (Safety Controller stops); manual → notify only
- `/geofence` service (arm/disarm) + `/geofence/status`; web/HMI notification badge on NEAR/CROSSED/OUT_OF_FIX
- `golfcart_msgs` — `GeofenceStatus.msg`, `GeofenceTrigger.srv`
- Pure math in `geofence_math.hpp` (point-in-polygon, distance-to-boundary) — unit-tested
- `scripts/geofence_check.sh` + `scripts/gps_fix_pub.py` (headless smoke test)

### GPS-Denied Dead-Reckoning Fallback
- `localization_quality_node` (`golfcart_localization`) — now publishes
  `OK` / `DEGRADED` / `LOST` on `/localization/quality` (LOST = no valid GPS fix
  for `gps_timeout_s`)
- `dead_reckoning_node` (`golfcart_localization`) — bounded dead-reckoning
  policy: when GPS is lost, keep driving on the fused pose for
  `max_dr_time_s` / `max_dr_distance_m`, then publish a priority-3 stop
- Publishes `DeadReckoningStatus` on `/dead_reckoning/status` (state, budget
  used/remaining) for the HMI/web badge
- `geofence_node` — suppresses its `OUT_OF_FIX` stop while DR is driving within
  budget (geofence remains the backstop if the DR node is absent)
- `golfcart_msgs` — `DeadReckoningStatus.msg`
- Pure math in `dead_reckoning_math.hpp` (budget integration, budget-exceeded
  decision) — unit-tested
- Config: `dead_reckoning_node` section + `gps_timeout_s` for the quality node
  (`config/golfcart.yaml`)

### Gazebo Simulation (gz-sim)
- `golfcart_gazebo` package
- Course world (`worlds/course.sdf`) with obstacles, green, tee, water hazard, slope ramp, steep zone
- Cart model embedded in SDF with `gz_ros2_control` (diff_drive_controller + joint_state_broadcaster) and gz-sim sensors (LiDAR, IMU, GPS, camera)
- `ros_gz_bridge` bridges `/scan`, `/imu`, `/gps`, `/camera/image`, `/clock`
- `cmd_vel_converter` — bridges nav `Twist`→controller `TwistStamped`
- `launch/sim.launch.py` — `ros2 launch golfcart_gazebo sim.launch.py [headless:=true]`
- `scripts/sim_check.sh` — headless sanity check (topics, sim time, cart movement)

### Learning / Map Refinement
- `tools/map_editor/map_editor/refine.py` — merge recorded observations into an
  existing course map (confidence-weighted blending + Gaussian spatial
  smoothing so new/old data align without border artifacts)
- `scripts/extract_flags.py` — reads a recorded rosbag (`record_bag.sh --all`)
  and extracts discrete flags (drivable / steep / obstacle) in the course map
  frame
- `scripts/refine_map.sh` — end-to-end: bag -> obs.jsonl -> refined course zip
- **Alignment:** observations rasterize onto the exact existing costmap grid
  (same origin + resolution); blending uses a baseline weight for the existing
  DEM data so unobserved cells are preserved and borders transition smoothly
- `gradient_from_imu()` recovers the ground-fixed gradient from trolley-relative
  IMU pitch/roll + yaw (inverse of the slope_node projection)
- 34 editor tests pass (9 new refine tests)

### Speed Zones (Speed Limiting by Zone)
- `speed_zone_node` (`golfcart_navigation`) — limits the trolley's max speed
  inside course speed-limit zones
- **Zone data:** a `SPEED_ZONE` shape type in the map editor carries a
  `max_speed_mps` property (polygon); exported into `holes/holeN.yaml` and
  loaded into `CourseMap.speed_zones` / `speed_zone_limits_mps` /
  `speed_zone_labels` (parallel arrays, map frame)
- **Limit semantics:** inside a zone → that zone's `max_speed_mps`
  (most-restrictive wins on overlap); outside all zones → `-1` (no limit);
  stale pose → conservative low limit (never speed up on unknown data)
- **Safety Controller** subscribes `/speed_zone/status` and clamps requested
  velocity to `min(max_linear_velocity_mps, limit_mps)` — the limit is a cap,
  not a stop
- `golfcart_msgs` — `SpeedZoneStatus.msg`; `CourseMap` gained `speed_zones` /
  `speed_zone_limits_mps` / `speed_zone_labels`
- Pure math in `speed_zone_math.hpp` (point-in-polygon) — unit-tested;
  `scripts/test_speed_zone_node.py` headless e2e

### Course-Aware Speed Governor
- Extends `speed_zone_node` (`golfcart_navigation`) — also slows the trolley
  when it is **near** a forbidden zone (green, tee, water hazard, rough,
  bunker), not just inside explicit `SPEED_ZONE` polygons
- **Typed forbidden zones:** `CourseMap` gained `forbidden_zone_types` /
  `forbidden_zone_labels` (parallel to `forbidden_zones`) so each polygon keeps
  its type/label (previously lost in the parser)
- **Proximity:** `distance_to_polygon` (0 if inside) — when within
  `slow_radius_m` of a forbidden zone, applies `feature_limit_mps`;
  most-restrictive limit wins; status label like `"near Green 5"`
- Config: `slow_radius_m` + `feature_limit_mps` for `speed_zone_node`
- Pure math in `speed_zone_math.hpp` (`distance_to_polygon`) — unit-tested

### Battery Range Estimator (self-learning)
- `range_estimator_node` (`golfcart_navigation`) — estimates remaining range
  from battery charge + terrain slope + remaining hole distance; publishes
  `/range/status` (OK / CAUTION / CRITICAL)
- **Self-learning energy model:** slope-bucketed `Wh/m` (downhill/flat/uphill)
  updated via online EMA from measured battery current × distance, so it
  improves with every round (no persistence across reboots in v1)
- Informational only — never commands motion; reserve + margin keep the
  estimate conservative
- `golfcart_msgs` — `RangeStatus.msg`
- Pure math in `range_estimator_math.hpp` (slope bucketing, EMA learning, range
  estimate, warning states) — unit-tested

### Energy Dashboard (HMI)
- New **ENERGY** screen on the ESP32 handle-unit HMI showing the range
  estimator's `/range/status`: remaining range (colored by state), state badge
  (OK/CAUTION/CRITICAL), remaining hole distance, return distance, battery %
- `handle_gateway` subscribes `/range/status` and forwards to the ESP32 via new
  state IDs (`ST_RANGE_M`/`ST_RETURN_M` uint16, `ST_RANGE_STATE` uint8);
  ENERGY item added to the main menu
- Informational only — never commands motion
- `test_protocol.py` encode tests; firmware builds (RAM 29.7%, Flash 15.5%)

### Deployment (Option D, Hybrid)
- `systemd/` — systemd units per service, auto-start on boot + restart on crash:
  `golfcart-core`, `golfcart-teleop`, `golfcart-localization`, `golfcart-mapping`,
  `golfcart-navigation`, `golfcart-follow`, `golfcart-geofence`
- `golfcart_bringup/core.launch.py` — always-on control pipeline (odrive, motion,
  safety, battery, IMU, GPS, LiDAR, obstacle, hill/rollback, auto-shutdown)
- `golfcart_bringup/web_server.launch.py` — web teleop servers only (rosbridge + HTTP)
- `scripts/deploy.sh` — build in Docker, rsync code+maps to the Pi, install/update
  systemd units, restart services (code via git, maps/config via rsync)
- `scripts/install_services.sh` / `scripts/uninstall_services.sh` — manage services on the Pi

## Not yet implemented (documented only)

See `docs/features/` for design docs. The software-only roadmap batch is
complete (Remote E-Stop + Telemetry, GPS-Denied Fallback, Obstacle Steering
Assist, Battery Range Estimator).

- Learning on-board flags (design doc `docs/features/learning.md` describes an
  on-board recorder; we instead derive flags off-board from rosbags — see the
  Learning section above)

## Key decisions (permanent)

See `docs/architecture.md` §35 for details.

- ROS 2 Lyrical / Ubuntu 26.04
- ODrive: USB
- QoS: `SensorDataQoS` for high-rate streams, services for discrete ops
- Control rates: safety 50 Hz, motion 50 Hz, state 20 Hz
- Coordinate frames: REP-103
- Executor: single-threaded per node
- Messages: `MotionRequest` in, `geometry_msgs/Twist` out
- Battery: INA219 over I2C
