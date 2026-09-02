# Obstacle Detection and Stopping

Obstacle detection is a safety-related function.

The system should maintain a configurable stopping zone in front of the trolley.

The stopping threshold should consider:

- current speed
- stopping distance
- reaction time
- sensor latency
- terrain/slope
- configured safety margin

Obstacle detection should result in a safety-layer decision rather than directly commanding the motors.

Conceptually:

```text
LiDAR
  ↓
Obstacle Detector
  ↓
Obstacle State
  ↓
Safety Controller
  ↓
STOP / LIMIT / ALLOW
```

## Negative Obstacles (ditches, lakes, streams)

2D LiDAR cannot detect negative obstacles (drop-offs, water). A **depth camera**
(GIXVISION) is used to detect these, feeding the costmap and safety layer.

```text
Depth Camera
    ↓
Negative Obstacle Detector
    ↓
Obstacle State
    ↓
Safety Controller
```

## Unexpected Obstacle Operator Interaction

When a previously unknown obstacle blocks the route during autonomous
navigation, the trolley stops and asks the operator how to proceed **via the
web app** (e.g. "retry / replan / stop").

See `plans/plan-autonomous-navigation.prompt.md` for the full navigation plan.