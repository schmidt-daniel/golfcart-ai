# Plan: Summon (Drive to Operator's Phone Position)

**TL;DR** — Build the Summon feature on top of the already-implemented autonomous
navigation stack. The trolley drives to the operator's **live phone GPS position**
instead of a fixed waypoint. This adds: (1) a `summon_node` that tracks the phone
target and re-issues goals as the operator moves, (2) a phone-side web page that
streams the browser's GPS position and triggers summon, (3) safety hardening
specific to driving toward a person (target-loss stop, max distance, speed limit,
timeout), and (4) validation in Gazebo. The existing `/set_goal_geo` service,
`navigation_node`, Nav2 stack, and web app are reused.

**User decisions (confirmed)**
- Scope: Summon only (no route replay — removed; no follow-me).
- Target source: phone browser geolocation streamed over the network.
- Trigger: a "SUMMON" button on the phone web page.
- **Targeting mode: selectable** — the operator chooses between two modes:
  - **Current position** — aim at the operator's live position (simple, no lead).
  - **Predictive / intercept** — estimate the operator's velocity (direction +
    speed) and aim at their **predicted future position** at the trolley's ETA,
    so the trolley meets them instead of chasing them.
  The mode is chosen by the operator (web toggle) and passed to `summon_node`.
- Simulation: Gazebo (existing `golfcart_gazebo` course world).
- Safety: target-loss stop, max summon distance, slow speed, timeout, physical
  stop override, obstacle handling via existing Nav2 + operator prompt.

---

## Current state (verified)

- **Navigation stack is implemented and validated in sim** (`docs/features/navigation.md`):
  - `navigation_node` (`src/golfcart_navigation/src/navigation_node.cpp`) — bridges
    Nav2 `cmd_vel` → `MotionRequest` (priority 0), exposes `/set_goal` service
    (takes `GoalPose` in map frame) → forwards to Nav2 `navigate_to_pose` action,
    publishes `NavigationStatus` (IDLE/PLANNING/DRIVING/PAUSED/STOPPED/ARRIVED/ERROR).
  - `georeference_node` (`src/golfcart_navigation/src/georeference_node.cpp`) —
    exposes `/set_goal_geo` service (takes lat/lon in `GoalPose.x/y`) → converts to
    map-frame via map origin (lat/lon + rotation) → forwards to `/set_goal`.
  - Nav2 servers launched by `launch/navigation.launch.py` (planner, controller,
    bt_navigator, velocity_smoother, global/local costmaps).
- **Web app** (`src/golfcart_teleop/web/index.html`) already has:
  - SUMMON button wired to `/set_goal_geo` with a **tapped** map target.
  - Browser geolocation (`navigator.geolocation.watchPosition`) tracking the phone.
  - Trolley marker from `/gps/fix`, target marker, navigation status display,
    CANCEL button, obstacle-interaction modal (placeholder).
- **HMI** (`src/golfcart_teleop/golfcart_teleop/hmi_node.py`) — scaffold with a
  `navigate_to_target(x, y, theta)` that calls `/set_goal` (map frame). Not the
  primary summon surface (phone is), but kept consistent.
- **Messages** (`golfcart_msgs`): `GoalPose.msg`, `NavigationStatus.msg`,
  `SetGoal.srv` all exist.
- **Simulation** (`golfcart_gazebo`): course world with LiDAR/IMU/GPS/camera,
  `ros_gz_bridge` publishes `/gps` (`gps_msgs/GPSFix`), `/scan`, `/imu`, `/clock`.
- **Deployment**: systemd units + `scripts/deploy.sh` (Option D hybrid).

### Key gaps for Summon (what this plan fills)
1. **No live-target tracking.** The web app sends a one-shot tapped target. Summon
   needs to continuously re-target the operator's **moving** phone position.
2. **No predictive/intercept targeting.** If the operator is walking, aiming at
   their *current* position makes the trolley chase them forever. Predictive mode
   estimates their velocity and aims at their predicted position at the trolley's
   ETA, so the trolley meets them.
3. **No phone-side "summon me" flow.** The current page is a teleop page; summon
   needs a dedicated phone view that streams GPS and triggers the drive.
