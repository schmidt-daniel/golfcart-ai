We are building an autonomous golf trolley by adding hardware and software to a traditional push golf trolley.

# Planned features

## Hill Assist

On inclines, additional assistance can optionally be activated.

Potentially with automatic activation.

## Hill Descent Brake

When the vehicle is rolling downhill, it is automatically braked to prevent it from rolling away.

Implementation:

* Inclination detection via gyroscope

## Rollback Protection

In this mode, the vehicle can only be pushed forward. If backward movement is detected, the motor brake is automatically activated.

## Follow Me

The vehicle follows a person walking ahead at a defined distance.

Implementation:

* A LiDAR scanner determines the distance to objects.
* A person’s legs are detected based on their typical spacing/width.
* When the person is moving forward, the vehicle attempts to follow them.
* If the person walks back toward the vehicle, the vehicle stops.


# Hardware
Hardware being used:
- Raspberry Pi 5
- Raspberry Pi HQ camera
- ODrive 3.6 motor controller
- Hoverboard DC motors
- FHL-LD19P Lidar for terrain/environment scanning and hazard/obstacle detection
- Li-Ion 36V battery pack
- Force sensors for supported pushing and braking (by grabbing the handle)
- GPS for localization and mapping
- Small Display and a game controller joystick for HMI
- IMU to measure angles and acceleration
