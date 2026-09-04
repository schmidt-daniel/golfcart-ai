# System Architecture

## 1. Purpose

This document defines the technical architecture of the autonomous golf trolley.

The architecture is designed around the following principles:

1. Safety has priority over autonomous functionality.
2. High-level features request motion; they do not directly control motors.
3. Hardware-specific implementations are isolated behind interfaces.
4. Sensor data is timestamped, validated, and monitored for freshness.
5. The system should be testable without physical hardware wherever practical.
6. ROS 2 provides the robotics middleware and communication layer.
7. C++ is preferred for hardware-facing and safety-critical components; Python is used where it provides significant advantages for perception, AI, experimentation, or tooling.

This document is the detailed architecture reference. `AGENT.md` contains the rules an autonomous coding agent must follow.

This document covers the whole-system architecture. Individual features are documented in separate files:

| Feature | File |
| --- | --- |
| Obstacle Detection and Stopping | [`features/obstacle-detection.md`](features/obstacle-detection.md) |
| Follow Me | [`features/follow-me.md`](features/follow-me.md) |
| Hill Assist | [`features/hill-assist.md`](features/hill-assist.md) |
| Push Assist (Pedelec-Style) | [`features/push-assist.md`](features/push-assist.md) |
| Hill Descent Brake | [`features/hill-descent-brake.md`](features/hill-descent-brake.md) |
| Rollback Protection | [`features/rollback-protection.md`](features/rollback-protection.md) |
| GPS and Localization | [`features/gps-localization.md`](features/gps-localization.md) |
| Navigation and Future Expansion | [`features/navigation.md`](features/navigation.md) |
| HMI Display | [`features/hmi-display.md`](features/hmi-display.md) · [spec](hmi-spec.md) |
| Web-Based Remote Teleop | [`features/web-teleop.md`](features/web-teleop.md) |
| Auto-Shutdown / Sleep When Idle | [`features/auto-shutdown.md`](features/auto-shutdown.md) |
| Energy-Saving Mode | [`features/energy-saving.md`](features/energy-saving.md) |
| Route Recording and Replay | [`features/route-replay.md`](features/route-replay.md) |
| Learning | [`features/learning.md`](features/learning.md) |
| Geofencing / Stay-on-Course | [`features/geofencing.md`](features/geofencing.md) |
| Speed Limiting by Zone | [`features/speed-zones.md`](features/speed-zones.md) |
| Voice Control | [`features/voice-control.md`](features/voice-control.md) |
| Summon | [`features/summon.md`](features/summon.md) |

---

# 2. Target Platform

## Hardware

- Raspberry Pi 5 — main onboard computer
- ODrive 3.6 — motor controller
- Two hoverboard-style DC motors with integrated encoders
- FHL-LD19P LiDAR
- Raspberry Pi HQ Camera
- IMU
- GPS
- Force sensors in the handle
- TFT display
- Game-controller joystick
- 36 V Li-Ion battery

## Software

- Ubuntu 26.04
- ROS 2 Lyrical
- C++ for safety-critical and hardware-facing components
- Python for perception, AI, experimentation, and tooling where appropriate
- RViz 2 for visualization
- ROS 2 bags for recording and replay

---

# 3. High-Level Architecture

```text
                         ┌─────────────────────────┐
                         │      Raspberry Pi 5      │
                         │       Ubuntu 26.04       │
                         │         ROS 2            │
                         └────────────┬────────────┘
                                      │
             ┌────────────────────────┼────────────────────────┐
             │                        │                        │
             ▼                        ▼                        ▼
       Sensor Layer             Perception Layer         HMI Layer
             │                        │                        │
       ┌─────┼─────┐             ┌────┴────┐                  │
       │     │     │             │         │                  │
     LiDAR  IMU   GPS          Person    Obstacle          Joystick
       │     │     │           Tracking   Detection           │
       │     │     │             │         │                  │
       └─────┼─────┴─────────────┴─────────┴──────────────────┘
             │
             ▼
      ┌──────────────────┐
      │ State Estimation │
      └────────┬─────────┘
               │
               ▼
      ┌──────────────────┐
      │ Behavior Layer   │
      │                  │
      │ Manual           │
      │ Hill Assist      │
      │ Hill Descent     │
      │ Rollback         │
      │ Follow Me        │
      └────────┬─────────┘
               │
               │ MotionRequest
               ▼
      ┌──────────────────┐
      │ Safety Controller│
      │                  │
      │ Fault handling   │
      │ Obstacle stop    │
      │ Speed limits     │
      │ Sensor timeouts  │
      │ Safety states    │
      └────────┬─────────┘
               │
               │ SafeMotionCommand
               ▼
      ┌──────────────────┐
      │ Motion Controller│
      │                  │
      │ Differential     │
      │ drive conversion │
      │ acceleration     │
      │ deceleration     │
      └────────┬─────────┘
               │
               ▼
          ODrive Driver
               │
          ┌────┴────┐
          ▼         ▼
      Left Motor  Right Motor
```

