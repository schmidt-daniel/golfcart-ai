# Follow Me

Follow Me consists of separate stages:

```text
Sensor Data
    ↓
Person Detection
    ↓
Person Tracking
    ↓
Target Validation
    ↓
Follow Controller
    ↓
MotionRequest
```

The Follow Me controller should consume an abstract `PersonTarget`.

> **Status:** Implemented (LiDAR-first, validated in simulation). The trolley
> detects and follows a person walking away in front of it using a LiDAR person
> detector, behind a `PersonTarget` abstraction. See
> `plans/plan-follow-me.prompt.md` for the full design.

## Architecture

```text
/scan (LiDAR)
  ├─> person_detection_node ──> /person/target (PersonTarget)
  └─> obstacle_awareness_node ─> /obstacles/awareness (soft, for steering)
                                    ↓
                              follow_controller_node
                                    ↓
                              MotionRequest (priority 0)
                                    ↓
                              Safety Controller
```

## PersonTarget

```text
PersonTarget
    distance_m
    lateral_offset_m
    relative_velocity_mps
    confidence
    source        # lidar / camera / fusion
    timestamp
    valid
```

## Detection (LiDAR)

- **Detection pipeline:** sector gate → clustering → cluster filter (person-like
  width) → centroid. Disambiguation via motion tracking (a person moves; a pole
  doesn't) + leg-pair signature + temporal validation + EMA smoothing.
- **Lock-on acquisition:** waits for the first person within `lock_distance_m`
  (2 m), locks on, and continuously tracks that same person (continuity check), so
  it never switches to a closer stranger.
- **Orientation robustness:** the lock is maintained by centroid continuity — a
  person turning in place barely moves their centroid, so the lock holds through
  all intermediate angles (verified 0°↔90° ↔ 90° side-on still yields a stable
  valid target). The torso cylinder is radially symmetric in the LiDAR plane, so
  a 90° side-on view still returns a person-width cluster.

## Confidence model

`confidence = 0.5 · width_conf + 0.3 · motion_conf + 0.2 · struct_conf`

- **width_conf** — how closely the cluster width matches a person (~0.45 m ideal).
- **motion_conf** — the target's centroid is translating coherently (a person
  moves; a pole doesn't).
- **struct_conf** — derived from the rolling width history of the locked target
  (`struct_history_scans`, `struct_conf_scale_m`): it rises when the apparent
  width oscillates (two-legs ↔ one-leg) as a person turns in place, so a
  **stationary but turning person is still scored as a person**, while a static
  pole/wall (constant width) earns no structural credit. Verified in sim: a static
  90° hold scores ~0, an active turn sweep raises it to ~0.17 (larger in the real
  world, where the shoulder-vs-facing width swing is much wider than the sim's
  radially symmetric cylinder).

## Follow controller

- **Follow-behind control law:** maintain `follow_distance_m`, steer to center on
  the person, P-controller clamp to `max_speed_mps`.
- **Potential-field obstacle steering:** obstacles from `/obstacles/awareness`
  exert a repulsion, so the trolley steers around obstacles (vs. the hard safety
  stop handled separately by the Safety Controller).
- **Hold-on-stop / resume:** holds when the person stops or leaves the forward
  sector; resumes within `resume_timeout_s`; otherwise stops and requires
  re-triggering.
- **Trigger/stop:** `/follow` service (`FollowTrigger.srv`); `/follow/status`
  (`FollowStatus`). Callable from web + HMI.

The architecture also allows later implementations using camera detection or
LiDAR + camera fusion (behind the same `PersonTarget` interface), without
rewriting the follow controller.

## Follow Me stop conditions

The trolley must stop if:

- the person target is lost
- confidence falls below the required threshold
- the person approaches the trolley unexpectedly
- an obstacle is detected
- required sensor data becomes stale
- the Safety Controller rejects the motion request

The trolley must not blindly continue toward the target's last known position.