# GPS and Localization

GPS provides coarse global localization.

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