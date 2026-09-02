# Technology Stack and Architecture

## Core Technology

The project uses **ROS 2 as the robotics middleware**.

The target platform is:

* Raspberry Pi 5
* Ubuntu 26.04
* ROS 2 Lyrical

ROS 2 provides the communication and integration layer between sensors, perception, control, HMI, and autonomous functionality.

Do not build a custom message bus, event system, or inter-process communication framework unless there is a specific technical reason that ROS 2 cannot satisfy the requirement.

---

## Language Strategy

Use **C++ for hardware-facing and safety-critical functionality**.

Use **Python where it provides significant advantages for rapid development, perception, AI, experimentation, or tooling**.

### Prefer C++ for

* ODrive communication
* motor control
* encoder processing
* IMU integration
* LiDAR integration
* sensor state/health monitoring
* safety controller
* motion controller
* safety-critical state machines
* time-sensitive control loops

### Prefer Python for

* camera processing
* machine-learning inference
* early prototypes
* data analysis
* development tools
* diagnostics
* non-critical HMI functionality
* experiments and algorithms where Python significantly accelerates development

The language choice must not compromise the safety architecture.

Python code must never bypass the safety/motion layer to directly command motors.

---

# ROS 2 Architecture

The system should be structured as independent ROS 2 nodes with clearly defined responsibilities.

A typical architecture is:

```text
                         ROS 2
                           │
       ┌───────────────────┼───────────────────┐
       │                   │                   │
       ▼                   ▼                   ▼
    Sensors            Perception          Localization
       │                   │                   │
       └───────────────────┼───────────────────┘
                           ▼
                    Autonomous Logic
                           │
                           │ Motion Request
                           ▼
                 ┌───────────────────┐
                 │  Safety Controller │
                 │                   │
                 │  • safety checks  │
                 │  • limits         │
                 │  • fault handling │
                 │  • state machine  │
                 └─────────┬─────────┘
                           │
                           ▼
                  Motion Controller
                           │
                           ▼
                       ODrive
                      ↙      ↘
                  Motor L   Motor R
```

High-level features should produce **motion requests**, not direct motor commands.

For example:

```text
Follow Me
    ↓
MotionRequest
    ↓
Safety Controller
    ↓
Motion Controller
    ↓
ODrive
```

Never:

```text
Follow Me
    ↓
ODrive.set_velocity()
```

---

# Safety Boundary

ROS 2 is a **robotics middleware**, not a safety certification mechanism.

The safety-critical behavior must therefore be implemented explicitly in the application architecture.

The Safety Controller must be able to override any autonomous or manual motion request.

For example:

```text
Autonomous request: 0.8 m/s
             ↓
      Safety Controller
             ↓
       obstacle detected
             ↓
          STOP
```

Similarly, loss of a required sensor must be able to stop the trolley independently of the autonomous controller.

The safety controller must not depend on the correctness of a high-level autonomous algorithm.

---

# Motion Interface

High-level components should communicate desired motion using an abstract representation such as:

```text
linear_velocity
angular_velocity
```

The motion-control layer converts this into differential-drive commands:

```text
left_motor_velocity
right_motor_velocity
```

This keeps autonomous behavior independent of the specific motor controller.

The ODrive-specific implementation must remain isolated behind a hardware abstraction.

---

# Node Responsibilities

Where practical, maintain clear separation between:

### Hardware nodes

Examples:

```text
odrive_node
imu_node
lidar_node
gps_node
camera_node
force_sensor_node
joystick_node
```

### Processing nodes

Examples:

```text
sensor_fusion_node
obstacle_detection_node
person_tracking_node
```

### Behavior nodes

Examples:

```text
follow_me_node
hill_assist_node
hill_descent_node
rollback_protection_node
```

### Safety/control nodes

Examples:

```text
safety_controller
motion_controller
```

The exact node boundaries may evolve as the implementation develops. Do not create ROS 2 nodes purely for the sake of having many nodes; responsibilities should remain cohesive.

---

# Sensor Abstraction

Hardware drivers should expose standardized application-level data rather than leaking hardware-specific details into higher-level algorithms.

For example, Follow Me should consume:

```text
PersonTarget
```

rather than directly consuming raw LD19P measurements.

A possible target representation is:

```text
distance
lateral_offset
relative_velocity
confidence
timestamp
valid
```

The initial implementation may detect a person using LiDAR leg patterns.

