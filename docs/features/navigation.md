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

## Decisions (see `plans/plan-autonomous-navigation.prompt.md`)

- **Planner:** Nav2 (planner + controller + costmap).
- **Localization:** GPS for coarse global position + LiDAR SLAM for local precision.
- **Map:** OSM (base geometry) + own sensor mapping + refinement from recorded visits. Maps are **per-course**.
- **Slope safety:** map-based steep zones (from IMU during visits) + live IMU roll/pitch check.
- **Negative obstacles (ditches/lakes):** depth camera (GIXVISION) feeds the costmap and safety.
- **Learning:** record successful routes; refine the map from each visit; prefer known-good routes.
- **Operator interaction:** on an unexpected obstacle mid-route, stop and ask the operator via the web app.