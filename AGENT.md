# Autonomous Golf Trolley

This project turns a traditional push golf trolley into an electrically assisted, semi-autonomous golf trolley by adding motors, sensors, a motor controller, and onboard software.

The system is built around a Raspberry Pi 5 and must be designed with **safe failure behavior as the highest priority**. The trolley operates around people, obstacles, slopes, and potentially uneven terrain.

## Project Goals

The software shall provide:

1. Manual motorized assistance
2. Hill Assist
3. Hill Descent Brake
4. Rollback Protection
5. Follow Me
6. Environmental and obstacle detection
7. A local HMI for configuration and control

The system should be developed incrementally. Basic motor control and safety functions must be reliable before autonomous functionality such as Follow Me is implemented.

---

# System Architecture

The Raspberry Pi 5 is the main computing platform and runs the application software.

The high-level architecture is:

```text
                    ┌─────────────────────┐
                    │     Raspberry Pi 5  │
                    │                     │
 Sensors ──────────►│ Sensor Processing   │
                    │ State Estimation    │
                    │ Safety Logic        │
                    │ Motion Controller   │
                    │ Autonomous Features │
                    │ HMI                 │
                    └─────────┬───────────┘
                              │
                              │ Motor commands
                              ▼
                    ┌─────────────────────┐
                    │    ODrive 3.6       │
                    │                     │
                    │ Motor control       │
                    │ Encoder feedback    │
                    └─────────┬───────────┘
                              │
                    ┌─────────┴─────────┐
                    ▼                   ▼
              Left DC Motor       Right DC Motor
              + encoder            + encoder
```

The Raspberry Pi is responsible for **high-level control and decision making**.

The ODrive is responsible for **low-level motor control and encoder-based motor regulation**.

Do not duplicate low-level motor-control functionality on the Raspberry Pi if it is already correctly handled by the ODrive.

---

# Hardware

## Main Compute Platform

### Raspberry Pi 5

The Raspberry Pi 5 is the main onboard computer.

Responsibilities include:

* sensor acquisition
* sensor processing
* state estimation
* safety monitoring
* motion control
* autonomous behavior
* HMI
* logging and diagnostics

The Raspberry Pi must not be treated as inherently safety-certified hardware.

Any software failure, communication loss, process crash, or unexpected state must result in a safe motor behavior.

---

## Motor Controller

### ODrive 3.6

The ODrive controls the two drive motors.

Responsibilities:

* motor commutation
* closed-loop motor control
* encoder processing
* velocity/position control
* motor braking where supported

The motors and encoders have already been tuned and configured on the ODrive.

**Do not change the existing motor/encoder configuration unless explicitly required.**

Before modifying ODrive configuration, understand the consequences for:

* motor direction
* encoder direction
* velocity feedback
* braking
* maximum current
* maximum velocity
* acceleration/deceleration
* fault behavior

---

## Motors

Two hoverboard-style DC motors with integrated encoders are used.

Each motor provides encoder feedback to the ODrive.

The system should normally control the trolley using differential drive:

```text
left_motor_velocity
right_motor_velocity
```

Higher-level motion commands should preferably be expressed as:

```text
linear_velocity
angular_velocity
```

and converted into left/right motor commands by the motion-control layer.

---

## LiDAR

### FHL-LD19P

The LiDAR is used for:

* obstacle detection
* environmental scanning
* distance measurement
* Follow Me
* potentially terrain/environment analysis

LiDAR measurements must be treated as sensor data rather than absolute truth.

The software must account for:

* invalid measurements
* missing measurements
* noisy measurements
* occlusion
* unexpected objects
* sensor communication failure

---

## Camera

### Raspberry Pi HQ Camera

The camera is used for image-based detection tasks.

Potential applications include:

* person detection
* object classification
* environmental understanding
* future autonomous functions

Camera-based detection must not be the sole safety mechanism for stopping the vehicle.

---

## IMU

The IMU provides:

* orientation
* inclination
* acceleration
* potentially angular velocity

The IMU is used for:

* slope detection
* Hill Assist
* Hill Descent Brake
* Rollback Protection
* motion/state estimation

Orientation estimates should be filtered appropriately and must distinguish between actual inclination and transient acceleration where necessary.

