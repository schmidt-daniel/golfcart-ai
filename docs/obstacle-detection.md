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