# golfcart_description

URDF/xacro model of the 3-wheeled golf push trolley, with sensor mounts
(lidar, camera, IMU, GPS)above the front wheel.

## Kinematics

- **Two rear wheels** — driven (ODrive), fixed, non-steering.
- **One front wheel** — caster-like, rotates on a vertical axis to steer.

## Sensor mounts

LiDAR, camera, IMU, GPS are mounted **above the front wheel** on a mast/
column rising from the front wheel area. Frames follow REP-103 and the architecture's
frame tree: `base_link -> {lidar_link, camera_link, imu_link, gps_link}`.



## Editing measurements

All dimensions are **xacro parameters** at the top of `urdf/golfcart.urdf.xacro`.
Edit them with the correct measurements later. Key ones:

| Parameter | Default | Meaning |
| --- | --- | --- |
| `chassis_length` | 0.9 m | Front-to-back |
| `chassis_width` | 0.6 m | Side-to-side |
| `chassis_z` | 0.35 m | Chassis height above ground |
| `rear_wheel_radius` | 0.15 m | Rear wheel radius |
| `rear_wheel_base` | 0.5 m | Rear track (distance between rear wheels) |
| `rear_axle_x` | −0..25 m | Rear axle position along X |
| `front_wheel_x` | 0.35 m | Front wheel position along X |
| `front_wheel_radius` | 0.12 m | Front wheel radius |
| `mast_z` | 0.6 m | Sensor platform height above ground |
| `lidar_offset_z` | 0.05 m | LiDAR above mast top |
| `camera_offset_z` | 0.02 m | Camera above mast top |
| `imu_offset_z` | 0.0 m | IMU at mast top |
| `gps_offset_z` | 0.10 m | GPS above mast top |

## Launch

```bash
ros2 launch golfcart_description display.launch.py
```

Publishes TF2 via `robot_state_publisher` so the frame tree is available for
localization, SLAM, and navigation.