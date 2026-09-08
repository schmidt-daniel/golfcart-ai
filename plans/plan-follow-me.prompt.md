# Plan: Follow Me (Person Following)

**TL;DR** — Implement Follow Me on top of the existing sensor stack. The trolley
detects and follows a person **walking away in front of it** (follow-behind) using
**LiDAR first** (already wired in sim), behind a clean `PersonTarget` abstraction
so camera and LiDAR+camera fusion can be added later without rewriting the follow
controller. The follow controller converts the person's relative position into a
`MotionRequest` (priority 0, autonomous) that drives the trolley to maintain a set
following distance.

**User decisions (confirmed)**
- Sensor approach: **LiDAR first**, designed for later camera + fusion (see
  "Sensor approach" below).
- **Follow direction: follow behind** — the LiDAR is mounted at the front of the
  trolley; the trolley trails the operator walking away in front of it.
- **When the person stops:** the trolley **holds position**. If the person then
  walks *toward* the trolley (e.g. to grab a club from the bag), the trolley
  stays stopped; if the person continues walking away within 20 s, the trolley
  resumes following.
- **Obstacle handling is separate:** a dedicated obstacle-awareness node feeds
  the follow controller for **soft route replanning** (steer around), while the
  existing Safety Controller independently handles the **hard safety stop**
  (priority 3).
- **Packages are split:** `person_detection_node` in `golfcart_lidar`;
  `follow_controller_node` in a new `golfcart_follow` package.
- **`PersonTarget` has a `source` field** (for future LiDAR/camera fusion).
- **Lock-on acquisition:** when inactive, wait for the first person within
  `lock_distance_m` (2 m) and lock onto them. Once locked, continuously track that
  same person (continuity check) so the trolley never switches to a closer
  stranger. The lock is released when the target is lost.
- **Obstacle steering: potential-field repulsion** — obstacles exert a repulsive
  force on the desired heading (smooth avoidance).
- **Person leaves the forward sector:** the trolley **holds** (the LiDAR can't
  see behind the trolley anyway — the bag blocks the view).
- **After `resume_timeout_s` expires:** the trolley **stops and requires
  re-triggering** (safety).
