# Plan: Wheel-Slip / Traction Detection

**TL;DR** — Detect when the trolley's wheels are spinning without the trolley
moving (wheel slip / loss of traction), and warn the operator (and optionally
limit speed) so the trolley doesn't dig in or lose control on wet grass or
steep slopes.

**User decisions (confirmed)**
- Scope: Wheel-slip / traction detection only.
- Detection: compare the **wheel-commanded velocity** (from `/motor/state`) to
  the **actual fused-pose velocity** (from `/odometry/filtered`). When the
  wheels are spinning much faster than the trolley is actually moving, that's
  slip.
- Action: publish a `SlipStatus` message; the Safety Controller can warn and/or
  limit speed. No hard stop by default (slip is a warning, not a fault).

---

## Current state (verified)

- **`/motor/state`** (`golfcart_msgs/MotorState`): `left_velocity`,
  `right_velocity` (m/s) — the wheel speeds from the ODrive/mock.
- **`/odometry/filtered`** (`nav_msgs/Odometry`): the fused pose from the EKF
  (wheel odom + IMU + GPS). Its `twist.twist.linear.x` is the actual forward
  velocity.
- **`wheel_odometry_node`** computes `v = (v_left + v_right) / 2` from motor
  state — the wheel-derived forward velocity.
- **Safety Controller** (`safety_controller_node`) subscribes to status topics
  (`SlopeStatus`, `ObstacleState`, `SpeedZoneStatus`) and reacts. It's the
  natural consumer of a slip warning.

### Key gaps for wheel-slip detection (what this plan fills)
1. **No slip detection** — nothing compares wheel speed to actual motion.
2. **No slip status message** — nothing to carry the slip state to the Safety
   Controller / HMI.
3. **No operator warning** — the operator isn't told the wheels are slipping.

---

## Architecture

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

### New message: `SlipStatus.msg`
Published on `/slip/status`.

```text
# Wheel-slip / traction status from wheel_slip_node.
bool   slipping          # true when sustained slip is detected
float64 slip_ratio       # (wheel_speed - actual_speed) / wheel_speed (0-1)
float64 wheel_speed_mps  # wheel-derived forward speed (m/s)
float64 actual_speed_mps # fused-pose forward speed (m/s)
bool   valid
builtin_interfaces/Time timestamp
```

### Pure math: `slip_math.hpp`
- `slip_ratio(wheel_speed, actual_speed)` — 0 when no slip, 1 when fully
  spinning in place. Guard against divide-by-zero (wheel_speed near 0 → no
  slip).
- `is_slipping(slip_ratio, threshold)` — true when `slip_ratio > threshold`.
- A small **debounce** (sustained slip for `debounce_s` before declaring
  SLIPPING) to avoid false positives from noise.

### New node: `wheel_slip_node`
- Subscribes `/motor/state` (wheel velocities) and `/odometry/filtered`
  (actual velocity).
- Computes `wheel_speed` and `actual_speed`, then `slip_ratio`.
- Applies a debounce: only report `slipping=true` after slip persists for
  `debounce_s`.
- Publishes `SlipStatus` on `/slip/status` on a timer.

### Safety Controller integration
- Subscribe `/slip/status`. When `slipping`, publish a warning (via
  `/safety/state` or a log) and optionally clamp the max linear velocity to
  `slip_limit_mps` (a cap, not a stop). Configurable via a param.

### Config (`config/golfcart.yaml`)
Add a `wheel_slip_node` section:
```yaml
wheel_slip_node:
  slip_threshold: 0.3        # slip_ratio above this = slipping (0-1)
  debounce_s: 0.5            # sustained slip time before declaring (s)
  publish_rate_hz: 20.0      # status publish rate (Hz)
```

### Launch (`core.launch.py`)
Add the `wheel_slip_node` to the core control pipeline, using `_node_params`.

---

## Safety notes
- Slip is a **warning**, not a fault — it doesn't hard-stop by default.
- The Safety Controller can optionally **limit** speed during slip (a cap, not
  a stop), so the operator keeps control.
- The debounce prevents false positives from sensor noise.

## Effort
Medium. A new `wheel_slip_node` + `SlipStatus.msg` + pure math
(`slip_math.hpp`) + Safety Controller integration + config.

## Validation
- Unit tests for `slip_ratio` (no slip, partial, full spin, divide-by-zero) and
  the debounce logic.
- Build + existing tests pass.
- Headless Gazebo smoke test: command the wheels to spin while the cart is
  held (or on a low-friction surface), verify `/slip/status` reports slipping.