# Plan: GPS-Denied Dead-Reckoning Fallback

**TL;DR** — When GPS is lost, keep the trolley usable by leaning harder on the
already-fused wheel odometry + IMU pose (from the EKF) for a **bounded** time
and distance, warn the operator, and then safe-stop if GPS does not recover.
This adds a graceful-degradation policy layer on top of the existing fusion and
localization-quality detection, instead of stopping outright the moment GPS
drops.

**User decisions (confirmed)**
- Scope: GPS-denied dead-reckoning fallback only (no route replay, no new
  sensors). Reuses the existing EKF fused pose.
- Policy: a bounded dead-reckoning budget (`max_dr_time_s` and
  `max_dr_distance_m`). Within budget → keep driving on the fused pose and warn.
  Beyond budget → safe-stop (the geofence OUT_OF_FIX stop remains as a backstop).
- Operator notification: surface the degraded/lost state on the HMI/web via a
  status message (badge). No audio/TTS in this iteration.
- Simulation: Gazebo (existing `golfcart_gazebo` course world + `gps_dropout_node.py`).
- Safety: the DR fallback is a **policy**, not a bypass — the Safety Controller
  still arbitrates all motion. The geofence OUT_OF_FIX stop stays as a backstop.

---

## Current state (verified)

- **Fusion exists** (`golfcart_localization`):
  - `sensor_fusion_node` bridges `ImuData`→`sensor_msgs/Imu` (`/imu/data_raw`)
    and `GpsFix`→`sensor_msgs/NavSatFix` (`/gps/fix_std`).
  - `robot_localization` EKF (`config/ekf.yaml`) fuses wheel odom + IMU + GPS →
    `/odometry/filtered` (nav_msgs/Odometry) + TF `odom→base_link`.
  - `wheel_odometry_node` publishes `/wheel/odometry` from `/motor/state`.
- **Quality detection exists** (`localization_quality_node`):
  - Subscribes `/odometry/filtered`, reads the position covariance (indices 0, 7),
    publishes `NavigationStatus` on `/localization/quality` with state `OK` /
    `DEGRADED` (covariance > `max_position_covariance`).
  - **Gap:** it has no `LOST` state and no notion of *time since GPS was last
    valid* — it only reacts to covariance, which can stay low for a while after
    GPS drops (odom/IMU drift is slow to inflate covariance).
- **Geofence backstop** (`geofence_node`):
  - Subscribes `/gps/fix`; if no valid fix for `gps_timeout_s`, publishes
    `OUT_OF_FIX` and (autonomous only) a priority-3 zero `MotionRequest` → safe stop.
  - **Gap:** this stops the trolley immediately on GPS loss. The DR fallback
    needs to *suppress* this stop while within the DR budget, then let it fire
    (or fire its own stop) when the budget is exhausted.
- **Messages** (`golfcart_msgs`): `NavigationStatus.msg`, `GpsFix.msg`,
  `GeofenceStatus.msg`, `MotionRequest.msg` all exist.
- **Simulation**: `golfcart_gazebo` course world + `gps_dropout_node.py`
  (simulates GPS loss by dropping fixes on a schedule) — already used by summon.

### Key gaps for DR fallback (what this plan fills)
1. **No `LOST` state / no GPS-age tracking** in `localization_quality_node`.
2. **No DR budget enforcement** — nothing bounds how long/far the trolley may
   drive on dead reckoning before stopping.
3. **No operator notification** of the degraded/lost state (beyond the existing
   DEGRADED covariance flag).
4. **Geofence stops immediately on GPS loss** — needs to be suppressed during
   the DR budget window.

---

## Architecture

```text
/gps/fix (GpsFix)  ──┐
/odometry/filtered ──┤
                     ↓
   localization_quality_node  →  /localization/quality (OK / DEGRADED / LOST)
                     ↓
   dead_reckoning_node (policy)
        │   ├─ GPS OK        → normal operation (no DR)
        │   ├─ GPS DEGRADED  → warn operator; keep driving on fused pose
        │   └─ GPS LOST      → drive on dead-reckoning for a bounded
        │                      time/distance, then safe-stop if not recovered
        ↓  /localization/quality (LOST) + /dead_reckoning/status (operator)
        ↓  /geofence/status (suppress OUT_OF_FIX stop during DR budget)
```

