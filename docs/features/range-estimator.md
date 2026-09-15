# Battery Range Estimator (self-learning)

Estimate the trolley's remaining range from battery charge, terrain slope, and
remaining hole distance, and warn the operator if it may not make it back. The
energy model **learns** from measured battery current over time, so it improves
with every round instead of relying on a fixed calibration.

> **Status:** Implemented. See `plans/plan-range-estimator.prompt.md` for the
> full design.

## Purpose

Prevents the trolley from running out mid-round. The operator gets a live range
estimate and a clear warning when the trolley may not make it back.

## Overview

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

## How it works

### Energy model (`range_estimator_math.hpp`)
- A slope-bucketed `Wh/m` table: **downhill / flat / uphill**.
- Each bucket holds a `wh_per_m` value (energy consumed per meter).

### Learning
- The node integrates energy (`voltage × current × dt`) and distance
  (`/odometry/filtered` deltas) while moving.
- Periodically (every `update_period_s`), it updates the current slope bucket's
  `wh_per_m` via an online EMA:
  `new = (1 - alpha) * old + alpha * measured`.
- Measurements are clamped to a sane range so a bad sample can't produce a
  wildly optimistic estimate.
- **No persistence across reboots in v1** — the model starts from
  `default_wh_per_m` each boot. Persistence can be added later.

### Range estimate
- `usable_wh = charge% × capacity - reserve`.
- `range_m = usable_wh / wh_per_m` (using the current slope bucket).

### Warnings
- **OK** — range covers remaining + return + margin.
- **CAUTION** — range covers remaining + return, but not the margin.
- **CRITICAL** — range does not cover remaining + return.

## Safety considerations

- The range estimator is **informational only** — it never commands motion. It
  only warns the operator.
- The reserve (`reserve_wh`) and margin (`return_margin_m`) keep the estimate
  conservative so the trolley doesn't run out mid-round.
- The learned model is clamped so a bad measurement can't produce an optimistic
  estimate.

## Configuration

See `config/golfcart.yaml`:

```yaml
range_estimator_node:
  battery_capacity_wh: 500.0        # usable pack capacity (Wh)
  reserve_wh: 50.0                  # reserve kept for safety (Wh)
  return_margin_m: 50.0             # extra distance margin for "can_finish" (m)
  ema_alpha: 0.3                    # learning rate for the Wh/m model (0-1)
  update_period_s: 5.0              # how often to update the model + publish (s)
  default_wh_per_m: 0.02            # initial flat-ground consumption (Wh/m)
```

## Messages

- `golfcart_msgs/RangeStatus` — `/range/status`
  (state, range_m, remaining_m, return_m, can_finish, wh_per_m).

## Validation

- Pure math in `range_estimator_math.hpp` (slope bucketing, EMA learning,
  range estimate, warning states) — unit-tested (`test_range_estimator.cpp`).
- Full workspace test suite passes.
- Gazebo smoke test: drive the course, verify `/range/status` is published and
  the model updates from measured current.