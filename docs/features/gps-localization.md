# GPS and Localization

GPS provides coarse global localization.

> **Status:** Implemented. `golfcart_localization` fuses GPS + IMU + wheel
> odometry via `robot_localization` EKF (`/odometry/filtered`), with a
> `localization_quality_node` monitoring covariance. See `navigation.md` and
> `docs/architecture.md` for the full stack.

It may be used for:

- mapping
- recording trolley routes
- future navigation
- hole/course localization

GPS must not be used as the sole mechanism for:

- collision avoidance
- precise obstacle positioning
- short-range positioning

A future localization system may combine:

```text
GPS
IMU
wheel odometry
LiDAR
```

using sensor fusion.