The exact ROS 2 node decomposition may evolve. The responsibility boundaries are more important than the exact number of processes.

---

# 4. Architectural Boundaries

The system is divided into five conceptual layers.

## 4.1 Hardware Layer

Responsible for communicating with physical devices.

Examples:

- ODrive
- LiDAR
- IMU
- GPS
- Camera
- Force sensors
- Joystick
- Display

Hardware drivers must expose application-level data and status rather than leaking unnecessary hardware-specific details upward.

---

## 4.2 Perception and State Estimation

Responsible for turning raw sensor data into useful information.

Examples:

- obstacle detection
- person detection
- person tracking
- inclination estimation
- wheel odometry
- sensor health
- localization

This layer should not directly command motors.

---

## 4.3 Behavior Layer

Responsible for deciding what the trolley wants to do.

Examples:

- Manual
- Hill Assist
- Hill Descent Brake
- Rollback Protection
- Follow Me

Behavior nodes generate `MotionRequest` messages.

They do not directly communicate with the ODrive.

---

## 4.4 Safety Layer

Responsible for determining whether requested movement is allowed.

The Safety Controller has authority over all motor movement.

It may:

- reduce speed
- limit acceleration
- override commands
- stop the trolley
- enter a fault state

No behavior node may bypass this layer.

---

## 4.5 Motion Layer

Responsible for converting an approved motion command into motor-specific commands.

The preferred abstraction is:

```text
linear_velocity
angular_velocity
```

The motion controller converts this into:

```text
left_motor_velocity
right_motor_velocity
```

The ODrive driver then translates the commands into the ODrive API/protocol.

---

# 5. ROS 2 Node Architecture

The initial node architecture should be approximately:

```text
Hardware Nodes
├── odrive_node
├── battery_node
├── imu_node
├── lidar_node
├── gps_node
├── camera_node
├── force_sensor_node
└── joystick_node

Processing Nodes
├── wheel_odometry_node
├── sensor_fusion_node
├── obstacle_detection_node
└── person_tracking_node

Behavior Nodes
├── manual_control_node
├── hill_assist_node
├── hill_descent_node
├── rollback_protection_node
└── follow_me_node

Safety / Control
├── safety_controller
└── motion_controller

HMI / Diagnostics
├── hmi_node
└── diagnostics_node
```

These are logical responsibilities rather than mandatory process boundaries.

If two components are tightly coupled and have simple communication, combining them into one node is acceptable.

Do not create ROS nodes merely to maximize modularity.

---

# 6. ROS 2 Data Flow

## Sensor Data

Sensor nodes publish continuously changing data.

Examples:

```text
/scan
/imu/data
/gps/fix
/camera/image_raw
/wheel/odometry
/handle/force
```

Each message must contain or be associated with:

- timestamp
- validity where applicable
- frame of reference where applicable

---

## Perception Data

Examples:

```text
/obstacles
/person_target
/person_tracking/status
```

Perception results should contain confidence/quality information when applicable.

---

## System State

Examples:

```text
/safety/state
/system/state
/battery/state
/diagnostics
```

---

## Motion

High-level behavior publishes:

```text
/motion/request
```

The Safety Controller produces:

```text
/motion/safe_command
```

The Motion Controller converts this into motor commands.

The ODrive node is the only component that should directly communicate with the motor controller.

---

# 7. Motion Request