---

## GPS

GPS is available for:

* localization
* mapping
* future autonomous navigation

GPS accuracy is not sufficient to be treated as the sole source of localization for precise obstacle avoidance.

GPS must therefore not be used as a safety mechanism for detecting nearby obstacles or preventing collisions.

---

## Force Sensors

Force sensors are integrated into the trolley handle.

They are intended to detect user interaction such as:

* pushing
* pulling
* braking
* gripping the handle
* potentially user intent

Force-sensor input should be filtered and interpreted using thresholds/hysteresis rather than reacting directly to individual raw measurements.

---

## HMI

The HMI consists of:

* small TFT display
* game-controller joystick

The HMI is used for:

* selecting operating modes
* configuring parameters
* displaying system status
* displaying warnings/faults
* manual control

Safety-critical states must always take precedence over HMI commands.

---

# Operating Modes

The trolley should have an explicit operating state/mode.

At minimum:

```text
OFF
MANUAL
HILL_ASSIST
HILL_DESCENT_BRAKE
ROLLBACK_PROTECTION
FOLLOW_ME
FAULT
```

Only one primary operating mode should control the motors at a time.

Safety mechanisms such as emergency stop, fault handling, obstacle stopping, or loss of required sensor data may override the currently selected operating mode.

---

# Functional Features

## Hill Assist

On inclines, additional motor assistance can be activated.

The feature should initially support **manual activation**.

Automatic activation may be added later.

Inputs:

* IMU inclination
* motor velocity
* potentially force-sensor input
* user-selected assistance level

The system should distinguish between:

* driving uphill
* standing on a slope
* rolling downhill
* accelerating/decelerating on level ground

Do not activate Hill Assist solely because the IMU reports an inclination.

Automatic activation should use appropriate thresholds and hysteresis to prevent rapid mode switching around the activation threshold.

---

## Hill Descent Brake

When the trolley is moving downhill, the system automatically applies braking assistance to prevent uncontrolled acceleration.

Primary input:

* IMU inclination

Additional useful inputs:

* wheel/motor encoder velocity
* direction of travel

The control system should distinguish between:

```text
stationary on slope
moving uphill
moving downhill
```

The braking behavior should be smooth and should avoid abrupt torque changes unless an immediate safety stop is required.

A failure or loss of required sensor information must result in a safe state.

---

## Rollback Protection

When Rollback Protection is enabled, the trolley is allowed to move only in the intended forward direction.

If backward movement is detected:

1. Detect the unexpected direction using encoder feedback.
2. Confirm the movement is above the configured threshold.
3. Apply motor braking.
4. Prevent continued backward movement.
5. Report the event through the HMI/logging system.

Encoder velocity should be preferred over indirect inference where possible.

Do not rely exclusively on the IMU to detect rollback.

The system must distinguish between sensor noise and actual backward movement.

---

## Follow Me

The trolley follows a person walking ahead of it at a configurable distance.

### Intended behavior

1. Detect a candidate person.
2. Determine the person's relative position and distance.
3. Confirm that the target is suitable for following.
4. Start moving toward the target.
5. Maintain the configured following distance.
6. Continuously monitor the target.
7. Stop if the target is lost or approaches the trolley unexpectedly.

### Current planned implementation

The LiDAR is used to determine distances to objects.

A person's legs are identified using their characteristic:

* spacing
* width
* movement
* position relative to the trolley

The camera may later be used to improve person identification and target tracking.

### Critical safety behavior

If the target cannot be reliably identified:

**STOP.**

If the target disappears:

**STOP.**

If an obstacle is detected in the path:

**STOP.**

If the person walks toward the trolley:

**STOP.**

If sensor data required for safe following becomes unavailable:

**STOP.**

Follow Me must never blindly continue using the person's last known position.

---

# Safety Architecture

Safety has priority over convenience and autonomous behavior.

The general priority is:

```text
Emergency / Fault
       ↓
Safety Stop
       ↓
Obstacle Avoidance / Collision Prevention
       ↓
Rollback / Descent Protection
       ↓
User Commands
       ↓
Autonomous Behavior
       ↓
Normal Motion
```

A higher-priority safety condition must be able to override a lower-priority motion command.