4. **No summon-specific safety.** Target-loss stop, max distance, timeout, and a
   summon speed limit are not enforced anywhere.
5. **No GPS-dropout simulation** in Gazebo to validate target-loss handling.

---

## Architecture

```text
Phone browser (geolocation)
        ↓  HTTP/WebSocket (phone GPS stream)
   summon_node (tracks live target)
        │   ├─ mode = CURRENT  → aim at live position
        │   └─ mode = PREDICT → estimate velocity, aim at predicted position at ETA
        ↓  /set_goal_geo (lat/lon)  [re-issued as operator moves]
   georeference_node (lat/lon → map frame)
        ↓  /set_goal (map frame)
   navigation_node (Nav2 bridge)
        ↓  navigate_to_pose action
   Nav2 (planner + controller + costmaps)
        ↓  cmd_vel → MotionRequest (priority 0)
   Safety Controller (arbitration)
        ↓
   ODrive
```

Summon is a **use case on top of autonomous navigation**. The only difference from
a fixed waypoint is that the target is the operator's **live phone position** rather
than a stored/tapped waypoint. All the machinery (localization, mapping, Nav2,
obstacle avoidance, priority arbitration) is reused unchanged.

### Targeting modes

**Current-position mode** aims the goal at the operator's latest GPS fix. Simple and
predictable, but if the operator is walking, the trolley plays catch-up and may
never actually reach them.

**Predictive / intercept mode** adds a **lead** on the target. `summon_node`
estimates the operator's velocity $\vec{v}_{op}$ from recent GPS fixes, estimates the
trolley's time-of-arrival (ETA), and aims at the predicted future position:

$$\text{target}_{pred} = \text{pos}_{op} + \vec{v}_{op} \cdot \text{ETA}$$

where $\text{ETA} \approx \text{distance}_{trolley\to target} / v_{trolley}$. The
lead and the ETA are **coupled** (the lead depends on how long the trolley takes to
get there, which depends on where it aims), so each re-target cycle does a small
fixed-point iteration (a few iterations of the above equation) to converge. The
predicted target is clamped to a max lead distance and to the course boundary so it
never aims off-course or beyond a safe range.

Both modes share the same re-target loop, state machine, and safety checks; only the
target computation differs. The operator selects the mode (web toggle) and it is
passed to `summon_node` at start (and can be changed mid-summon).

**Predictive mode includes a "hold if approaching" rule.** If the operator is walking
directly toward the trolley, the trolley should **not move at all** — it waits for the
operator to arrive. Otherwise (moving away, sideways, or stationary), it does a
predictive intercept. Concretely, each re-target cycle:
1. Compute the operator's velocity $\vec{v}_{op}$ and the vector from the trolley to
   the operator, $\vec{d}$.
2. If the operator is moving **toward** the trolley — i.e. $\vec{v}_{op}$ points within
   an **approach cone** (angle between $\vec{v}_{op}$ and $\vec{d}$ < `approach_cone_deg`,
   default ~25°) **and** the operator's closest-approach distance to the trolley is
   within `approach_pass_m` (default ~2 m, so they'll actually reach it, not pass
   beside) — then **hold position** (no goal issued / cancel active goal) and wait.
3. Otherwise (moving away, sideways, or stationary with $\vec{v}_{op} \approx 0$),
   do the predictive intercept as above.
4. **Waiting fallback:** if the operator is approaching but hasn't arrived within
   `approach_timeout_s` (default 30 s) or the distance isn't shrinking fast enough,
   switch to predictive intercept so the trolley doesn't wait forever.

This avoids the awkward "both moving toward each other" dance and saves battery/wear
when the operator is already coming. It is a **rule inside predictive mode**, not a
third mode — the web toggle stays two options (`CURRENT` / `PREDICT`).

**Predictive mode is reactive, not predictive of intent.** The velocity estimate is
recomputed continuously from recent GPS fixes, so a change in the operator's
direction or speed is picked up automatically on the next re-target cycle (~1 s).
However, the prediction only **extrapolates current motion** — it cannot anticipate a
future turn or stop. If the operator turns or stops, the trolley re-aims within about
a second, but with a brief lag while the new velocity is detected and the target
recomputed. This is inherent to any intercept/pursuit algorithm.

