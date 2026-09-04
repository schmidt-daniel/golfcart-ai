# Auto-Shutdown / Sleep When Idle

Automatically power down or enter a low-power state when the trolley is idle.

> **Related:** For frequent short stops during a round, see
> [Energy-Saving Mode](energy-saving.md) — a lighter sleep state that wakes
> automatically on IMU motion. Auto-shutdown is the final, manual-restart
> power-off for long idle / end of day.

## Purpose

Protect the battery and avoid leaving the system running when unused.

## Behavior

If the trolley is idle for a configurable period (no joystick input, no motion,
no active mode), it should:

1. Enter a low-power state, or
2. Power down gracefully.

## Inputs

- joystick activity
- motion state (`/safety/state`)
- operating mode
- IMU data (`/imu/data`) — for roll-away risk

## Safety

- Auto-shutdown must never occur while the trolley is moving.
- **Auto-shutdown must not occur when there is a risk of rolling away.** The
  trolley must not be powered down on a slope where it could roll uncontrolled.
  This is determined from IMU data (inclination) and/or wheel motion.
- Shutdown should be graceful (stop motors, save state, then power off).
- A deliberate user action should be required to restart.

## Roll-Away Risk

The trolley must not shut down if it is at risk of rolling away. Conditions
that indicate roll-away risk:

- **On a slope** — IMU inclination (roll/pitch) exceeds a threshold, meaning
  the trolley could roll if unpowered.
- **Moving** — wheel motion detected (via `/motor/state` or `/safety/state`).
- **Rollback detected** — the rollback-protection state is active.

If any of these are true, auto-shutdown is **suppressed** (the trolley stays
powered so it can hold position / brake).

## Effort

Low. A watchdog/timer node that monitors activity and triggers shutdown,
suppressed by roll-away risk.