## Fail-Safe Principle

When in doubt, stop the motors.

Examples:

* loss of LiDAR communication
* invalid IMU data
* invalid encoder data
* ODrive communication failure
* application crash
* unexpected state transition
* invalid motor command
* target lost during Follow Me
* obstacle detected
* inconsistent sensor data

The system must never assume that missing data means that everything is safe.

---

# Motor Safety

All motor commands must pass through a central motion-control/safety layer.

Individual features must **not directly control the motors**.

For example, Follow Me should generate a desired motion command:

```text
desired_linear_velocity
desired_angular_velocity
```

rather than directly commanding:

```text
left_motor
right_motor
```

The safety/motion layer then decides whether that command is permitted.

Conceptually:

```text
Feature
  ↓
Desired Motion
  ↓
Safety Checks
  ↓
Motion Limits
  ↓
Motor Controller
```

This prevents individual features from bypassing safety mechanisms.

---

# Sensor Handling

Sensor input must be treated as asynchronous and potentially unreliable.

Every sensor interface should provide, where applicable:

* current value
* timestamp
* validity
* communication status
* quality/confidence

Avoid using stale sensor data without explicitly checking its age.

For example:

```text
sensor_value
sensor_timestamp
sensor_valid
```

A sensor timeout should be detected explicitly.

---

# Units

Use SI units throughout the software.

Preferred units:

| Quantity         | Unit               |
| ---------------- | ------------------ |
| Distance         | meters             |
| Velocity         | meters/second      |
| Acceleration     | meters/second²     |
| Angle            | radians internally |
| Angular velocity | radians/second     |
| Time             | seconds            |
| Force            | Newtons            |
| Battery voltage  | volts              |
| Current          | amperes            |

Human-facing UI may use degrees, km/h, etc., but internal software should use SI units.

Do not mix units implicitly.

---

# Control and Timing

Motion control should run at a deterministic and explicitly defined update rate where practical.

Sensor acquisition, state estimation, safety monitoring, and motor control should not depend on arbitrary UI or application-loop timing.

Avoid blocking operations inside time-critical control loops.

Long-running operations such as:

* camera processing
* logging
* network communication
* GPS processing

should not block safety-critical control logic.

---

# Fault Handling

Faults should be explicit states rather than merely log messages.

Examples:

```text
ODRIVE_COMMUNICATION_LOST
ODRIVE_MOTOR_FAULT
IMU_TIMEOUT
LIDAR_TIMEOUT
ENCODER_INVALID
BATTERY_LOW
BATTERY_CRITICAL
FOLLOW_TARGET_LOST
OBSTACLE_DETECTED
INVALID_MOTION_COMMAND
```

A fault should define:

1. Detection condition
2. Safe response
3. Whether motor operation is permitted
4. Whether automatic recovery is allowed
5. What information is shown to the user
6. What information is logged

Fault recovery must never cause the motors to start unexpectedly.

---

# Logging and Diagnostics

The system should provide structured logging.

Log at least:

* operating-mode changes
* safety-state changes
* sensor failures
* ODrive faults
* motor commands
* detected obstacles
* Follow Me target state
* Hill Assist activation
* Hill Descent Brake activation
* Rollback events
* battery state
* application errors

Avoid excessive logging inside high-frequency control loops.

Use rate limiting or aggregation for repetitive events.

---

# Configuration

Parameters must not be scattered throughout the code.

Centralize configurable values such as:

* maximum speed
* acceleration
* deceleration
* Hill Assist threshold
* Hill Descent Brake threshold
* rollback threshold
* Follow Me distance
* Follow Me maximum speed
* obstacle stopping distance
* sensor timeouts
* force-sensor thresholds

Configuration values should have meaningful names and explicit units.

Example:

```text
FOLLOW_ME_DISTANCE_M
MAX_LINEAR_VELOCITY_MPS
ROLLBACK_THRESHOLD_MPS
LIDAR_TIMEOUT_S
```

---

# Development Rules

## Safety Before Features

Do not implement autonomous functionality before the underlying manual motor control and safety mechanisms are reliable.

Recommended development order:

