# GPS-Denied Dead-Reckoning Fallback

When GPS is lost, the trolley keeps driving on the fused pose (wheel odometry +
IMU) for a **bounded** time and distance, warns the operator, and then
safe-stops if GPS does not recover — instead of stopping outright the moment
GPS drops.

> **Status:** Implemented. See `plans/plan-dead-reckoning.prompt.md` for the
> full design.

## Purpose

GPS drops are common on tree-lined fairways, in tunnels, and near buildings.
Stopping outright on every drop is safe but frustrating. This feature adds a
graceful-degradation policy: within a bounded dead-reckoning budget, the
trolley keeps driving on the already-fused pose; beyond the budget it stops.

## Overview

```text
/gps/fix ──┐
/odometry/filtered ──┤
                     ↓
localization_quality_node → /localization/quality (OK / DEGRADED / LOST)
                     ↓
dead_reckoning_node (policy)
        │   ├─ GPS OK        → normal operation
        │   ├─ GPS DEGRADED  → warn operator; keep driving on fused pose
        │   └─ GPS LOST      → drive on dead-reckoning for a bounded
        │                      time/distance, then safe-stop if not recovered
        ↓  /dead_reckoning/status (operator badge)
        ↓  /motion/request (priority-3 stop when budget exceeded)
        ↓  /geofence/status (OUT_OF_FIX stop suppressed during DR)
```

## How it works

### Localization quality (`localization_quality_node`)
- Monitors the EKF fused-pose covariance (`/odometry/filtered`) **and** the age
  of the last valid GPS fix (`/gps/fix`).
- Publishes `NavigationStatus` on `/localization/quality`:
  - `OK` — covariance low and a recent valid GPS fix.
  - `DEGRADED` — covariance above `max_position_covariance` (drift too high).
  - `LOST` — no valid GPS fix for `gps_timeout_s`.

### Dead-reckoning policy (`dead_reckoning_node`)
- Subscribes `/localization/quality`, `/odometry/filtered`, `/gps/fix`.
- State machine:
  - `OK` → no DR; budget counters reset.
  - `DEGRADED` → warn operator (status `DEGRADED`), keep driving.
  - `LOST` → enter DR. Integrate distance from `/odometry/filtered` deltas.
    While `used_time < max_dr_time_s` **and** `used_distance < max_dr_distance_m`:
    status `DRIVING_DR`, keep driving. When either budget is exhausted →
    status `DR_BUDGET_EXCEEDED` and publish a priority-3 zero `MotionRequest`
    (safe stop).
  - Recovery: when `/gps/fix` becomes valid again → back to `OK`, reset.
- Publishes `DeadReckoningStatus` on `/dead_reckoning/status` (budget used /
  remaining, state) for the HMI/web badge.

### Geofence backstop (`geofence_node`)
- While the DR node reports `DRIVING_DR` (within budget), the geofence
  **suppresses** its `OUT_OF_FIX` priority-3 stop (it still publishes
  `OUT_OF_FIX` for the operator, but does not force a stop).
- When DR reports `DR_BUDGET_EXCEEDED` (or `OK`/`DEGRADED` with no DR), the
  geofence resumes its normal stop behavior.
- This keeps the geofence as the backstop: if the DR node is absent or fails,
  the geofence still stops on GPS loss.

## Safety considerations

- The DR fallback is a **policy layer**, not a bypass. All motion still flows
  through the Safety Controller.
- The budget bounds how far the trolley can be from its true position before
  stopping (dead-reckoning accumulates drift).
- The geofence `OUT_OF_FIX` stop remains as a backstop if the DR node is absent
  or fails.
- The budget-exceeded stop is a priority-3 request (same as the geofence stop),
  so it overrides autonomous (0) and manual (1) requests.

## Configuration

See `config/golfcart.yaml`:

```yaml
localization_quality_node:
  max_position_covariance: 1.0      # EKF covariance above this = DEGRADED
  gps_timeout_s: 3.0                # no valid GPS fix for this long = LOST (s)

dead_reckoning_node:
  max_dr_time_s: 30.0               # max seconds to drive on dead reckoning (s)
  max_dr_distance_m: 50.0           # max meters to drive on dead reckoning (m)
  stop_priority: 3                  # priority of the budget-exceeded stop request
```

## Messages

- `golfcart_msgs/DeadReckoningStatus` — `/dead_reckoning/status`
  (state, budget used/remaining, timestamp).

## Validation

- Pure math in `dead_reckoning_math.hpp` (budget integration, budget-exceeded
  decision) — unit-tested (`test_dead_reckoning.cpp`).
- Full workspace test suite passes.
- Gazebo smoke test: use `gps_dropout_node.py` to drop GPS and verify the
  trolley keeps driving within budget, then stops.