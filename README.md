# Golf Cart AI

Autonomous golf push trolley built on ROS 2. Drives the motors through a full
safety + motion pipeline, with incremental features: joystick/keyboard/web teleop,
battery monitoring, IMU, GPS, LiDAR + obstacle detection, auto-shutdown,
energy-saving mode (sleep when idle, IMU wake), hill/rollback behaviors, a URDF
model of the trolley, and a full gz-sim simulation for validating the navigation
stack without hardware.

## Stack

- ROS 2 Lyrical / Ubuntu 26.04
- Raspberry Pi 5 (aarch64)
- C++ for safety/motion/hardware; Python for teleop/perception/HMI

## Pipeline

```text
Joystick / Keyboard / Web / Behavior
        ↓  MotionRequest
Safety Controller
        ↓  SafeMotionCommand (Twist)
Motion Controller
        ↓  MotorCommand
ODrive / Mock
```

The Safety Controller is the central authority for movement. It gates all
`MotionRequest`s (applying limits, stopping on battery-critical, excessive-roll,
obstacle-in-zone, request-timeout) and outputs the approved `Twist`.

## Packages

| Package | Purpose |
| --- | --- |
| `golfcart_msgs` | Custom messages (`MotionRequest`, `MotorCommand`, `MotorState`, `BatteryState`, `ImuData`, `GpsFix`, `Obstacle`, `ObstacleState`) |
| `golfcart_odrive` | `MotorController` interface, ODrive driver(USB, JSON protocol), mock, serial port, `odrive_node`, `battery_node` |
| `golfcart_control` | `motion_controller_node` (differential drive, 50 Hz), `safety_controller_node` (state machine, 50 Hz) |
| `golfcart_teleop` | `joystick_node`, `arduino_joystick_node`, `keyboard_teleop_node`, `web_teleop_server` |
| `golfcart_imu` | `ImuSensor` interface, mock, scaffold driver, `imu_node` |
| `golfcart_gps` | `GpsSensor` interface, mock, scaffold driver, `gps_node` |
| `golfcart_lidar` | `LidarSensor` interface, mock, scaffold driver, `lidar_node`, `obstacle_detection_node` |
| `golfcart_power` | `auto_shutdown_node` (watchdog, idle shutdown, roll-away suppression), `energy_saver_node` (sleep when idle, IMU wake) |
| `golfcart_behavior` | `hill_rollback_node` (Hill Assist, Hill Descent Brake, Rollback Protection) |
| `golfcart_description` | URDF/xacro model of the 3-wheeled trolley + sensor mounts (lidar, camera, imu, gps) |
| `golfcart_localization` | `wheel_odometry_node`, `sensor_fusion_node`, `localization_quality_node` + robot_localization EKF |
| `golfcart_mapping` | slam_toolbox online async mapping (`mapping.launch.py`) |
| `golfcart_navigation` | `navigation_node`, `georeference_node`, Nav2 stack (planner, RPP controller, bt_navigator, costmaps) |
| `golfcart_gazebo` | gz-sim course world + cart model, `gz_ros2_control`, sensors, `cmd_vel_converter`, `sim.launch.py` |
| `golfcart_bringup` | Launch files (`joystick_control`, `keyboard_control`, `web_teleop`) |

## Features

See `FEATURES.md` for the full tracker. Implemented: joystick/keyboard/web teleop,
battery monitoring, ODrive driver (pending hardware validation), IMU, GPS, LiDAR +
obstacle detection, auto-shutdown, hill/rollback behaviors, URDF model, localization
(robot_localization EKF), course mapping (slam_toolbox), autonomous navigation (Nav2),
and a full gz-sim simulation. Planned: HMI display, route replay, geofencing, speed
zones, voice control, summon.

## Build

### Option A: Docker (recommended for development)

Build and test the workspace inside a ROS 2 Lyrical container — no ROS install
needed on the host.

```bash
./docker/build.sh          # build the image
./docker/build.sh test     # build image + colcon build & tests
./docker/build.sh shell    # open an interactive shell
```

> If your user is in the `docker` group but the group isn't active in the
> current session, run via `sg docker -c "./docker/build.sh test"`.

### Option B: Native

```bash
source /opt/ros/lyrical/setup.bash
cd <workspace>
colcon build
source install/setup.bash
```

## Run (mock, hardware-free)

```bash
ros2 launch golfcart_bringup keyboard_control.launch.py implementation:=mock
```

In another terminal, enable the safety controller, then drive:

```bash
ros2 service call /safety/enable std_srvs/srv/Trigger
# W/S forward/back, A/D turn, Space stop
```

## Run (real ODrive)

```bash
ros2 launch golfcart_bringup joystick_control.launch.py implementation:=odrive
```

> Note: the ODrive hardware driver is a scaffold. Implement the ODrive 3.6
> communication (USB/CAN) against the verified ODrive 0.5.6 API before use.



## Run (URDF model)

```bash
ros2 launch golfcart_description display.launch.py
```

Publishes TF2 via `robot_state_publisher` so the frame tree
(`base_link -> {lidar_link, camera_link, imu_link, gps_link}`)is available for
localization, SLAM, and navigation. Edit the xacro parameters in
`src/golfcart_description/urdf/golfcart.urdf.xacro` with your real measurements.



## Run (Gazebo simulation)

A full-robot simulation in **gz-sim** (Gazebo Sim) lets you validate the
navigation stack without physical hardware. It launches a course world
(obstacles, green, tee, water hazard, slope ramp, steep zone) with the cart
model, `gz_ros2_control` (diff-drive), and LiDAR/IMU/GPS/camera sensors.

```bash
# With GUI
ros2 launch golfcart_gazebo sim.launch.py

# Headless (CI / automated testing)
ros2 launch golfcart_gazebo sim.launch.py headless:=true

# Headless sanity check (topics, sim time, cart movement)
scripts/sim_check.sh
```

The simulation exposes the same ROS topics as the real cart: `/cmd_vel`,
`/odom`, `/scan`, `/imu`, `/gps`, `/camera/image`, `/joint_states`, `/clock`.
Drive it by publishing a `geometry_msgs/Twist` on `/cmd_vel` (the
`cmd_vel_converter` bridges it to the controller's `TwistStamped`).

See `src/golfcart_gazebo/README.md` and `docs/architecture.md` §26.1 for details.



## Deploy (Raspberry Pi)

Deployment uses a hybrid strategy (Option D): **code via git/rsync**, **maps via
rsync** (large binary data kept out of git). Services run as systemd units that
auto-start on boot and restart on crash.

```bash
# Build in Docker, rsync code (+ optional maps) to the Pi, install systemd
# units, and restart services.
./scripts/deploy.sh --pi pi@<host> [--map-dir ./maps] [--no-build]
```

On the Pi, the services are managed manually with:

```bash
./scripts/install_services.sh --start   # install + enable + start
./scripts/uninstall_services.sh         # stop + disable + remove
```

Services: `golfcart-core` (control pipeline), `golfcart-teleop` (web),
`golfcart-localization` (EKF), `golfcart-mapping` (SLAM), `golfcart-navigation`
(Nav2). See `systemd/` and `docs/architecture.md` §34.1 for details.



## Tests

```bash
# Docker
./docker/build.sh test

# Native
colcon test
colcon test-result --verbose
```