The interface should nevertheless allow future implementations such as:

```text
LiDAR leg detection
Camera person detection
LiDAR + camera fusion
```

without requiring the Follow Me controller to be rewritten.

---

# ROS 2 Topics and Interfaces

Use ROS 2 topics for continuously changing sensor/state information.

Examples:

```text
/imu
/lidar
/wheel_odometry
/gps
/camera/image
/obstacles
/person_target
/safety_state
/battery_state
```

Use appropriate ROS 2 services/actions for discrete operations or long-running tasks where appropriate.

Avoid inventing custom communication mechanisms when a standard ROS 2 mechanism is suitable.

Prefer standard ROS 2 message types where they accurately represent the data.

Create custom message types when the application's semantics cannot be represented clearly by existing messages.

---

# Data Recording and Replay

Use **ROS 2 bags** to record sensor and system data during testing.

Important test sessions should be reproducible without the physical trolley whenever possible.

For example:

```text
Golf course
    ↓
ROS 2 bag
    ↓
Development PC
    ↓
Algorithm replay
    ↓
Evaluation
```

This is especially important for:

* Follow Me
* obstacle detection
* sensor fusion
* localization
* autonomous navigation

Algorithms should, where practical, be testable using recorded data.

---

# Visualization and Debugging

Use **RViz 2** for visualization of robotics data where useful.

The system should make it possible to visualize at least:

* LiDAR scans
* detected obstacles
* detected person
* target position
* trolley pose
* coordinate frames
* planned/requested motion
* safety state

Visualization must not be required for the trolley to operate safely.

---

# Navigation

Do not introduce the ROS 2 Navigation (Nav2) stack until autonomous navigation actually requires it.

Follow Me can initially be implemented independently:

```text
LiDAR / Camera
      ↓
Person Tracking
      ↓
Follow Me Controller
      ↓
Motion Request
```

If the project later requires autonomous navigation between locations, investigate Nav2 rather than implementing a custom navigation framework.

---

# Real-Time and Timing Requirements

ROS 2 callbacks, executors, and node scheduling must not be assumed to provide hard real-time guarantees.

Safety-critical control loops should be designed accordingly.

Avoid:

* blocking calls in control callbacks
* unbounded processing inside high-frequency callbacks
* dependence on arbitrary callback ordering
* reliance on UI/network activity for motor control
* uncontrolled queue growth

Explicitly monitor sensor and communication timestamps.

Stale sensor data must be treated as invalid when its age exceeds the relevant safety limit.

---

# Hardware Independence

The application architecture should avoid coupling autonomous algorithms directly to the ODrive API.

For example:

```text
Follow Me
     ↓
MotionRequest
     ↓
Motion Controller
     ↓
MotorController interface
     ↓
ODrive implementation
```

This allows the motor controller to be replaced or simulated without rewriting the autonomous algorithms.

The same principle applies to sensors.

---

# Simulation and Testing

Hardware-independent components should have simulated or mocked interfaces.

At minimum, the following should be testable without physical hardware:

* safety state machine
* motion limiting
* rollback detection
* hill-state detection
* Follow Me control logic
* sensor timeout handling
* obstacle handling
* fault handling

Hardware integration tests should be separated from pure software tests.

---

# Technology Decision Rules

When choosing a new technology or dependency:

1. Prefer existing ROS 2 functionality when appropriate.
2. Prefer established robotics libraries over custom implementations.
3. Keep safety-critical dependencies small.
4. Avoid introducing heavyweight frameworks for simple functionality.
5. Prefer well-maintained open-source libraries with Raspberry Pi / ARM64 support.
6. Verify compatibility with Ubuntu 26.04 and ROS 2 Lyrical.
7. Consider CPU, memory, latency, and power consumption.
8. Consider how the component behaves when communication fails.
9. Avoid dependencies that can prevent the safety controller from stopping the vehicle.
10. Document significant technology decisions in `docs/architecture.md`.
11. Execution plans in `plans/*`.

---

# Architecture Documentation

Detailed architectural decisions belong in:

```text
docs/architecture.md
```

That document should describe:

* ROS 2 node architecture
* topic/service/action interfaces
* data flow
* coordinate frames
* sensor fusion
* control loops
* safety boundaries
* hardware abstractions
* threading/executor model
* timing requirements

`AGENT.md` contains the rules and constraints that an autonomous coding agent must follow. `docs/architecture.md` contains the detailed implementation architecture.
