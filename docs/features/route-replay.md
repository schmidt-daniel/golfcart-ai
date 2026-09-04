# Route Recording and Replay

Record a GPS route while driving, then replay it autonomously.

> **Status:** Planned. Autonomous navigation is implemented (see `navigation.md`);
> route replay would record waypoints and feed them to the Nav2 goal interface.

## Purpose

A natural stepping stone toward autonomous navigation. The operator drives
(or pushes) the trolley along a route once; the trolley can then repeat it.

## Recording

- Record GPS fixes (and optionally IMU/odometry) into a route while the
  trolley is driven manually.
- Store the route as a list of waypoints.

## Replay

- A waypoint-following controller drives the trolley along the recorded route.
- Uses GPS for global position and IMU/odometry for short-term motion.

## Architecture

```text
GPS / IMU / Odometry
        ↓
   Localization
        ↓
  Route (waypoints)
        ↓
 Waypoint Follower
        ↓
   MotionRequest
        ↓
  Safety Controller
```

## Relationship to Nav2

Route replay is simpler than full autonomous navigation. It can be implemented
before introducing Nav2. If full navigation is later required, evaluate Nav2
(see `navigation.md`).

## Effort

Medium. Requires GPS/localization (planned) plus a waypoint-following
controller.