# Follow Me

Follow Me consists of separate stages:

```text
Sensor Data
    ↓
Person Detection
    ↓
Person Tracking
    ↓
Target Validation
    ↓
Follow Controller
    ↓
MotionRequest
```

The Follow Me controller should consume an abstract `PersonTarget`.

Example:

```text
PersonTarget
    distance_m
    lateral_offset_m
    relative_velocity_mps
    confidence
    timestamp
    valid
```

The initial implementation may detect a person's legs from LiDAR.

The architecture must allow later implementations using:

- camera detection
- LiDAR + camera fusion
- improved tracking algorithms

without rewriting the Follow Me controller.

## Follow Me stop conditions

The trolley must stop if:

- the person target is lost
- confidence falls below the required threshold
- the person approaches the trolley unexpectedly
- an obstacle is detected
- required sensor data becomes stale
- the Safety Controller rejects the motion request

The trolley must not blindly continue toward the target's last known position.