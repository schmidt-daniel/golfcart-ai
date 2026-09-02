# Navigation and Future Expansion

Follow Me should initially remain independent of full autonomous navigation.

If future requirements include:

- autonomous travel between holes
- course mapping
- waypoint navigation
- route planning

evaluate ROS 2 Nav2 before implementing a custom navigation framework.

A future architecture could become:

```text
GPS / IMU / Odometry / LiDAR
              ↓
        Localization
              ↓
             Map
              ↓
            Nav2
              ↓
       MotionRequest
              ↓
      Safety Controller
              ↓
         ODrive
```