# Roadmap: Next Features

This document captures the next batch of features to implement, selected for
being **software-only** (no new hardware) and building directly on the existing
autonomous stack. Each feature reuses already-implemented nodes, messages, and
patterns.

**Selected (in priority order):**

| # | Feature | Theme | Effort | Reuses |
| --- | --- | --- | --- | --- |
| 1 | Remote E-Stop + Live Telemetry Dashboard | Safety / Operator UX | Low-Med | web server, safety controller |
| 6 | GPS-Denied Dead-Reckoning Fallback | Robustness | Medium | localization quality, EKF |
| 7 | Obstacle Steering Assist (manual) | Safety / UX | Medium | obstacle awareness |
| 8 | Battery Range Estimator | Operator UX | Medium | battery, slope costmap |

> **Not selected now:** Voice Control, Push Assist, camera person re-ID,
> weather sensing — these require new hardware (mic, load cell, camera
> processing, sensors) and are documented separately in `docs/features/`.

---

## 1. Remote E-Stop + Live Telemetry Dashboard

**Goal:** Give the operator a phone/web page with a large emergency-stop button
and a live view of the trolley's state (position, battery, speed, mode, faults).

**Why now:** Highest safety value, purely software, and reuses the existing web
server + safety controller. It is the natural companion to the other phone/HMI
features (summon, teleop).

### Reuses
- `web_teleop_server` (`golfcart_teleop`) — serves the phone page + rosbridge.
- `safety_controller_node` — already exposes `/safety/stop` (Trigger service)
  and `/safety/state` (String). A remote E-STOP is just a client of these.
- `/gps/fix`, `/battery/state`, `/navigation/status`, `/geofence/status`,
  `/speed_zone/status`, `/slope/status` — all already published.

### Design
```text
Phone browser (dashboard.html)
        ↓  rosbridge (roslib.js)
   /safety/stop  (Trigger)   →  Safety Controller → safe stop (priority-3)
   /safety/state, /gps/fix, /battery/state, /navigation/status, ...
        ↓
   Live telemetry panel (position, battery %, speed, mode, fault badges)
```

- **E-STOP button:** calls `/safety/stop` (already a hard stop). Add a
  confirmation + a "re-enable" path via `/safety/enable`. The button must be
  visually distinct and require deliberate action (hold-to-confirm) to avoid
  accidental stops.
- **Telemetry panel:** subscribe to the existing status topics and render
  battery %, position (map + lat/lon), current speed, safety state, and any
  active limit (geofence NEAR/CROSSED, speed-zone limit, slope warning).
- **Fault badges:** surface `safety/state` FAULT and the hard-stop flags
  (battery critical, excessive roll, obstacle in zone).

### Safety notes
- The E-STOP is a **request source** — it goes through the Safety Controller's
  arbitration, never bypassing it. It must not command motors directly.
- Network loss must not leave the trolley in an ambiguous state; the Safety
  Controller's existing request-timeout already stops motion if no request
  arrives, so a lost connection is fail-safe.

### Effort
Low-Medium. Mostly a new HTML page + wiring to existing topics/services.

---

## 6. GPS-Denied Dead-Reckoning Fallback

**Goal:** When GPS is lost, keep the trolley usable by leaning harder on wheel
odometry + IMU (already fused in the EKF) and warn the operator, instead of
stopping outright.

**Why now:** Improves robustness in tree-lined fairways / tunnels where GPS
drops. The fusion already exists; this adds a graceful-degradation policy.

### Reuses
- `localization_quality_node` — already monitors EKF covariance and publishes
  `NavigationStatus` DEGRADED/OK on `/localization/quality`.
- `sensor_fusion_node` + `robot_localization` EKF — already fuse wheel odom +
  IMU + GPS into `/odometry/filtered`.
- `geofence_node` — currently treats stale GPS as OUT_OF_FIX (safe stop). This
  feature adds a policy layer on top.

### Design
```text
localization_quality_node  →  /localization/quality (OK / DEGRADED / LOST)
        ↓
   dead_reckoning_node (policy)
        │   ├─ GPS OK        → normal operation
        │   ├─ GPS DEGRADED  → warn operator; keep driving on fused pose
        │   └─ GPS LOST      → drive on dead-reckoning for a bounded time/distance,
        │                      then safe-stop if not recovered
        ↓  /localization/quality + operator notification
```

- **Policy:** define a bounded dead-reckoning budget (e.g. `max_dr_time_s` and
  `max_dr_distance_m`). Within budget, keep driving on the fused pose; beyond
  it, safe-stop (the geofence already stops on OUT_OF_FIX as a backstop).
- **Warn the operator:** surface the degraded state on the HMI/web (badge +
  audio if TTS is added later).
- **Drift awareness:** dead-reckoning accumulates drift; the budget bounds how
  far the trolley can be from its true position before stopping.

### Effort
Medium. Mostly a policy node + wiring; the fusion and quality detection exist.

---

## 7. Obstacle Steering Assist (manual driving)

**Goal:** While the operator drives manually, gently steer around obstacles
instead of only hard-stopping on them.

**Why now:** The hard stop is safe but frustrating; a soft steering nudge
improves the manual experience. The obstacle awareness view already exists.

### Reuses
- `obstacle_awareness_node` (`golfcart_follow`) — already produces a soft
  steering obstacle view (`/obstacles/awareness`) separate from the safety stop.
- `safety_controller_node` — the hard stop on `obstacle_in_zone` stays as the
  safety backstop.
- `motion_controller_node` — differential drive; steering assist is a
  `MotionRequest` (priority 1, manual) that nudges angular velocity.

### Design
```text
/scan → obstacle_awareness_node → /obstacles/awareness (soft steering view)
        ↓
   steering_assist_node
        │   ├─ only active while operator is driving manually (priority 1)
        │   ├─ compute a steering nudge away from nearby obstacles
        │   └─ publish MotionRequest (priority 1) with the nudge
        ↓
   Safety Controller (hard stop on obstacle_in_zone still applies)
```

- **Priority:** the assist is a **manual-priority (1)** request, so it never
  overrides autonomous driving (priority 0) or safety (priority 3).
- **Safety backstop:** the existing hard stop on `obstacle_in_zone` remains
  unchanged — the assist only *reduces* the chance of reaching that state.
- **Tunable:** `assist_gain`, `min_distance_m`, dead zone so it doesn't fight
  the operator's own steering.

### Effort
Medium. A new `steering_assist_node` + tuning; reuses the awareness view.

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

1. **#1 Remote E-Stop + Telemetry** — smallest, highest safety value, unlocks
   the phone dashboard used by #2.
2. **#2 Go to Hole N** — core golf use case; reuses the dashboard + summon
   pattern.
3. **#8 Battery Range Estimator** — independent, medium, good operator value.
4. **#6 GPS-Denied Fallback** — robustness; pairs with #8 (both touch
   localization/battery policy).
5. **#7 Obstacle Steering Assist** — UX polish; last because it needs careful
   tuning against the safety backstop.

Each feature should follow the existing pattern: a plan in `plans/`, a feature
doc in `docs/features/`, unit tests for pure math, a headless e2e check script,
and a `FEATURES.md` entry when implemented.