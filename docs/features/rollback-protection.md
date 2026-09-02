# Rollback Protection

Rollback Protection prevents unintended backward movement.

Primary input:

- wheel encoder velocity

The detection algorithm should distinguish actual backward movement from:

- encoder noise
- measurement jitter
- transient sign changes

When rollback is confirmed:

```text
Backward movement detected
        ↓
Safety Controller
        ↓
Apply braking
        ↓
Prevent continued backward movement
        ↓
Report event
```

Rollback protection must not depend solely on the IMU.