High-level behavior should express desired motion independently of the hardware.

Conceptually:

```text
MotionRequest
    linear_velocity_mps
    angular_velocity_radps
    source
    priority
    timestamp
```

The exact ROS message definition should be established when implementation begins.

The request represents intent, not permission.

For example:

```text
Follow Me
    ↓
linear = 0.8 m/s
angular = 0.1 rad/s
    ↓
Safety Controller
    ↓
linear = 0.4 m/s
angular = 0.1 rad/s
```

The Safety Controller may modify or reject the request.

---

# 8. Safety Controller

The Safety Controller is the central authority for movement.

It receives:

- MotionRequest
- obstacle state
- IMU state
- encoder state
- ODrive state
- sensor health
- battery state
- operating mode
- emergency-stop state
- safety arm switch state (dedicated physical switch)

It outputs:

- allowed motion
- safety state
- fault information

## Safety priority

```text
Emergency / Fault
        ↓
Safety Stop
        ↓
Obstacle Stop
        ↓
Rollback / Descent Protection
        ↓
Motion Limits
        ↓
User Command
        ↓
Autonomous Behavior
```

A higher-priority condition always overrides lower-priority requests.

---

# 9. Safety States

The initial safety state machine should contain at least:

```text
SAFE_STOPPED
READY
MOVING
LIMITED
STOPPING
FAULT
```

The exact state model may evolve.

Example transitions:

```text
SAFE_STOPPED
      │
      │ valid enable
      ▼
    READY
      │
      │ permitted motion
      ▼
   MOVING
      │
      ├── safety condition ──► STOPPING
      │                           │
      │                           ▼
      │                     SAFE_STOPPED
      │
      └── fault ───────────────► FAULT
```

Unexpected state transitions must result in a safe state.

---

# 10. Fail-Safe Behavior

The system follows the principle:

> If the system cannot confidently establish that continued movement is safe, it must stop.

Examples:

- LiDAR timeout
- IMU timeout
- encoder failure
- ODrive communication loss
- invalid motor state
- application-level safety fault
- lost Follow Me target
- obstacle inside stopping zone
- invalid motion request
- critical battery condition

A missing value must never implicitly mean "safe."

---

# 11. Motor Control

The motor-control pipeline is:

```text
Behavior
    ↓
MotionRequest
    ↓
SafetyController
    ↓
SafeMotionCommand
    ↓
MotionController
    ↓
Differential Drive
    ↓
ODrive Driver
    ↓
ODrive
    ↓
Motors
```

No other component may bypass this pipeline.

## Differential Drive

For a trolley with wheel separation `L`, the desired left and right wheel velocities are derived from:

```text
v_left  = v - (ω * L / 2)
v_right = v + (ω * L / 2)
```

where:

- `v` = linear velocity in m/s
- `ω` = angular velocity in rad/s
- `L` = distance between the drive wheels in meters

The exact sign convention must be documented and tested against the physical trolley.

---

# 12. ODrive Integration

The ODrive driver is responsible for:

- connection management
- command transmission
- encoder feedback
- motor state
- fault/status monitoring
- communication timeout detection

The motors and encoders are already tuned and configured on the ODrive.

Do not modify the existing motor/encoder configuration unless explicitly required.

ODrive-specific APIs must not leak into high-level behavior code.

The ODrive driver should expose a generic motor-controller interface.

Conceptually:

```text
MotorController
├── command_velocity()
├── get_velocity()
├── get_position()
├── get_state()
├── get_fault()
└── stop()
```

The exact interface should be adapted to the capabilities and communication mechanism used by the ODrive 3.6.

---

# 13. Encoder Feedback

Encoder feedback is the primary source for wheel/motor velocity.

It is used for:

- wheel odometry
- direction detection
- rollback detection
- motion monitoring
- detecting unexpected motor behavior

The system should compare commanded and measured motion where appropriate.

Unexpected behavior such as:

```text
commanded forward
measured backward
```

must be treated as a potential fault.

---

# 14. IMU

The IMU provides:

- orientation
- acceleration
- angular velocity

Primary uses:

- slope estimation
- Hill Assist
- Hill Descent Brake
- motion state estimation

