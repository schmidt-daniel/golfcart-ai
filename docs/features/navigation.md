# Autonomous Navigation (Nav2)

Autonomous navigation to a target on the course is **implemented** (Phases 0–8
of `plans/plan-autonomous-navigation.prompt.md`). The trolley plans a path to a
goal and follows it while avoiding obstacles, respecting forbidden zones, and
handling GPS loss and manual override.

## Architecture

```text
GPS / IMU / Wheel Odometry / LiDAR
              ↓
        Localization (robot_localization EKF)
              ↓
             Map (slam_toolbox / CourseMap)
              ↓
            Nav2 (planner + controller + costmaps)
              ↓
       MotionRequest (priority 0)
              ↓
      Safety Controller (arbitration)
              ↓
         ODrive
```

## Implemented stack

| Component | Package / Node | Notes |
| --- | --- | --- |
| Messages | `golfcart_msgs` | `GoalPose`, `NavigationStatus`, `CourseMap`, `SetGoal.srv` |
| Wheel odometry | `golfcart_localization/wheel_odometry_node` | `/motor/state` → `nav_msgs/Odometry` + TF `odom→base_link` |
| Sensor fusion | `golfcart_localization/sensor_fusion_node` + `robot_localization` EKF | fuses wheel odom + IMU + GPS → `/odometry/filtered` |
| Localization quality | `golfcart_localization/localization_quality_node` | monitors EKF covariance → `NavigationStatus` DEGRADED/OK |
| Mapping | `golfcart_mapping` (slam_toolbox) | online async SLAM, per-course occupancy grid |
| Planner | Nav2 `planner_server` (NavFn) | global path planning |
| Controller | Nav2 `controller_server` (Regulated Pure Pursuit) | local tracking |
| Behavior tree | Nav2 `bt_navigator` | `navigate_to_pose` action |
| Velocity smoothing | Nav2 `velocity_smoother` | smooths cmd_vel |
| Costmaps | Nav2 global + local costmaps | obstacle inflation |
| Nav bridge | `golfcart_navigation/navigation_node` | `cmd_vel`→`MotionRequest` (priority 0), `/set_goal`→action, pause on manual override |
| Georeferencing | `golfcart_navigation/georeference_node` | lat/lon → map-frame via `/set_goal_geo` |
| Simulation | `golfcart_gazebo` | gz-sim course world for validation |

## Key behaviors

- **Obstacle avoidance** — Nav2 costmaps + RPP controller steer around obstacles.
- **Forbidden-zone stop** — the cart stops in front of forbidden zones (water,
  steep zones) defined in the `CourseMap`.
- **Manual override** — a higher-priority manual `MotionRequest` pauses
  autonomous motion (priority arbitration in the Safety Controller).
- **Obstacle mid-route** — on an unexpected obstacle, stop and ask the operator
  (via the web app).
- **No-map direct route** — without a map, take a direct route and map as it
  drives.
- **Slope cost layer** — roll/pitch asymmetry from the `CourseMap` slope data
  raises cost on steep terrain.
- **Localization-quality gate** — if EKF covariance degrades (e.g. GPS loss),
  navigation is gated / degraded.

## Decisions (see `plans/plan-autonomous-navigation.prompt.md`)

- **Planner:** Nav2 (NavFn planner + RPP controller + costmaps).
- **Localization:** GPS for coarse global position + LiDAR SLAM for local precision.
- **Map:** per-course maps (slam_toolbox) + `CourseMap` (features, slope, forbidden zones).
- **SLAM library:** slam_toolbox.
- **Controller:** Regulated Pure Pursuit (RPP).
- **Deployment:** Option D hybrid (systemd + rsync, see `docs/architecture.md` §34.1).
- **GPS anchoring:** map origin anchored to GPS; dead reckoning on GPS loss.
- **Priority arbitration:** 0=autonomous, 1=manual, 2=behavior, 3=safety override.

## Status

Implemented and validated in simulation (gz-sim). Hardware validation on the
Raspberry Pi 5 is pending (see `docs/architecture.md` §34.1 deployment).
