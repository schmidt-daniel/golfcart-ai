# Hill Descent Brake

Hill Descent Brake is responsible for preventing uncontrolled downhill acceleration.

Inputs:

- IMU inclination
- wheel velocity
- direction of travel

The system should distinguish:

```text
stationary on slope
moving uphill
moving downhill
```

Braking should normally be smooth.

An emergency stop may override smooth braking if required for safety.