The software must distinguish static inclination from dynamic acceleration where possible.

A raw acceleration spike must not automatically be interpreted as a change in slope.

Sensor mounting orientation and coordinate transformation must be explicitly documented.

---

# 15. LiDAR

The FHL-LD19P is used for:

- obstacle detection
- environment scanning
- Follow Me
- future navigation functions

Raw LiDAR data should be processed into application-level information.

Examples:

```text
Obstacle
    position
    distance
    relative_velocity
    confidence
    timestamp
```

The obstacle detector must account for:

- invalid readings
- missing scans
- noisy measurements
- occlusion
- sensor timeout

LiDAR must not be treated as infallible.

---

# 16. Force Sensors

Force sensors in the handle provide information about user interaction.

Possible uses:

- pushing intent
- pulling intent
- braking intent
- user presence/contact
- manual override

Raw sensor values should be filtered.

Thresholds should include hysteresis where necessary.

The system should distinguish between:

```text
no interaction
intentional push
intentional pull
braking
sensor fault
```

The exact interpretation should be established experimentally.

---

# 17. Coordinate Frames

The system must use a consistent coordinate-frame convention.

Recommended trolley frame:

```text
             +X
              ↑
              │
           forward
              │
              │
      +Y ←────┼────→ -Y
              │
              │
```

The exact convention may be changed if required by the ROS ecosystem or hardware.

Once selected, it must be used consistently.

Sensor-specific coordinate systems must be transformed at the sensor interface boundary.

ROS TF2 should be used for coordinate-frame transformations.

At minimum, the system is expected to eventually contain frames similar to:

```text
map
 └── odom
      └── base_link
           ├── lidar_link
           ├── camera_link
           ├── imu_link
           └── gps_link
```

The exact frame tree will be finalized during implementation.

---

# 18. Time and Sensor Freshness

All sensor data must be timestamped.

The system must monitor data age.

Conceptually:

```text
now - sensor_timestamp <= allowed_age
```

If data exceeds its allowed age, it is stale.

Different sensors may have different freshness requirements.

Examples:

- IMU: high-frequency and low-latency
- LiDAR: high-frequency obstacle information
- GPS: relatively low-frequency
- camera: dependent on perception algorithm

Stale safety-relevant data must not be treated as current.

---

# 19. Timing and Control Loops

Control loops must have explicit update rates.

Do not allow UI activity, network communication, logging, or camera processing to determine motor-control timing.

Avoid blocking operations in control callbacks.

Long-running or computationally expensive operations should be isolated from safety-critical loops.

The exact frequencies should be established during implementation and documented here.

Initial candidates:

```text
Safety monitoring:     50–100 Hz
Motor command/control: 50–100 Hz
IMU processing:        100+ Hz where supported
LiDAR processing:      sensor rate
Follow Me:             10–30 Hz
Camera perception:     10–30 Hz
GPS:                   sensor rate
HMI:                   10–30 Hz
```

These are starting points, not validated safety requirements.

---

# 20. ROS 2 QoS

QoS settings should reflect the semantics of each data stream.

Examples:

High-frequency sensor data:

```text
best effort
small queue
volatile
```

Critical state information:

```text
reliable
appropriate queue depth
```

The exact QoS configuration must be documented alongside each interface.

Do not use reliable delivery indiscriminately for high-rate sensor streams if stale data is less useful than dropped data.

---

# 21. Threading and Executors

The ROS 2 executor model must not allow a slow callback to block safety-critical processing.

Potentially blocking or computationally expensive work includes:

- camera processing
- AI inference
- file I/O
- network communication
- GPS processing

These should be isolated from the safety-critical execution path.

The executor/threading architecture should be documented once actual implementation requirements are known.

---

# 22. HMI

The HMI consists of:

