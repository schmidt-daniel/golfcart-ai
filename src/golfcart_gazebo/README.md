# golfcart_gazebo

Gazebo (gz-sim) simulation of the golf cart on a course world. Used to validate
the navigation stack (Phase 7) without physical hardware.

## Launch

```bash
# With GUI
ros2 launch golfcart_gazebo sim.launch.py

# Headless (CI / automated testing)
ros2 launch golfcart_gazebo sim.launch.py headless:=true

# Headless sanity check (topics, sim time, cart movement)
scripts/sim_check.sh
```

## What it provides

- **Course world** (`worlds/course.sdf`): flat fairway, obstacles (trees),
  green, tee, water hazard (forbidden zone), slope ramp (roll/pitch slope
  costmap), steep zone (high slope cost / forbidden stop).
- **Cart model** embedded in the world SDF with:
  - `gz_ros2_control` (`GazeboSimROS2ControlPlugin`) driving the two rear wheels
  - `diff_drive_controller` → odometry + `odom→base_link` TF
  - `joint_state_broadcaster` → `/joint_states`
  - gz-sim sensors: LiDAR, IMU, GPS, camera
- **`ros_gz_bridge`** forwards sensor topics to ROS:
  - `/scan` (LiDAR), `/imu`, `/gps` (`gps_msgs/GPSFix`), `/camera/image`, `/clock`
- **`cmd_vel_converter`** bridges the nav stack's `geometry_msgs/Twist` on
  `/cmd_vel` to the controller's `geometry_msgs/TwistStamped` on
  `/cmd_vel_stamped` (Lyrical's diff_drive_controller expects TwistStamped).

## Topics

| Topic | Type | Source |
| --- | --- | --- |
| `/cmd_vel` | `geometry_msgs/Twist` | nav stack input (converted) |
| `/cmd_vel_stamped` | `geometry_msgs/TwistStamped` | to diff_drive_controller |
| `/odom` | `nav_msgs/Odometry` | diff_drive_controller |
| `/scan` | `sensor_msgs/LaserScan` | gz-sim LiDAR |
| `/imu` | `sensor_msgs/Imu` | gz-sim IMU |
| `/gps` | `gps_msgs/GPSFix` | gz-sim NavSat |
| `/camera/image` | `sensor_msgs/Image` | gz-sim camera |
| `/joint_states` | `sensor_msgs/JointState` | joint_state_broadcaster |
| `/clock` | `rosgraph_msgs/Clock` | sim time |

## Notes (Lyrical / gz-sim)

- `GZ_SIM_SYSTEM_PLUGIN_PATH` must include `/opt/ros/<distro>/lib` so gz-sim
  finds `libgz_ros2_control-system.so` (set in the launch file).
- The world registers `gz-sim-physics-system`, `gz-sim-sensors-system`,
  `gz-sim-imu-system`, and `gz-sim-navsat-system` plugins.
- `use_sim_time: true` is set on the controller_manager and controllers so they
  use the sim clock (bridged from `/clock`).
- gz-sim does not resolve `$(find <pkg>)` inside plugin `<parameters>` strings;
  the launch file substitutes the installed package path into a generated world
  copy (`course_generated.sdf`).