1. ODrive communication
2. Motor control
3. Encoder feedback
4. Emergency stop / safe shutdown
5. IMU integration
6. Basic motion state estimation
7. Rollback Protection
8. Hill Descent Brake
9. Hill Assist
10. LiDAR integration
11. Obstacle detection
12. Follow Me
13. Camera-based improvements
14. GPS/localization
15. Mapping/navigation

---

## Testing

Hardware-dependent code should be designed so that it can be tested without the physical trolley where possible.

Prefer interfaces such as:

```text
MotorController
IMU
LiDAR
Camera
GPS
ForceSensor
HMI
```

with mock/simulated implementations for testing.

Test at least:

* normal operation
* sensor timeout
* invalid sensor data
* communication loss
* motor-controller failure
* unexpected direction
* slope transitions
* obstacle appearance
* target loss
* rapid state transitions
* boundary conditions
* startup/shutdown
* recovery from faults

Safety behavior should be tested independently from the autonomous algorithms.

---

# State Machines

Use explicit state machines for safety-critical behavior.

Avoid implementing complex operating-mode logic as a collection of unrelated boolean flags.

Prefer:

```text
enum OperatingMode {
    OFF,
    MANUAL,
    HILL_ASSIST,
    HILL_DESCENT_BRAKE,
    ROLLBACK_PROTECTION,
    FOLLOW_ME,
    FAULT
}
```

State transitions should be explicit and documented.

Unexpected transitions must result in a safe state.

---

# Code Quality

When modifying the code:

* Prefer simple, explicit implementations over clever abstractions.
* Keep safety-critical logic easy to inspect.
* Avoid hidden side effects.
* Avoid global mutable state where possible.
* Validate external inputs.
* Handle communication failures explicitly.
* Add tests for safety-critical behavior.
* Do not silently ignore exceptions.
* Do not automatically retry an operation if retrying could cause unexpected motor behavior.
* Do not change hardware configuration without understanding its implications.

Before introducing a new dependency, consider whether it is necessary and whether it increases system complexity or failure modes.

---

# Hardware-Specific Documentation

Hardware-specific behavior must be documented close to the corresponding driver/integration.

This includes:

* ODrive configuration
* serial/CAN/USB communication
* motor direction
* encoder polarity
* sensor mounting orientation
* coordinate systems
* LiDAR mounting position
* IMU mounting orientation
* camera orientation
* GPIO assignments
* electrical limits

Never assume that a sensor's coordinate system matches the application's coordinate system.

Document all transformations explicitly.

---

# Coordinate System

The trolley coordinate system should be defined consistently.

Recommended convention:

```text
             +X
              ↑
              │
        trolley forward
              │
              │
      +Y ←────┼────→ -Y
              │
              │
             -X
```

The exact convention may be changed if required by the hardware/software stack, but once selected it must be used consistently throughout the application.

Sensor coordinate transformations must be handled at the sensor interface boundary.

---

# External References

## ODrive

ODrive 0.5.6 documentation:

[ODrive Documentation](https://docs.odriverobotics.com/v/0.5.6/getting-started.html?utm_source=chatgpt.com)

When implementing or modifying ODrive integration, use the documentation corresponding to the actual firmware/API version running on the controller.

Do not assume that examples from newer ODrive versions are compatible with ODrive 3.6 / firmware 0.5.6.

---

# Agent Instructions

When working on this project, always:

1. Understand the existing architecture before modifying it.
2. Identify whether a change affects motor control or safety.
3. Keep safety logic independent from autonomous decision making.
4. Prefer stopping over continuing when required information is unavailable.
5. Never bypass the central safety/motion layer.
6. Never directly command motors from a high-level feature.
7. Preserve existing ODrive motor/encoder configuration unless explicitly instructed otherwise.
8. Use explicit units in variable names or documentation where ambiguity is possible.
9. Add tests for safety-critical changes.
10. Consider sensor failure and communication loss for every new hardware integration.
11. Avoid blocking safety-critical control loops.
12. Make state transitions explicit.
13. Log important safety and fault events.
14. Do not assume that a sensor measurement is valid merely because a value was received.
15. Before implementing autonomous motion, ensure that an independent safety mechanism can stop the trolley.

## Most Important Rule

**The trolley must fail safe.**

If the software cannot confidently determine that continued movement is safe, it must stop the motors.