**Velocity-estimate lag vs. noise tradeoff.** The operator's velocity is low-pass
filtered over recent samples to reject GPS noise. This creates a tunable tradeoff:
more filtering gives a smoother estimate but reacts more slowly to direction/speed
changes; less filtering reacts faster but is noisier. The filter window is a
parameter (`velocity_filter_window_s`). The re-target hysteresis (~1 m) also means
tiny movements don't trigger a re-issue, but any meaningful direction/speed change
exceeds it and triggers an update.

---

## Phases

### Phase 0 — Messages & service extensions (no new deps)
1. **`SummonStatus.msg` is the single source of truth for summon state.** Do NOT add
   a `summon_state` field to `NavigationStatus` — that stays for navigation state
   only. (Decision #10: avoid two sources of truth.)
2. **Add a `SummonStatus.msg`** (new) published by `summon_node` on `/summon/status`:
   - `bool active`, `string state`, `float64 target_lat`, `float64 target_lon`,
     `float64 distance_m`, `float32 progress` (0–1 fraction of route completed),
     `builtin_interfaces/Time timestamp`.
   - This gives the phone page a single topic to render summon state.
   - **Route polyline:** the web app subscribes to Nav2's `/plan` topic
     (`nav_msgs/Path`) to draw the planned route; `SummonStatus` carries only the
     progress fraction + distance. (Decision #1 proposal.)
2b. **Add a `PhoneFix.msg`** (new) — a lightweight custom message for the phone's
    position, matching what the browser geolocation actually provides:
    `float64 latitude_deg`, `float64 longitude_deg`, `float64 accuracy_m`,
    `float64 speed_mps`, `float64 heading_deg`, `bool valid`,
    `builtin_interfaces/Time timestamp`. (Decision: custom `PhoneFix` rather than
    `gps_msgs/GPSFix` — lighter and matches the browser data.)
3. **Add `SummonTrigger.srv`** (new) — a single service with a flag (Decision #5):
   - Request: `float64 lat`, `float64 lon`, `string mode` (`CURRENT`/`PREDICT`),
     `bool cancel`.
   - `summon_node` exposes one service `/summon`. `cancel=false` starts/restarts
     summon; `cancel=true` cancels it.
   - (Alternative: reuse `/set_goal_geo`; but a dedicated service lets `summon_node`
     own the summon state machine and safety checks. Decision: dedicated service.)
   - The `mode` field lets the operator choose current vs. predictive targeting.

### Phase 1 — `summon_node` (C++, new node in `golfcart_navigation`)
4. **Create `src/golfcart_navigation/src/summon_node.cpp`** — the core of the feature.
   Responsibilities:
   - **Subscribe to the phone GPS stream** (topic `/phone/gps`, `golfcart_msgs/PhoneFix`).
     The phone page publishes its position here.
   - **Subscribe to the trolley's fused pose** (`/odometry/filtered`, `nav_msgs/Odometry`)
     for ETA and max-distance math (map frame, fused — better than raw `/gps/fix`).
   - **Track the live target**: maintain the latest phone lat/lon with a timestamp.
   - **Accuracy gate**: only accept a phone fix if `accuracy_m <= 2.5` (Decision #8);
     otherwise treat it as invalid (no summon / target-loss).
   - **Estimate operator velocity** (predictive mode only): from a short history of
     recent GPS fixes, compute the operator's heading and speed (e.g. a least-squares
     fit or a simple finite-difference over the last N samples, low-pass filtered to
     reject GPS noise).
   - **Compute the target** based on the selected mode:
     - `CURRENT` → use the latest phone lat/lon directly.
     - `PREDICT` → estimate ETA from trolley position/speed, then aim at
       `pos_op + v_op * ETA`, iterating a few times to converge (lead/ETA coupling),
       clamped to `max_lead_m` and the course boundary.
   - **Frame reconciliation for the "hold if approaching" check** (Decision #3
     proposal): convert the operator's velocity (computed in ENU from lat/lon) into
     the map frame using the map-origin rotation (same transform as
     `georeference_node`), then compare against the trolley's map-frame position.
     All vectors are compared in the map frame.
   - **Re-target loop**: while summon is active, periodically (e.g. 1 Hz) convert the
     computed target lat/lon → map frame via the `/set_goal_geo` service and re-issue
     the goal to Nav2 (via `/set_goal`). Only re-issue if the target moved beyond a
     small hysteresis (e.g. 1 m) to avoid spamming Nav2.
   - **State machine**: IDLE → TRACKING → DRIVING → (ARRIVED | TARGET_LOST |
     TIMEOUT | CANCELLED | ERROR). Publish `SummonStatus` on `/summon/status`.
   - **Safety checks** (see Phase 2).
   - **Mode switching**: accept a mode change mid-summon (via the `SummonTrigger`
     request) and recompute the target on the next cycle.
   - **Re-trigger while active**: a new `/summon` call with `cancel=false` **restarts**
     summon (Decision #7).
   - **Already at the trolley**: if the operator is within `arrival_radius_m` when
     summon is triggered, **refuse** (return an error) (Decision #6).
   - **Network loss**: if the phone connection drops (no `/phone/gps` for
     `target_timeout_s`), drive to the **last planned location** and send a
     notification to the phone (Decision #9).
4b. **Extract pure math into a testable header** (Decision #4 proposal): put the
    velocity estimation, predictive-target computation, and approach-cone check into
    a standalone `summon_math.hpp` with no ROS dependencies, so they can be
    unit-tested directly with gtest.
5. **Register `summon_node`** in `CMakeLists.txt` (add executable + install) and in
   `launch/navigation.launch.py` (or a new `summon.launch.py`).

### Phase 2 — Summon safety (in `summon_node`)
6. **Target-loss**: If no phone GPS message for `target_timeout_s` (e.g. 3 s),
   or the phone reports `valid=false`, transition to `TARGET_LOST` and **cancel the
   Nav2 goal** (call `/safety/stop` or cancel the action). Never drive blind.
7. **Max distance**: if the straight-line distance from the trolley's current
   position to the phone target exceeds `max_summon_distance_m` (e.g. 100 m), refuse
   to start (or stop if it grows beyond a margin). Prevents unsupervised long drives.
8. **Timeout**: if summon has been active longer than `summon_timeout_s` (e.g. 120 s)
   without arriving, transition to `TIMEOUT` and stop.
9. **Speed limit**: summon uses a reduced max speed. **Decision:** do NOT use a
   dynamic velocity_smoother override (dropped). Instead, set a fixed reduced max
   speed in the Nav2 `velocity_smoother` config that applies during summon (or rely
   on the global max and document summon as slow). **Battery-aware summon is NOT
   needed** — distances are short (≤ `max_summon_distance_m`).
10. **Arrival**: summon is complete when the trolley is within `arrival_radius_m`
    (default 2 m) of the target. In predictive mode, "arrived" means it reached the
    predicted (intercept) point. **Decision:** arrival radius = 2 m.
11. **Speed down on arrival**: slow the trolley as it approaches the target
    (`approach_slowing_radius_m`, e.g. 5 m) so it eases up to the operator rather than
    stopping abruptly.
12. **Arrival confirmation**: when the trolley arrives, signal the operator — an
    **arrival notification on the phone** and/or the **trolley's HMI** — and wait for
    the operator to acknowledge before going idle (prevents immediate re-targeting if
    the operator is still moving).
13. **Physical stop override**: the existing priority arbitration already ensures a
    manual `MotionRequest` (priority 1) overrides summon (priority 0). The web
    STOP/CANCEL buttons call `/safety/stop`. No new work needed beyond wiring
    `/summon/cancel` to also cancel the Nav2 goal.
14. **Obstacle handling**: reuse the existing Nav2 obstacle costmap + Nav2. If Nav2
    cannot find a path, it stops. **Decision:** the operator is **informed via a
    message on the phone** (`SummonStatus` → `OBSTACLE` state + a notification on the
    phone page), rather than a silent stop-and-wait. The phone page shows the
    retry/replan/stop options.
15. **Final-approach terrain awareness**: handled by the existing **forbidden zones**
    in the `CourseMap` costmap (and future image recognition), not by a separate
    path-aware target bias. No new work beyond what navigation already does.

Note: accuracy gating (≤ 2.5 m), already-at-trolley refusal, restart-on-retrigger,
and network-loss behavior are defined in Phase 1 item 4.

### Phase 3 — Phone-side web page (new `summon.html` + server)
16. **Create `src/golfcart_teleop/web/summon.html`** — a dedicated, phone-optimized
    page:
    - Uses `navigator.geolocation.watchPosition` (high accuracy) to get the phone GPS.
    - **Publishes the phone position** to `/phone/gps` (`golfcart_msgs/PhoneFix`) via
      rosbridge at ~1 Hz.
    - **Mode toggle**: a control to select **Current position** vs **Predictive
      (intercept)** targeting, passed to `/summon/start` (and changeable mid-summon).
    - **SUMMON button**: calls `/summon/start` with the caller's current lat/long and
      the selected mode.
    - **CANCEL / STOP button**: calls `/summon/cancel` and `/safety/stop`.
    - **Status display**: subscribes `/summon/status` and `/navigation/status` to show
      TRACKING / DRIVING / TARGET_LOST / TIMEOUT / ARRIVED / CANCELLED, plus distance
      and the active mode.
    - **Route + progress display**: subscribe to Nav2's `/plan` topic
      (`nav_msgs/Path`) and **draw the planned route onto the map**, with a progress
      indicator (fraction + distance) from `SummonStatus`.
    - **Accuracy warning**: if the phone GPS accuracy exceeds 2.5 m, warn the
      operator that summon may be refused.
    - **Arrival notification**: when `SummonStatus` reports `ARRIVED`, notify the
      operator on the phone and on the HMI. **(Decision #11)**
    - **Obstacle notification**: when `SummonStatus` reports `OBSTACLE`, show a
      prominent message on the phone (per decision #14) with retry/replan/stop options.
    - Minimal Leaflet map showing phone + trolley + target + planned route.
17. **Serve `summon.html`**: the existing `web_teleop_server.py` serves the whole
    `web/` directory, so `summon.html` is served automatically at
    `http://<host>:8080/summon.html`. No new server change needed (verify).
18. **Keep `index.html` consistent**: the existing SUMMON button can remain as a
    "set tapped target" flow, but the primary summon/reopen UX moves to `summon.html`.
    Optionally add a link between the two pages. Add route-progress display here too.

### Phase 4 — HMI (minor)
19. **`hmi_node.py`**: add `summon_to(lat, lon)` that calls `/summon/start`, and
    show the **arrival confirmation** and summon status on the HMI. The phone is the
    primary summon surface, but the HMI shows summon status/arrival for the operator
    on the trolley. (Decision #11)

### Phase 5 — Gazebo validation (incl. GPS dropout)
20. **Add GPS-dropout simulation** to `golfcart_gazebo` so target-loss handling can
    be tested:
    - Add a small node or launch-time param that intermittently drops `/gps` (or
      `/phone/gps`) messages. Simplest: a `gps_dropout_node` (test-only) that
      forwards `/gps` but suppresses output for configurable windows.
    - Alternatively, simulate phone GPS loss by stopping the phone page's
      `/phone/gps` publishing (manual test).
21. **Sim test scenario** (`scripts/summon_check.sh` or extend `sim_check.sh`):
    - Launch sim + navigation + summon.
    - Simulate a phone target (publish `/phone/gps` at a known lat/lon).
    - Trigger `/summon/start`; verify the trolley drives toward the target.
    - Move the phone target; verify the trolley re-targets.
    - Stop publishing `/phone/gps`; verify `TARGET_LOST` and the trolley stops.
    - Verify manual override (joystick/web) stops summon.
    - Verify max-distance refusal and timeout.
    - **Validation is sim-only for now** (decision #7); real-hardware validation is
      deferred until the real GPS/IMU/LiDAR drivers are implemented.

### Phase 6 — Deployment
22. **systemd**: add a `golfcart-summon.service` (or fold `summon_node` into the
    existing `golfcart-navigation.service` launch). Decision: fold into the
    navigation launch so summon is available whenever navigation is up; no new unit
    needed. The phone page is served by the existing web server.
23. **`scripts/deploy.sh`**: no change needed (code via git, web/ is part of the
    teleop package). Verify `summon.html` is installed with the web/ directory.

---

## Relevant files

- `src/golfcart_msgs/msg/SummonStatus.msg` (new) — summon state + target + distance.
- `src/golfcart_msgs/srv/SummonTrigger.srv` (new) — start/cancel summon with lat/lon.
- `src/golfcart_msgs/msg/NavigationStatus.msg` — add `summon_state` field (optional).
- `src/golfcart_navigation/src/summon_node.cpp` (new) — live-target tracking +
  summon state machine + safety.
- `src/golfcart_navigation/src/summon_math.hpp` (new) — pure math (velocity
  estimation, predictive target, approach-cone) for unit testing.
- `src/golfcart_navigation/CMakeLists.txt` — add `summon_node` executable + install.
- `src/golfcart_navigation/launch/navigation.launch.py` — add `summon_node`.
- `src/golfcart_teleop/web/summon.html` (new) — phone-optimized summon page.
- `src/golfcart_teleop/web/index.html` — keep tapped-target SUMMON; add link to
  `summon.html`; wire obstacle modal to summon status.
- `src/golfcart_teleop/golfcart_teleop/hmi_node.py` — add `summon_to(lat, lon)`.
- `src/golfcart_gazebo/` — optional `gps_dropout_node` for target-loss testing.
- `scripts/summon_check.sh` (new) — sim validation script.
- `docs/features/summon.md` — update status from Planned → Implemented.
- `docs/architecture.md` — document summon node, phone GPS topic, safety rules.
- `FEATURES.md` — move Summon from "Not yet implemented" to "Implemented".

---

## Verification

1. `colcon build` + `colcon test` — all packages build; unit tests pass
   (summon state machine, target-loss logic, max-distance check, timeout,
   velocity estimation, predictive-target computation).
2. Gazebo: launch sim + navigation + summon; publish a phone target; verify the
   trolley drives to it; move the target and verify re-targeting; drop the phone
   GPS and verify `TARGET_LOST` stop; verify manual override stops summon; verify
   max-distance refusal and timeout.
3. **Mode comparison**: with the operator walking at a constant speed, verify that
   **predictive mode** reaches the operator (intercepts) while **current mode**
   trails behind — demonstrating the lead works.
4. Web: open `summon.html` on a phone/desktop; verify the mode toggle, SUMMON
   button triggers the drive, status updates, and CANCEL stops it.
5. RViz: visualize the trolley driving to the moving target (and the predicted
   target in predictive mode).

---

## Decisions

- **Summon is a use case on top of navigation**, not a standalone feature — reuse
  the existing Nav2 stack, `/set_goal_geo`, and priority arbitration unchanged.
- **Live target via a dedicated `/phone/gps` topic** published by the phone page
  (rosbridge), consumed by `summon_node`. Clean separation from the trolley's own
  `/gps/fix`.
- **Phone GPS message type:** custom `golfcart_msgs/PhoneFix` (lat/lon/accuracy/
  speed/heading/valid/timestamp) rather than `gps_msgs/GPSFix` — lighter and matches
  the browser geolocation data. (Decision #1)
- **Trolley position source:** `/odometry/filtered` (EKF, map frame) for ETA and
  max-distance math; `/gps/fix` only for the web map display. (Decision #2)
- **Single `/summon` service with a flag** (Decision #5): `cancel=false` starts/
  restarts summon; `cancel=true` cancels. `summon_node` owns the summon state machine
  and safety checks (vs. reusing `/set_goal_geo` directly).
- **Selectable targeting mode** — `CURRENT` (aim at live position) or `PREDICT`
  (aim at predicted position at ETA). Chosen by the operator via the web toggle,
  passed in `SummonTrigger.mode`, changeable mid-summon.
- **Predictive mode** estimates operator velocity from recent GPS fixes (low-pass
  filtered to reject noise), computes ETA from trolley position/speed, and aims at
  `pos_op + v_op * ETA` with a few fixed-point iterations to resolve the lead/ETA
  coupling. The predicted target is clamped to `max_lead_m` and the course boundary.
- **"Hold if approaching" rule (inside predictive mode):** if the operator is walking
  toward the trolley (within `approach_cone_deg` ≈ 25° and closest approach
  ≤ `approach_pass_m` ≈ 2 m), the trolley holds position and waits, with a fallback to
  predictive intercept after `approach_timeout_s` ≈ 30 s so it never waits forever.
  This keeps the web toggle at two options (`CURRENT` / `PREDICT`).
- **Re-target with hysteresis** (re-issue goal only when the target moved > ~1 m) to
  avoid spamming Nav2 with near-identical goals.
- **Safety rules (all in `summon_node`, tunable params):**
  - `target_timeout_s` (default 3 s) → `TARGET_LOST` stop.
  - `max_summon_distance_m` (default 100 m) → refuse/stop.
  - `summon_timeout_s` (default 120 s) → `TIMEOUT` stop.
  - `max_speed_mps` → reduced summon speed, set as a fixed max in the Nav2
    `velocity_smoother` config (no dynamic override — dropped).
  - `arrival_radius_m` (default 2 m) → summon complete when within this radius of
    the target. (Decision #3)
  - `approach_slowing_radius_m` (default 5 m) → slow down as the trolley approaches
    the target, easing up to the operator. (Speed scaling on arrival)
  - Manual override always wins (existing priority arbitration).
- **Battery-aware summon is NOT needed** — distances are short (≤
  `max_summon_distance_m`). (Decision #5)
- **No "return home"** — there is no fixed home location; summon is point-to-point
  to the operator. (Decision #6)
- **No multi-target** — a trolley is not shared; summon targets a single operator's
  phone. (Decision #8)
- **Route + progress display:** the web app draws the planned route from Nav2's
  `/plan` topic and shows progress (fraction + distance) from `SummonStatus`.
- **Arrival notification:** the operator is notified on the **phone and the HMI**
  when the trolley arrives, and the trolley waits for acknowledgment before going
  idle. (Decision #11)
- **Obstacle mid-route:** the operator is **informed via a message on the phone**
  (`SummonStatus` → `OBSTACLE` + phone notification) with retry/replan/stop options,
  rather than a silent stop-and-wait. (Decision #6)
- **Final-approach terrain awareness:** handled by the existing **forbidden zones**
  in the `CourseMap` costmap (and future image recognition), not a separate
  path-aware target bias.
- **Accuracy gate:** summon only works when the phone GPS is accurate to ≤ 2.5 m;
  otherwise refuse / treat as target-loss.
- **Already at the trolley:** if the operator is within `arrival_radius_m` when
  summon is triggered, refuse (return an error).
- **Re-trigger while active:** a new `/summon` call with `cancel=false` restarts
  summon.
- **Network loss:** on phone connection loss, drive to the last planned location and
  send a notification to the phone.
- **Frame reconciliation:** the "hold if approaching" check is done in the map frame
  (operator velocity converted from ENU via the map-origin rotation).
- **Testable math:** velocity estimation, predictive-target, and approach-cone logic
  live in a standalone `summon_math.hpp` (no ROS deps) for direct unit testing.
- **Single source of truth:** `SummonStatus.msg` carries summon state; `NavigationStatus`
  is not extended with a `summon_state` field.
- **Phone page is the primary summon surface**; HMI is kept consistent but
  low-priority.
- **Validation is sim-only for now**; real-hardware validation is deferred until the
  real GPS/IMU/LiDAR drivers are implemented. (Decision #7)
- **Deployment:** `summon_node` folds into the existing navigation launch; no new
  systemd unit. `summon.html` served by the existing web server.

---

## Out of scope (deferred)

- **Follow Me** (moving-target tracking with continuous following) — separate
  feature; summon is a "drive to the phone's position" one-shot/periodic re-target
  (with optional predictive lead), not continuous following.
- **Route replay** — removed as a feature.
- **Geofencing enforcement** — Nav2 costmap already handles course boundaries.
- **Speed-by-zone** — velocity smoother handles global max for now.
- **Real hardware validation** — depends on real GPS/IMU/LiDAR drivers (still
  scaffolds); summon is validated in simulation first.
