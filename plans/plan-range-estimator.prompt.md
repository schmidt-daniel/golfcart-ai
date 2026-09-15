# Plan: Battery Range Estimator (self-learning)

**TL;DR** — Estimate the trolley's remaining range from battery charge, terrain
slope, and remaining hole distance, and warn the operator if it may not make it
back. The energy model **learns** from measured battery current over time
(online EMA per slope bucket), so it improves with every round instead of
relying on a fixed calibration.

**User decisions (confirmed)**
- Scope: Battery Range Estimator only.
- **Self-learning:** the `Wh/m` energy model is continuously updated from
  measured battery current × distance, bucketed by terrain slope (online EMA).
  No persistence across reboots in v1 (keeps it simple and safe); persistence
  can be added later.
- Warnings: three states — OK / CAUTION (range < remaining + margin) /
  CRITICAL (range < remaining). Surface on HMI/web via `/range/status`.
- Simulation: Gazebo (existing `golfcart_gazebo` course world).

---

## Current state (verified)

- **Battery** (`golfcart_odrive/battery_node`): publishes `BatteryState` on
  `/battery/state` with `voltage_v`, `current_a`, `charge_percent`, `valid`.
  (The INA219 I2C read is a scaffold returning `valid=false`; the mock/sim
  path provides data.)
- **Slope costmap** (`golfcart_navigation/slope_node`): loads the ground-fixed
  gradient (dz/dx, dz/dy) from the course zip and publishes `SlopeStatus` on
  `/slope/status` (`slope_deg`, `roll_rad`, `pitch_rad`, `valid`).
- **Course session** (`golfcart_navigation/course_session_node`): publishes
  `HoleSession` on `/course/hole` with `remaining_m` (distance to the green),
  `distance_m` (hole distance), and trolley position.
- **Fused pose** (`/odometry/filtered`): `nav_msgs/Odometry` for distance
  integration.
- **Messages** (`golfcart_msgs`): `BatteryState.msg`, `SlopeStatus.msg`,
  `HoleSession.msg` all exist.

### Key gaps for the range estimator (what this plan fills)
1. **No energy model** — nothing estimates Wh consumed per meter.
2. **No range estimate** — nothing converts charge into remaining distance.
3. **No learning** — the model is static; it should improve from measured data.
4. **No operator warning** — nothing surfaces "may not make it back".

---

## Architecture

```text
/battery/state (charge %, current)  +  /slope/status (slope)  +  /odometry/filtered
        ↓
   range_estimator_node
        │   ├─ energy model: Wh/m per slope bucket (learned via EMA)
        │   ├─ estimate remaining range (m) from current charge
        │   ├─ compare to remaining hole distance + return-to-base distance
        │   └─ publish /range/status (range_m, can_finish, warn)
        ↓
   HMI / web (range gauge + "may not make it back" warning)
```

### New message: `RangeStatus.msg`
Published on `/range/status` for the HMI/web gauge.

```text
# Range estimate from range_estimator_node.
string state            # OK / CAUTION / CRITICAL
float64 range_m         # estimated remaining range (m)
float64 remaining_m     # remaining hole distance (m)
float64 return_m        # estimated distance back to base (m)
bool   can_finish       # true if range covers remaining + return
float64 wh_per_m        # current learned energy consumption (Wh/m)
builtin_interfaces/Time timestamp
```

### Pure math: `range_estimator_math.hpp`
- **Energy model:** a slope-bucketed `Wh/m` table. Buckets by slope (e.g.
  downhill / flat / uphill). Each bucket holds a `wh_per_m` value.
- **Learning:** given a measured energy delta (Wh) and distance (m) over a
  sample window, update the bucket's `wh_per_m` via EMA:
  `new = (1 - alpha) * old + alpha * measured`.
- **Range estimate:** `range_m = usable_wh / wh_per_m` (using the current
  slope bucket, or a blended value over the remaining path).
- **Warnings:** OK / CAUTION / CRITICAL based on `range_m` vs
  `remaining_m + return_m` (+ margin).

### New node: `range_estimator_node`
- Subscribes `/battery/state`, `/slope/status`, `/odometry/filtered`,
  `/course/hole`.
- Integrates energy: `energy_wh += voltage * current * dt` while moving.
- Integrates distance from `/odometry/filtered` deltas.
- Periodically (e.g. every 5 s) updates the slope-bucket `wh_per_m` via EMA
  from the accumulated energy/distance, then resets the window.
- Publishes `/range/status` on a timer.

### Config (`config/golfcart.yaml`)
Add a `range_estimator_node` section:
```yaml
range_estimator_node:
  battery_capacity_wh: 500.0    # usable pack capacity (Wh)
  reserve_wh: 50.0              # reserve kept for safety (Wh)
  return_margin_m: 50.0         # extra distance margin for "can_finish" (m)
  ema_alpha: 0.3                # learning rate for the Wh/m model (0-1)
  update_period_s: 5.0          # how often to update the model + publish (s)
  default_wh_per_m: 0.02        # initial flat-ground consumption (Wh/m)
```

### Launch (`navigation.launch.py`)
Add the `range_estimator_node` to the navigation launch, using `_node_params`.

---

## Safety notes
- The range estimator is **informational only** — it never commands motion. It
  only warns the operator.
- The reserve (`reserve_wh`) and margin (`return_margin_m`) keep the estimate
  conservative so the trolley doesn't run out mid-round.
- The learned model is bounded (clamped to a sane range) so a bad measurement
  can't produce a wildly optimistic estimate.

## Effort
Medium. A new `range_estimator_node` + `RangeStatus.msg` + pure math
(`range_estimator_math.hpp`) for the energy model, learning, and warnings —
unit-tested.

## Validation
- Unit tests for the energy model (EMA update, range estimate, warning states).
- Build + existing tests pass.
- Headless Gazebo smoke test: drive the course, verify `/range/status` is
  published and the model updates from measured current.