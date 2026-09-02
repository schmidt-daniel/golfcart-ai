# Auto-Shutdown / Sleep When Idle

Automatically power down or enter a low-power state when the trolley is idle.

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

## Safety

- Auto-shutdown must never occur while the trolley is moving.
- Shutdown should be graceful (stop motors, save state, then power off).
- A deliberate user action should be required to restart.

## Effort

Low. A watchdog/timer node that monitors activity and triggers shutdown.