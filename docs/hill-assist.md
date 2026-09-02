# Hill Assist

Hill Assist determines when additional propulsion assistance is appropriate.

Inputs may include:

- IMU inclination
- wheel velocity
- handle force
- direction of travel

The first implementation should preferably use explicit user activation.

Automatic activation can be introduced after the basic system is validated.

Activation and deactivation must use hysteresis to prevent rapid switching around a threshold.