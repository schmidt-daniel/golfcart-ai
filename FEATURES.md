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

### Hill Assist, Hill Descent Brake, Rollback Protection
- `golfcart_behavior` package — `hill_rollback_node`
- **Rollback Protection:** detects unintended backward movement on a slope (from wheel encoders) and requests a brake
- **Hill Descent Brake:** limits speed on downhill slopes to prevent uncontrolled acceleration
- **Hill Assist:** provides propulsion assistance on uphill slopes
- Publishes `MotionRequest` (higher priority than manual) and `behavior/status`
- Uses IMU pitch + wheel velocity with hysteresis

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

### Autonomous Navigation / Nav2
- `golfcart_navigation` package
- `navigation_node` — bridges `cmd_vel`→`MotionRequest` (priority 0), `/set_goal` service→`navigate_to_pose` action, publishes `NavigationStatus`, pauses on manual override
- `georeference_node` — converts lat/lon→map-frame via `/set_goal_geo` service
- Nav2 stack (manual bringup): planner (NavFn), controller (Regulated Pure Pursuit), bt_navigator, velocity_smoother, global/local costmaps
- `golfcart_msgs` — `GoalPose`, `NavigationStatus`, `CourseMap`, `SetGoal.srv`

### Gazebo Simulation (gz-sim)
- `golfcart_gazebo` package
- Course world (`worlds/course.sdf`) with obstacles, green, tee, water hazard, slope ramp, steep zone
- Cart model embedded in SDF with `gz_ros2_control` (diff_drive_controller + joint_state_broadcaster) and gz-sim sensors (LiDAR, IMU, GPS, camera)
- `ros_gz_bridge` bridges `/scan`, `/imu`, `/gps`, `/camera/image`, `/clock`
- `cmd_vel_converter` — bridges nav `Twist`→controller `TwistStamped`
- `launch/sim.launch.py` — `ros2 launch golfcart_gazebo sim.launch.py [headless:=true]`
- `scripts/sim_check.sh` — headless sanity check (topics, sim time, cart movement)

## Not yet implemented (documented only)

See `docs/features/` for design docs.

- Follow Me
- HMI Display (TFT) — spec + mockups in `docs/hmi-spec.md` & `docs/hmi/`
- Route Recording + Replay
- Geofencing
- Speed Zones
- Voice Control
- Summon (HMI menu scaffolded; full flow pending)

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