### New message: `DeadReckoningStatus.msg`
Published on `/dead_reckoning/status` for the HMI/web badge.

```text
# Dead-reckoning fallback status from dead_reckoning_node.
# Informs the operator how long the trolley can keep driving without GPS.
string state            # OK / DEGRADED / DRIVING_DR / DR_BUDGET_EXCEEDED
float64 budget_time_s   # configured max DR time (s)
float64 budget_distance_m  # configured max DR distance (m)
float64 used_time_s     # DR time used so far (s)
float64 used_distance_m # DR distance travelled so far (m)
float64 remaining_time_s    # budget remaining (s)
float64 remaining_distance_m # budget remaining (m)
builtin_interfaces/Time timestamp
```

### `localization_quality_node` changes
- Add a `LOST` state: track the age of the last **valid** GPS fix (subscribe
  `/gps/fix`). If `fix_age > gps_timeout_s` → `LOST` (in addition to the
  covariance-based `DEGRADED`).
- Publish `OK` / `DEGRADED` / `LOST` on `/localization/quality`.

### New node: `dead_reckoning_node`
- Subscribes `/localization/quality` (state), `/odometry/filtered` (pose for
  distance integration), `/gps/fix` (to detect recovery).
- State machine:
  - `OK` → no DR. Reset budget counters.
  - `DEGRADED` → warn operator (status `DEGRADED`), keep driving.
  - `LOST` → enter DR. Integrate distance from `/odometry/filtered` deltas.
    While `used_time < max_dr_time_s` **and** `used_distance < max_dr_distance_m`:
    status `DRIVING_DR`, keep driving (do nothing to motion — the fused pose
    still feeds navigation). When either budget is exhausted → status
    `DR_BUDGET_EXCEEDED` and publish a priority-3 zero `MotionRequest` (safe
    stop) — or rely on the geofence backstop.
  - Recovery: when `/gps/fix` becomes valid again → back to `OK`, reset.
- Publishes `/dead_reckoning/status` and (on budget exceeded) a priority-3 stop
  `MotionRequest` on `/motion/request`.

### `geofence_node` changes (suppress OUT_OF_FIX stop during DR)
- Subscribe `/dead_reckoning/status`. While the DR node reports
  `DRIVING_DR` (within budget), the geofence **suppresses** its `OUT_OF_FIX`
  priority-3 stop (it still publishes `OUT_OF_FIX` for the operator, but does
  not force a stop). When DR reports `DR_BUDGET_EXCEEDED` (or `OK`/`DEGRADED`
  with no DR), the geofence resumes its normal stop behavior.
- This keeps the geofence as the backstop: if the DR node is absent or fails,
  the geofence still stops on GPS loss.

### Config (`config/golfcart.yaml`)
Add a `dead_reckoning_node` section:
```yaml
dead_reckoning_node:
  max_dr_time_s: 30.0        # max seconds to drive on dead reckoning
  max_dr_distance_m: 50.0    # max meters to drive on dead reckoning
  stop_priority: 3           # priority of the budget-exceeded stop request
```
Add `gps_timeout_s` to `localization_quality_node` (it needs it for the LOST
state; default 3.0, matching the geofence).

### Launch (`fusion.launch.py`)
Add the `dead_reckoning_node` to the localization launch, using `_node_params`.

---

## Safety notes
- The DR fallback is a **policy layer**, not a bypass. All motion still flows
  through the Safety Controller.
- The budget bounds how far the trolley can be from its true position before
  stopping (drift accumulates on dead reckoning).
- The geofence `OUT_OF_FIX` stop remains as a backstop if the DR node is absent
  or fails.
- The budget-exceeded stop is a priority-3 request (same as the geofence stop),
  so it overrides autonomous (0) and manual (1) requests.

## Effort
Medium. A new `dead_reckoning_node` + a `DeadReckoningStatus.msg` + small
changes to `localization_quality_node` (LOST state) and `geofence_node`
(suppress during DR). Pure math (budget integration) in a `dead_reckoning_math.hpp`
for unit testing.

## Validation
- Unit test for the DR budget math (time/distance integration, budget-exceeded
  decision).
- Build + existing tests pass.
- Headless Gazebo smoke test using `gps_dropout_node.py` to drop GPS and verify
  the trolley keeps driving within budget, then stops.