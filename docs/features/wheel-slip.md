# Wheel-Slip / Traction Detection

Detect when the trolley's wheels are spinning without the trolley moving
(wheel slip / loss of traction), and warn the operator so the trolley doesn't
dig in or lose control on wet grass or steep slopes.

> **Status:** Implemented. See `plans/plan-wheel-slip.prompt.md` for the full
> design.

## Purpose

On wet grass (common on golf courses) or steep slopes, the wheels can spin
without the trolley moving. This is a real hazard — the trolley can dig in,
lose control, or fail to climb. Detecting it and warning the operator is
valuable.

## Overview

```text
/motor/state (wheel velocities)  +  /odometry/filtered (actual velocity)
        ↓
   wheel_slip_node
        │   ├─ wheel_speed = (v_left + v_right) / 2
        │   ├─ actual_speed = |twist.linear.x|
        │   ├─ slip_ratio = (wheel_speed - actual_speed) / wheel_speed
        │   └─ if slip_ratio > threshold for a sustained time → SLIPPING
        ↓  /slip/status (SlipStatus)
        ↓
   Safety Controller (warn / limit speed) + HMI/web badge
```

## How it works

- **`wheel_slip_node`** (`golfcart_control`) subscribes `/motor/state` (wheel
  velocities) and `/odometry/filtered` (actual fused-pose velocity).
- Computes `wheel_speed = (v_left + v_right) / 2` and
  `actual_speed = |twist.linear.x|`.
- Computes `slip_ratio = (wheel_speed - actual_speed) / wheel_speed`:
  - `0.0` = no slip (wheels match actual motion).
  - `1.0` = fully spinning in place.
  - Guarded against divide-by-zero; clamped to `[0, 1]`.
- **Debounce:** only reports `slipping=true` after slip persists for
  `debounce_s`, avoiding false positives from sensor noise.
- Publishes `SlipStatus` on `/slip/status`.

## Safety considerations

- Slip is a **warning**, not a fault — it doesn't hard-stop by default.
- The Safety Controller can optionally **limit** speed during slip (a cap, not
  a stop), so the operator keeps control.
- The debounce prevents false positives from sensor noise.

## Configuration

See `config/golfcart.yaml`:

```yaml
wheel_slip_node:
  slip_threshold: 0.3        # slip_ratio above this = slipping (0-1)
  debounce_s: 0.5            # sustained slip time before declaring (s)
  publish_rate_hz: 20.0      # status publish rate (Hz)
```

## Messages

- `golfcart_msgs/SlipStatus` — `/slip/status`
  (slipping, slip_ratio, wheel_speed_mps, actual_speed_mps, valid).

## Validation

- Pure math in `slip_math.hpp` (slip_ratio, debounced detector) — unit-tested
  (`test_slip_math.cpp`).
- Full workspace test suite passes.
- Headless Gazebo smoke test: command the wheels to spin while the cart is
  held, verify `/slip/status` reports slipping.