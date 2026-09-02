# Golf Cart AI — Joystick Motor Control MVP

First working slice of the autonomous golf trolley: drive the motors with a
joystick (or keyboard), routed through the full safety + motion pipeline.

## Stack

- ROS 2 Lyrical / Ubuntu 26.04
- Raspberry Pi 5 (aarch64)
- C++ for safety/motion; Python for teleop

## Pipeline

```text
Joystick / Keyboard
        ↓  MotionRequest
Safety Controller
        ↓  SafeMotionCommand
Motion Controller
        ↓  MotorCommand
ODrive / Mock
```

## Packages

| Package | Purpose |
| --- | --- |
| `golfcart_msgs` | Custom messages (`MotionRequest`, `SafeMotionCommand`, `MotorCommand`, `MotorState`) |
| `golfcart_odrive` | `MotorController` interface, ODrive driver scaffold, mock implementation, `odrive_node` |
| `golfcart_control` | `motion_controller_node`, `safety_controller_node` |
| `golfcart_teleop` | `joystick_node`, `keyboard_teleop_node` |
| `golfcart_bringup` | Launch files |

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

## Tests

```bash
# Docker
./docker/build.sh test

# Native
colcon test
colcon test-result --verbose
```
