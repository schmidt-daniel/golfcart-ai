# Roadmap: Next Features

This document captures the next batch of features to implement, selected for
being **software-only** (no new hardware) and building directly on the existing
autonomous stack. Each feature reuses already-implemented nodes, messages, and
patterns.

**Selected (in priority order):**

| # | Feature | Theme | Effort | Reuses |
| --- | --- | --- | --- | --- |
| 8 | Battery Range Estimator | Operator UX | Medium | battery, slope costmap |

> **Completed:** Remote E-Stop + Live Telemetry Dashboard (`docs/features/remote-estop-dashboard.md`),
> GPS-Denied Dead-Reckoning Fallback (`docs/features/dead-reckoning.md`),
> Obstacle Steering Assist (`docs/features/steering-assist.md`).

> **Not selected now:** Voice Control, Push Assist, camera person re-ID,
> weather sensing — these require new hardware (mic, load cell, camera
> processing, sensors) and are documented separately in `docs/features/`.

---

## 8. Battery Range Estimator

**Goal:** Estimate remaining range from battery charge + upcoming terrain slope
+ remaining hole distance, and warn the operator if the trolley may not make it
back.

**Why now:** Prevents the trolley from running out mid-round. All inputs exist.

### Reuses
- `battery_node` — `/battery/state` (charge %, voltage).
- Slope costmap (`slope_node` / `dem.py`) — terrain gradient for energy
  consumption.
- `course_session_node` — remaining hole distance (`HoleSession.remaining_m`).
- `geofence_node` / course map — distance back to the start/charger.

### Design
```text
/battery/state (charge %)  +  slope costmap  +  remaining distance
        ↓
   range_estimator_node
        │   ├─ energy model: Wh consumed per meter (flat) + slope penalty
        │   ├─ estimate remaining range (m) from current charge
        │   ├─ compare to remaining hole distance + return-to-base distance
        │   └─ publish /range/status (range_m, can_finish, warn)
        ↓
   HMI / web (range gauge + "may not make it back" warning)
```

- **Energy model:** a simple linear model `Wh/m` with a slope-dependent
  multiplier (uphill costs more, downhill less), calibrated from the battery
  voltage drop over known distances.
- **Warnings:** three states — OK / CAUTION (range < remaining + margin) /
  CRITICAL (range < remaining). Surface on HMI/web; optionally trigger the
  auto-shutdown/energy-saver to reduce consumption.

### Effort
Medium. A new `range_estimator_node` + a calibration pass; reuses battery,
slope, and course data.

---

## Suggested implementation order

1. **#8 Battery Range Estimator** — independent, medium, good operator value.
4. **#6 GPS-Denied Fallback** — robustness; pairs with #8 (both touch
   localization/battery policy).
5. **#7 Obstacle Steering Assist** — UX polish; last because it needs careful
   tuning against the safety backstop.

Each feature should follow the existing pattern: a plan in `plans/`, a feature
doc in `docs/features/`, unit tests for pure math, a headless e2e check script,
and a `FEATURES.md` entry when implemented.