- joystick (navigation + selection)
- TFT display (3.5" ILI9488, SPI, no touch)
- dedicated physical safety arm switch

The HMI should provide:

- operating-mode selection
- manual control
- configuration
- system status
- battery state
- warnings
- fault information
- sensor data display (GPS, IMU, LiDAR, battery)
- feature enable/disable
- recording control (ROS 2 bags)
- manual speed setting (like a traditional electric push cart)
- forward travel distance setting

## Display

- **3.5" ILI9488 SPI TFT** (480×320), driven via `luma.lcd` (Python).
- No touch (unsuitable with golf gloves).

## Input

- **Joystick** for menu navigation:
  - short press = select / confirm
  - double press = back / cancel
  - long press = reserved
- **Dedicated physical safety arm switch** for arm/disarm (not the joystick
  button, which is reserved for HMI navigation).

## Node

A `hmi_node` (Python, non-critical) renders the menu on the TFT and translates
joystick input into menu navigation and feature commands. It calls per-feature
enable/disable services and publishes speed/distance commands.

HMI commands are requests only.

The HMI must never bypass the Safety Controller.

A fault must not be cleared in a way that causes unexpected motor activation.

---

# 23. Configuration

Configuration should be centralized.

Examples:

```text
MAX_LINEAR_VELOCITY_MPS
MAX_ANGULAR_VELOCITY_RADPS
MAX_ACCELERATION_MPS2
MAX_DECELERATION_MPS2

HILL_ASSIST_THRESHOLD_RAD
HILL_DESCENT_THRESHOLD_RAD
ROLLBACK_THRESHOLD_MPS

FOLLOW_ME_DISTANCE_M
FOLLOW_ME_MAX_VELOCITY_MPS
FOLLOW_ME_MIN_CONFIDENCE

LIDAR_TIMEOUT_S
IMU_TIMEOUT_S
ODRIVE_TIMEOUT_S

BATTERY_LOW_V
BATTERY_CRITICAL_V
```

Every parameter must have:

- name
- unit
- default
- valid range
- description
- safety implications where applicable

---

# 24. Diagnostics

Diagnostics should expose:

- node health
- sensor status
- sensor age
- ODrive status
- motor state
- battery state
- current operating mode
- safety state
- active faults

A diagnostic failure must not silently result in continued operation if the affected component is safety-critical.

---

# 25. Logging

Log important events including:

- startup/shutdown
- mode changes
- safety-state changes
- faults
- ODrive errors
- sensor timeouts
- obstacle detections
- Follow Me target changes
- rollback events
- Hill Assist activation
- Hill Descent Brake activation
- battery warnings

High-frequency data should not be logged individually at unlimited rates.

Use aggregation or throttling where appropriate.

---

# 26. Simulation and Testing

Software should be structured so that hardware can be replaced by mocks or simulators.

Important components to test without physical hardware:

- Safety Controller
- motion limits
- state machines
- rollback detection
- hill-state detection
- obstacle handling
- Follow Me controller
- sensor timeout handling
- fault recovery

Hardware integration tests should be separate from pure unit tests.

## 26.1 Gazebo Simulation (gz-sim)

A full-robot simulation is provided by the `golfcart_gazebo` package using
**gz-sim** (Gazebo Sim, the Lyrical successor to classic Gazebo). It is used to
validate the navigation stack (Phase 7) without physical hardware.

**Launch:**
```bash
ros2 launch golfcart_gazebo sim.launch.py            # with GUI
ros2 launch golfcart_gazebo sim.launch.py headless:=true   # CI / headless
scripts/sim_check.sh                                  # headless sanity check
```

**Architecture:**
- The cart model is embedded directly in the course world SDF
  (`worlds/course.sdf`), not spawned from URDF, because gz-sim does not resolve
  `$(find <pkg>)` inside plugin `<parameters>` strings.
- **Control:** `gz_ros2_control` (`GazeboSimROS2ControlPlugin`) drives the two
  rear wheels. `diff_drive_controller` computes odometry + `odom→base_link` TF;
  `joint_state_broadcaster` publishes `/joint_states`.
- **Sensors:** gz-sim system plugins (`Sensors`, `Imu`, `NavSat`) publish LiDAR,
  IMU, GPS, and camera on gz transport; `ros_gz_bridge` forwards them to ROS:
  - `/scan` (LiDAR, `sensor_msgs/LaserScan`)
  - `/imu` (`sensor_msgs/Imu`)
  - `/gps` (`gps_msgs/GPSFix` ↔ `gz.msgs.NavSat`)
  - `/camera/image` (`sensor_msgs/Image`)
  - `/clock` (sim time)
- **cmd_vel:** the Lyrical `diff_drive_controller` expects
  `geometry_msgs/TwistStamped` on `/cmd_vel`, but the nav stack publishes
  `geometry_msgs/Twist`. `cmd_vel_converter` bridges `/cmd_vel` (Twist) →
  `/cmd_vel_stamped` (TwistStamped).
- **Course elements** (for Phase 7 verification): obstacles (trees), green,
  tee, water hazard (forbidden zone), slope ramp (roll/pitch slope costmap),
  steep zone (high slope cost / forbidden stop).

**Key gz-sim notes (Lyrical):**
- `GZ_SIM_SYSTEM_PLUGIN_PATH` must include `/opt/ros/<distro>/lib` so gz-sim
  finds `libgz_ros2_control-system.so`.
- The world must register `gz-sim-physics-system`, `gz-sim-sensors-system`,
  `gz-sim-imu-system`, and `gz-sim-navsat-system` plugins.
- `use_sim_time: true` must be set on the controller_manager and controllers so
  they use the sim clock (bridged from `/clock`).

---

# 27. ROS 2 Bag Recording

During real-world tests, record relevant ROS 2 topics.

At minimum, useful recordings include:

```text
/imu/data
/scan
/wheel/odometry
/gps/fix
/person_target
/obstacles
/motion/request
/motion/safe_command
/safety/state
```

Recorded data should be usable for offline analysis and algorithm development.

---

# 28. Visualization

RViz 2 should be used where practical to visualize:

- LiDAR scans
- obstacles
- person target
- trolley pose
- coordinate frames
- odometry
- motion commands
- safety state

Visualization must never be required for safe operation.

---

# 29. Hardware Abstraction

High-level code must not depend directly on hardware APIs.

Preferred pattern:

```text
Application
     ↓
Application Interface
     ↓
Hardware Interface
     ↓
Hardware Driver
     ↓
Physical Device
```

Examples:

```text
MotorController
IMUSensor
LidarSensor
GPSSensor
ForceSensor
Camera
```

This makes unit testing and hardware replacement easier.

---

# 30. Failure and Recovery

Every hardware integration should explicitly define:

- startup behavior
- connection loss
- timeout behavior
- invalid data behavior
- recovery behavior
- shutdown behavior

Automatic recovery must be carefully considered for motor-related components.

Reconnection must never automatically cause an unexpected motor start.

After a safety-critical fault, returning to an operational state should generally require an explicit and deliberate user action.

---

# 31. Development Sequence

Recommended implementation order:

```text
1.  ODrive communication
2.  Motor commands
3.  Encoder feedback
4.  Central motion controller
5.  Safety controller
6.  Emergency / safe stop behavior
7.  IMU
8.  Basic state estimation
9.  Rollback Protection
10. Hill Descent Brake
11. Hill Assist
12. LiDAR
13. Obstacle detection
14. Follow Me
15. Camera perception
16. GPS / localization
17. Mapping
18. Autonomous navigation
```

Each stage should be validated before adding the next layer of complexity.

---

# 32. Design Principles

When implementing new functionality:

### Prefer explicit state over implicit behavior

Use state machines for complex behavior.

### Prefer interfaces over hardware coupling

High-level algorithms should not know whether data comes from a particular sensor model.

### Prefer stopping over guessing

If safety-relevant information is unavailable or contradictory, stop.

### Prefer one source of authority

The Safety Controller is the authority for whether motion is permitted.

### Prefer measured behavior over assumptions

Use encoder feedback, sensor timestamps, and diagnostics to verify that the physical system behaves as commanded.

### Prefer incremental integration

Do not integrate multiple new hardware components and autonomous behaviors simultaneously.

---

# 33. External References

## ODrive

ODrive 0.5.6 documentation:

https://docs.odriverobotics.com/v/0.5.6/getting-started.html

The actual ODrive firmware/API version used by the project must be verified before implementing or modifying the driver.

Do not assume that APIs from newer ODrive versions are compatible with ODrive 3.6 / firmware 0.5.6.

---

# 34. Open Architectural Decisions

The following decisions should be finalized during implementation:

- enable / arm sequence (how the trolley is armed before motion)
- sensor mounting positions
- emergency-stop implementation
- exact obstacle stopping model
- Follow Me target-tracking algorithm
- IMU sensor-fusion algorithm
- localization approach
- simulation environment

Architectural decisions should be recorded here as the implementation matures rather than being left implicit in source code.

---

# 34.1 Deployment (Option D, Hybrid)

Deployment uses a **hybrid** strategy (see `scripts/deploy.sh`):

- **Code via git** — versioned, easy rollback. Deployed to the Pi with `rsync`
  (excluding build/install/log artifacts and the large `core` dump).
- **Maps/config via rsync** — per-course maps are large binary data kept out of
  git to avoid repo bloat; pushed when updated via `--map-dir`.

**Services** run as systemd units (see `systemd/`), auto-start on boot and
restart on crash:

| Service | Launch file | Purpose |
| --- | --- | --- |
| `golfcart-core.service` | `golfcart_bringup/core.launch.py` | Always-on control pipeline (odrive, motion, safety, battery, IMU, GPS, LiDAR, obstacle, hill/rollback, auto-shutdown) |
| `golfcart-teleop.service` | `golfcart_bringup/web_server.launch.py` | Web teleop (rosbridge + HTTP server) |
| `golfcart-localization.service` | `golfcart_localization/fusion.launch.py` | Sensor fusion + EKF |
| `golfcart-mapping.service` | `golfcart_mapping/mapping.launch.py` | slam_toolbox course mapping |
| `golfcart-navigation.service` | `golfcart_navigation/navigation.launch.py` | Nav2 autonomous navigation |

**Deploy:**
```bash
./scripts/deploy.sh --pi pi@<host> [--map-dir ./maps] [--no-build]
```

**On the Pi (manual):**
```bash
./scripts/install_services.sh --start   # install + enable + start
./scripts/uninstall_services.sh         # stop + disable + remove
```

---

# 34.2 Off-Board Map Building

Course maps are usually built **on-board** by `slam_toolbox` as the cart drives.
For large courses or to refine a map on a more powerful workstation, the cart
can instead **record** a bag that is replayed **off-board** to build the map.

**Workflow:**
```bash
# 1. On the cart: record the SLAM topics while driving the course
./scripts/record_bag.sh -o hole5          # -> rosbag2/hole5

# 2. Copy the bag to a workstation
rsync -avz pi@cart:~/golfcart-ai/rosbag2/hole5 ./

# 3. Replay it off-board through slam_toolbox to build the map
./scripts/build_map_offline.sh rosbag2/hole5   # -> hole5_map.{pgm,yaml}

# 4. Push the map back to the cart (maps are kept out of git)
rsync -avz hole5_map.* pi@cart:~/golfcart-ai/maps/hole5/
```

**Components:**
- `scripts/record_bag.sh` — records `/scan`, `/tf`, `/tf_static`, `/odom`
  (optionally IMU/GPS with `--all`) on the cart.
- `golfcart_mapping/launch/offline_mapping.launch.py` + 
  `config/mapper_params_offline.yaml` — slam_toolbox in offline mode
  (`use_sim_time: true`, consumes the bag `/clock`).
- `scripts/build_map_offline.sh` — `ros2 bag play --clock` + slam_toolbox +
  `map_saver_cli -t /map -f <prefix>` to save the occupancy grid.

## 34.2.1 Per-Hole Extraction (polygon)

A full-course recording can be split into **per-hole** bags by drawing a polygon
around each hole and keeping only the data captured while the cart was inside it.
The cart's position comes from `/gps/fix` (`golfcart_msgs/GpsFix`).

**Workflow:**
```bash
# 1. Draw a polygon around the hole (web tool, or any GIS/geojson.io)
#    -> tools/hole_polygon_drawer.html  (exports lat/lon corners or GeoJSON)

# 2. Extract only the data inside the polygon into a new bag
python3 scripts/extract_bag_polygon.py rosbag2/hole5 \
  --polygon "lat1,lon1 lat2,lon2 ..." -o rosbag2/hole5_holeA
#   or: --geojson holeA.geojson

# 3. Build a per-hole map from the extracted bag
./scripts/build_map_offline.sh rosbag2/hole5_holeA
```

**Components:**
- `tools/hole_polygon_drawer.html` — standalone web page (Leaflet) to draw a
  polygon and export lat/lon corners or GeoJSON.
- `scripts/extract_bag_polygon.py` — reads the bag, tracks the cart's GPS
  position, and writes only messages whose timestamp falls inside the polygon
  (point-in-polygon on `/gps/fix`), with optional `--pad` seconds around each
  entry/exit and `--keep-topics` filtering.

---

# 35. Permanent Decisions

The following decisions are permanent and should not be changed without a
strong, documented requirement. They were originally made for the joystick-control
MVP and are now fixed.

## ODrive communication interface

- **Decision:** USB (serial).
- **Rationale:** Simplest to get working; appears as `/dev/ttyUSB0`; the ODrive
  SDK works over USB with minimal setup. CAN may be added later for lower
  latency/robustness in production.
- **Status:** ODrive driver is a scaffold pending implementation against the
  verified ODrive 0.5.6 API.

## ROS 2 QoS

- **Decision:** Use `SensorDataQoS` (best-effort, small queue) for all
  high-frequency streams (`/motion/request`, `/motion/safe_command`,
  `/motor/command`, `/motor/state`). Use services for discrete operations
  (`safety/enable`, `safety/stop`, `safety/fault`).
- **Rationale:** Stale high-rate data is worse than dropped data; services are
  appropriate for discrete, acknowledged operations.

## Control-loop frequencies

- **Decision:**
  - Safety monitoring: 50 Hz
  - Motion command: 50 Hz
  - Motor state feedback: 20 Hz
  - Joystick: event-driven (on `/joy`)
- **Rationale:** The safety loop runs at or above the motion-command rate so it
  can react quickly. Rates are configurable via parameters.

## Coordinate-frame convention

- **Decision:** Adopt REP-103 (ROS standard): `X` forward, `Y` left, `Z` up,
  right-hand rule.
- **Rationale:** Matches TF2, RViz, and standard drivers. Not yet exercised by
  the MVP (no TF2), but locked down to avoid rework.

## Executor / threading model

- **Decision:** Single-threaded executor per node.
- **Rationale:** Each node is simple and independent; single-threaded is the
  most predictable model and avoids concurrency bugs. Revisit only if a node
  must do heavy work (e.g., camera processing) alongside fast control.

## Message definitions

- **Decision:** Keep the custom `MotionRequest` (with `source`/`priority`) for
  the *request*; the Safety Controller outputs the standard `geometry_msgs/Twist`
  for the *approved* command.
- **Rationale:** `MotionRequest` carries safety-relevant metadata; `Twist` is
  the ROS standard for velocity commands and interoperates with standard tools
  and Nav2.

## Battery monitoring

- **Decision:** Use an INA219 over I2C, read by a `battery_node` publishing
  `BatteryState` on `/battery/state`. The Safety Controller stops motion when
  the battery is critical.
- **Rationale:** Battery state is safety-relevant (a dead battery mid-round is
  a failure mode). The INA219 is cheap (~$5) and easy to integrate.
- **Voltage divider required:** The INA219 has a **max bus voltage of 26 V**,
  but the 36 V Li-Ion battery reaches ~42 V when fully charged. A **voltage
  divider** (e.g. R1 = R2 = 100 kΩ, divide by 2) must be placed on the VBUS
  sense pin to keep it ≤ 26 V. The measured voltage is then **scaled by the
  divider ratio** (×2) in `battery_node`. The current measurement (via the
  shunt) is unaffected by the divider.
- **Status:** `battery_node` scaffold reads the INA219 registers; the I2C read
  via `/dev/i2c-N` is pending implementation.

## HMI

- **Decision:** 3.5" ILI9488 SPI TFT (480×320), no touch, driven via `luma.lcd`
  (Python). Joystick for menu navigation (short = select, double = back, long =
  reserved). A dedicated physical safety arm switch for arm/disarm.
- **Rationale:** SPI is the standard, cheap interface for a 3.5" status display;
  no touch suits golf gloves; a dedicated safety switch is safer and frees the
  joystick button for HMI navigation.
- **Status:** design only; `hmi_node` not yet implemented.