- **Trigger/stop via web + HMI** (a `/follow` service, like summon's `/summon`).
- **`FollowStatus.msg`** for web/HMI display (like `SummonStatus`).
- **Sim needs a moving person** to properly test following.
- Simulation: Gazebo (existing `golfcart_gazebo` course world + `/scan`).
- Safety: stop on target loss, low confidence, unexpected approach, obstacle,
  stale data, or Safety Controller rejection.

---

## Sensor approach: LiDAR first, fusion-ready

**Recommendation:** start with LiDAR, design the architecture for fusion.

- **LiDAR** (`/scan`, `sensor_msgs/LaserScan`, 10 Hz, 360°, 30 m) is already
  wired in sim and already processed by `obstacle_detection_node`. It gives the
  person's **2D position directly** (distance + bearing), works in all lighting,
  and is cheap on the Pi. Limitation: 2D only — can't classify "person vs. pole"
  by shape alone (needs motion/leg-detection heuristics).
- **Camera** (`/camera/image`, 640×480, 10 Hz) is wired in sim but has **no
  person detector** yet. It can classify "is this a person" (the thing LiDAR
  can't do) but is lighting-dependent, heavier, and needs a real camera driver.
- **Fusion** (LiDAR + camera) is the best end state — LiDAR for precise 2D
  position/range, camera for classification/confidence — but is premature until
  each sensor's person detection works independently.

**Decision:** implement a LiDAR-based person detector first, behind the
`PersonTarget` abstraction from `docs/features/follow-me.md`. Camera and fusion
become drop-in later implementations behind the same interface. This follows the
existing design doc and avoids fusion complexity before either sensor works.

---

## Current state (verified)

- **`docs/features/follow-me.md`** already specifies the architecture:
  `Sensor Data → Person Detection → Person Tracking → Target Validation →
  Follow Controller → MotionRequest`, consuming an abstract `PersonTarget`
  (`distance_m`, `lateral_offset_m`, `relative_velocity_mps`, `confidence`,
  `timestamp`, `valid`). It explicitly says the initial implementation may detect
  a person's legs from LiDAR and must allow camera/fusion later.
- **LiDAR** (`golfcart_lidar`):
  - `lidar_node` publishes `sensor_msgs/LaserScan` on `/scan` at 10 Hz (360°,
    30 m range in sim).
  - `obstacle_detection_node` already consumes `/scan` and does range/angle
    analysis (stopping zone) — the pattern Follow-Me person detection extends.
  - `LidarSensor` interface + `MockLidarSensor` + `LidarSensorImpl` (scaffold).
- **Camera** (`golfcart_gazebo`): `/camera/image` (`sensor_msgs/Image`, 640×480,
  10 Hz) bridged from gz-sim. No person detector exists.
- **Control stack**: `MotionRequest` (priority 0 = autonomous) → Safety
  Controller → `Twist` → Motion Controller → ODrive. Manual override (priority 1)
  always wins.
- **Messages** (`golfcart_msgs`): `MotionRequest`, `Obstacle`, `ObstacleState`
  exist. No `PersonTarget` message yet.
- **Simulation**: `golfcart_gazebo` course world with LiDAR/camera/IMU/GPS.

### Key gaps for Follow Me (what this plan fills)
1. **No person detection** — nothing consumes `/scan` (or `/camera/image`) to find
   a person.
2. **No `PersonTarget` message** — the follow controller needs a standard target
   message.
3. **No follow controller** — nothing converts a person's relative position into
   a `MotionRequest`.
4. **No person tracking/validation** — target loss, confidence, stale-data, and
   unexpected-approach handling don't exist.
5. **No sim validation** — no way to place a person in the sim and verify the
   trolley follows.

---

## Architecture

```text
/scan (LiDAR)  ──┐
                 ├─> person_detection_node ──> /person/target (PersonTarget)
/camera/image ──┘        (LiDAR first; camera/fusion later)
                              ↓
                       follow_controller_node
                              ↓
                       MotionRequest (priority 0)
                              ↓
                       Safety Controller (arbitration)
                              ↓
                          ODrive
```

Follow Me is a **use case on top of the control stack**. The follow controller
emits autonomous `MotionRequest`s (priority 0); manual override (priority 1) and
safety (priority 3) always win. It does **not** use Nav2/GPS — it's a local,
sensor-relative following behavior (unlike Summon, which is GPS/Nav2-based).

---

## Phases

### Phase 0 — Messages (no new deps)
1. **Add `PersonTarget.msg`** to `golfcart_msgs`:
   - `float64 distance_m` — distance to the person.
   - `float64 lateral_offset_m` — lateral offset (left/right) of the person.
   - `float64 relative_velocity_mps` — closing/opening velocity.
   - `float32 confidence` — 0–1 detection confidence.
   - `string source` — detector that produced the target (`lidar`, `camera`, `fusion`).
   - `bool valid` — false if no valid target.
   - `builtin_interfaces/Time timestamp`.
   - This is the standard target consumed by the follow controller (per
     `follow-me.md`). The `source` field enables future LiDAR/camera fusion.
1b. **Add a `source` field to `Obstacle.msg`** (existing message) for consistency:
   - `string source` — detector that produced the obstacle (`lidar`, `camera`, `fusion`).
   - Same rationale as `PersonTarget.source`: enables future LiDAR/camera obstacle
     detection and fusion (relevant to the soft `obstacle_awareness_node`).
1c. **Add `FollowStatus.msg`** (new) published by `follow_controller_node` on
   `/follow/status`:
   - `bool active`, `string state` (IDLE / FOLLOWING / HOLDING / TARGET_LOST /
     STOPPED / ERROR), `float64 distance_m`, `float64 lateral_offset_m`,
     `float32 confidence`, `builtin_interfaces/Time timestamp`.
   - Lets the web/HMI show follow state (like `SummonStatus`).
1d. **Add `FollowTrigger.srv`** (new) — a single service with a flag (like
   `SummonTrigger`): `bool cancel` (false = start, true = stop).
   - `follow_controller_node` exposes `/follow`.
   - Callable from the web page and HMI.

### Phase 1 — LiDAR person detection (`golfcart_lidar`)
2. **Create `person_detection_node`** (C++) in `golfcart_lidar`:
   - Subscribes `/scan` (`LaserScan`), publishes `PersonTarget` on
     `/person/target`.
   - **Detection pipeline (per scan):**
     1. **Sector gate** — keep only returns in a forward sector (e.g. ±60°),
        within `min_range_m`..`max_range_m`.
     2. **Clustering** — group adjacent returns into clusters (connected by
        angular/range proximity). A person is a compact cluster; a wall is a
        long wide one.
     3. **Cluster filtering** — keep clusters whose width is person-like
        (e.g. 0.3–0.6 m) and reject too-wide (wall) or too-narrow (single pole)
        clusters. Compute each cluster's centroid → `distance_m` +
        `lateral_offset_m`.
   - **Leg-pair signature (disambiguation):** a person's two legs produce **two
     nearby clusters at the same range** that **alternate** over time (left leg
     forward, then right). Detect this "two-blob, alternating" signature to
     distinguish a person from a single static pole. (Per `follow-me.md` "detect
     a person's legs from LiDAR".)
   - **Motion-based disambiguation (primary):** a person **moves**; a pole/tree
     **doesn't**. Track clusters across successive scans (10 Hz) and prefer
     clusters whose position **moves coherently** over time. This is the
     strongest LiDAR-only signal for "person vs. static obstacle."
   - **Temporal validation:** require the target to be **consistently detected
     across N consecutive scans** before trusting it (rejects transient noise).
   - **Confidence:** 0–1, based on cluster compactness, leg-pair match, and
     motion consistency.
   - **Target validation:** reject targets too close (person approaching
     unexpectedly), too far, or too wide (wall/obstacle, not a person).
   - **Stale-data handling:** if no valid target for `target_timeout_s`, publish
     `valid=false`.
   - **Tracking/filter:** smooth the target with an EMA or Kalman filter for a
     stable `PersonTarget` (and to estimate `relative_velocity_mps`).
   - **Note on camera alternatives:** there is **no off-the-shelf 2D-LiDAR leg
     model**; the leg-pair + motion heuristic is the standard approach (patterned
     after the classic `laser_people_detection` ROS package). Camera-based
     detectors (OpenCV HOG, MediaPipe Pose, YOLO-nano) are the alternative path
     and are deferred to Phase 3.
3. **Register `person_detection_node`** in `golfcart_lidar/CMakeLists.txt` +
   `package.xml` (add `golfcart_msgs` dep if not present).

### Phase 2 — Follow controller (`golfcart_follow`)
4. **Create `follow_controller_node`** in a new `golfcart_follow` package:
   - Subscribes `/person/target` (`PersonTarget`), publishes `MotionRequest`
     (priority 0, source `follow_me`) on `/motion/request`.
   - **Control law (follow-behind):** maintain a set following distance
     (`follow_distance_m`, e.g. 1.5 m). If the person is farther → drive forward;
     if closer → stop or back off; if laterally offset → turn to center on the
     person.
   - **Speed scaling:** speed proportional to distance error (P-controller),
     clamped to `max_speed_mps`. Slow and cautious (like summon).
   - **Hold-on-stop behavior:** if the person's velocity → 0 (stopped), the
     trolley **holds position**. If the person then walks *toward* the trolley
     (closing), stay stopped. If the person resumes walking away within
     `resume_timeout_s` (20 s), resume following; otherwise stop following.
   - **Soft obstacle steering (potential-field repulsion):** subscribes
     `/obstacles/awareness`; each obstacle exerts a repulsive force on the
     desired heading, so the trolley steers around obstacles smoothly rather
     than stopping dead. The Safety Controller independently handles the hard
     stop (priority 3).
   - **Sector-exit hold:** if the person leaves the forward sector (or the LiDAR
     can't see them — e.g. behind the trolley, blocked by the bag), the trolley
     **holds** position.
   - **Resume timeout:** if the person doesn't resume walking away within
     `resume_timeout_s` (20 s), the trolley **stops and requires re-triggering**.
   - **Trigger/stop:** exposes `/follow` service (`FollowTrigger.srv`), callable
     from the web page and HMI. Publishes `FollowStatus` on `/follow/status`.
   - **Stop conditions** (per `follow-me.md`): stop if target lost, confidence
     below threshold, person approaches unexpectedly (distance drops fast),
     sensor data stale, or Safety Controller rejects.
   - **Manual override:** priority 0 means manual (priority 1) always wins; the
     controller just stops emitting when overridden.

### Phase 2b — Obstacle awareness (soft replanning)
5. **Create `obstacle_awareness_node`** (in `golfcart_lidar` or `golfcart_follow`):
   - Consumes `/scan`, publishes `/obstacles/awareness` — a **soft** obstacle
     view (positions of nearby obstacles) for the follow controller to steer
     around, distinct from the safety `/obstacles/state`.
   - **Separation rationale:** safety stops (priority 3, via Safety Controller);
     behavior steers (soft replanning). This lets Follow Me navigate around
     obstacles instead of stopping dead, and is reusable by other autonomous
     behaviors.

### Phase 3 — Camera detector (later, behind same interface)
6. **`camera_person_detection_node`** (future): consumes `/camera/image`, runs a
   person detector (e.g. OpenCV HOG / MediaPipe / lightweight YOLO), publishes
   `PersonTarget`. This is a **drop-in second detector** behind the same
   `/person/target` interface. **Deferred** — not in the initial scope (see Out
   of scope).

### Phase 4 — Sensor fusion (later)
7. **`person_fusion_node`** (future): fuses LiDAR + camera `PersonTarget`s (e.g.
   weighted by confidence) into a single target. **Deferred** — not in the
   initial scope.

### Phase 5 — Gazebo validation
8. **Sim person model — two levels:**
   - **Level 1 (essential): moving rigid person.** A person-like model (e.g. two
     leg cylinders) that **translates as a rigid body** via a script (`gz model`
     pose commands) — walks away, stops, turns. This exercises the **follow
     controller + motion tracking + hold-on-stop + resume**. The legs stay static
     relative to the model.
   - **Level 2 (nice-to-have): actuated legs.** A model with **two revolute-joint
     legs driven by a script to alternate** (left/right, right/left). This
     exercises the **leg-pair signature** detection. Only needed to validate the
     leg disambiguation heuristic specifically.
   - **Rationale:** gz-sim does rigid-body physics; legs don't swing unless
     actuated. Level 1 is sufficient to validate the *follow* behavior (we
     control the world, so we know the object is the "person"). Level 2 validates
     the *detection heuristic* (person vs. pole). Start with Level 1; add Level 2
     only if needed.
9. **Sim test scenario** (`scripts/follow_check.sh`):
   - Launch sim + follow stack.
   - Place a person target in front of the cart; verify `/person/target` is
     published with correct distance/offset.
   - Verify the trolley drives toward the person and stops at `follow_distance_m`.
   - Move the person; verify the trolley re-follows.
   - Stop the person; verify hold-on-stop + resume within `resume_timeout_s`.
   - Remove the person; verify the trolley stops (target lost).
   - Verify manual override stops following.

### Phase 6 — Deployment
10. **systemd**: add `golfcart-follow.service` (or fold into an existing launch).
    Decision: new `golfcart-follow.service` running the follow stack (detection +
    controller). The web/HMI can show follow status (optional).

---

## Relevant files

- `src/golfcart_msgs/msg/PersonTarget.msg` (new) — standard follow target (with `source`).
- `src/golfcart_msgs/msg/Obstacle.msg` — add `source` field (consistency with `PersonTarget`).
- `src/golfcart_msgs/msg/FollowStatus.msg` (new) — follow state for web/HMI.
- `src/golfcart_msgs/srv/FollowTrigger.srv` (new) — start/stop follow.
- `src/golfcart_lidar/src/person_detection_node.cpp` (new) — LiDAR person detector.
- `src/golfcart_lidar/CMakeLists.txt` — add `person_detection_node`.
- `src/golfcart_follow/` (new) — `follow_controller_node` + `obstacle_awareness_node` (+ package files).
- `src/golfcart_gazebo/` — add a **moving** person model to the course world for validation.
- `scripts/follow_check.sh` (new) — sim validation script.
- `systemd/golfcart-follow.service` (new) — deployment.
- `docs/features/follow-me.md` — update status from Planned → Implemented (LiDAR).
- `docs/architecture.md` — document follow-me node, `/person/target`, `/obstacles/awareness`, safety.
- `FEATURES.md` — move Follow Me from "Not yet implemented" to "Implemented".

---

## Verification

1. `colcon build` + `colcon test` — all packages build; unit tests pass
   (person-detection cluster logic, follow-controller control law, hold-on-stop,
   obstacle steering, stop conditions).
2. Gazebo: place a **moving** person in front of the cart; verify `/person/target`
   is published with correct distance/offset; verify the trolley drives to
   `follow_distance_m` and stops; move the person and verify re-following; stop
   the person and verify hold-on-stop + resume; remove the person and verify stop
   (target lost); verify manual override.
3. RViz: visualize `/scan`, the detected person target, and obstacle awareness.

---

## Decisions

- **LiDAR first, fusion-ready** — implement the LiDAR person detector behind the
  `PersonTarget` abstraction; camera and fusion are drop-in later implementations.
- **`PersonTarget.msg`** is the standard target consumed by the follow controller
  (per `follow-me.md`).
- **Person detection (LiDAR):** per-scan pipeline — sector gate → clustering →
  cluster filtering (person-like width) → centroid (distance + lateral offset).
  Disambiguation via **motion tracking** (a person moves; a pole doesn't) as the
  primary signal, plus the **leg-pair signature** (two nearby clusters at the
  same range that alternate over time). Temporal validation (N consecutive
  scans) + EMA/Kalman smoothing for a stable target and velocity estimate.
- **No off-the-shelf 2D-LiDAR leg model exists** — the leg-pair + motion heuristic
  is the standard approach (patterned after `laser_people_detection`). Camera
  detectors (HOG/MediaPipe/YOLO) are the deferred alternative path.
- **Follow direction:** follow behind — LiDAR at the front, trolley trails the
  operator walking away.
- **Hold-on-stop:** when the person stops, the trolley holds position; if the
  person walks toward the trolley, stay stopped; if they resume walking away
  within `resume_timeout_s` (20 s), resume following.
- **Obstacle handling is separate:** `obstacle_awareness_node` feeds the follow
  controller for **soft replanning** (steer around); the Safety Controller
  independently handles the **hard stop** (priority 3).
- **Obstacle steering method:** **potential-field repulsion** — obstacles exert a
  repulsive force on the desired heading (smooth avoidance).
- **Sector-exit hold:** if the person leaves the forward sector (or is blocked,
  e.g. behind the trolley), the trolley holds.
- **Resume timeout:** after `resume_timeout_s` (20 s), the trolley stops and
  requires re-triggering (safety).
- **Trigger/stop:** `/follow` service (`FollowTrigger.srv`) callable from web +
  HMI; `FollowStatus.msg` on `/follow/status` for display.
- **Follow controller:** P-controller on distance error, maintain
  `follow_distance_m`, clamp to `max_speed_mps`, emit `MotionRequest` priority 0.
- **Stop conditions:** target lost, low confidence, unexpected approach, stale
  data, Safety Controller rejection.
- **Packages split:** `person_detection_node` in `golfcart_lidar`;
  `follow_controller_node` + `obstacle_awareness_node` in `golfcart_follow`.
- **`PersonTarget.source`** and **`Obstacle.source`** fields enable future
  LiDAR/camera detection and fusion.
- **Deployment:** new `golfcart-follow.service` systemd unit.

---

## Out of scope (deferred)

- **Camera person detection** — a later drop-in detector behind the same
  `/person/target` interface (needs a real camera driver + detector model).
- **LiDAR + camera fusion** — the end goal, but deferred until each sensor's
  detection works independently. Fusion also fixes the orientation robustness
  problem below.
- **Real hardware validation** — depends on real LiDAR/camera drivers (still
  scaffolds); validated in simulation first.
- **Summon integration** — Follow Me and Summon are separate features; a
  summon→follow handoff is a future enhancement.

## Known limitation: person orientation (LiDAR-only)

- A **moving person walking sideways (90°)** is generally still detected and
  followed: the torso + one leg gives a ≥0.1 m cluster with motion, and the
  lock-on continuity keeps it on the same person. Requires `min_width_m` low
  enough to accept a single-leg span (sim uses 0.1).
- A **frozen/side-on person** (stopped at 90°) is fragile LiDAR-only: the visual
  view shows one leg (narrow cluster) and low motion → low confidence, so the
  target can be lost/held. The leg-pair heuristic is not met side-on.
- **Fix path:** camera fusion classifies "person" regardless of orientation
  (camera) while LiDAR gives position. Documented limitation for LiDAR-first;
  mitigated by the torso cross-section and low min_width.