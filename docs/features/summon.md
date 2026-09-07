# Summon

The trolley automatically drives to the operator's current position, triggered
from a smartphone.

> **Status:** Implemented (validated in simulation). The underlying autonomous
> navigation is implemented (see `navigation.md`); summon adds a live-phone-position
> target on top of it. See `plans/plan-summon.prompt.md` for the full design.

## Purpose

Allow the operator to call the trolley to them without walking to it. The
operator presses "summon" on their phone, and the trolley drives to the phone's
GPS position.

## Overview

```text
Phone GPS position
        ↓
summon_node (tracks live target)
        ↓
Target waypoint (CURRENT or PREDICT)
        ↓
/set_goal_geo → /set_goal → Nav2
        ↓
Trolley GPS + IMU/odometry
        ↓
Localization
        ↓
Path to target
        ↓
MotionRequest
        ↓
Safety Controller
```

## Relationship to Navigation

Summon is a **use case on top of autonomous navigation**, not a standalone
feature. It requires the same machinery as "drive to hole 5":

- a map of the course (for path planning and obstacle avoidance)
- localization (GPS + IMU + odometry)
- path planning (Nav2 — see `navigation.md`)
- obstacle avoidance (LiDAR)

The only difference from a fixed waypoint is that the target is the operator's
**live phone position** instead of a stored waypoint.

## Targeting modes

- **CURRENT** — aim at the operator's live GPS position (simple, no lead).
- **PREDICT** — estimate the operator's velocity (direction + speed) from recent
  GPS fixes and aim at their **predicted future position** at the trolley's ETA
  (intercept), so the trolley meets them instead of chasing them. Includes a
  **"hold if approaching"** rule: if the operator is walking directly toward the
  trolley, the trolley holds position and waits.

## Phone Side

The phone page (`web/summon.html`) publishes its GPS position to `/phone/gps`
(`golfcart_msgs/PhoneFix`) and triggers summon via the `/summon` service
(`SummonTrigger.srv`). It shows summon status, distance, progress, the planned
route, and arrival/obstacle notifications.

## Safety Considerations

Summon is autonomous motion toward a person, so it requires extra care:

- **Stop if the target is lost** — if the phone GPS drops or the network fails,
  the trolley drives to the last planned location and notifies the phone.
- **Accuracy gate** — summon only works when the phone GPS is accurate to ≤ 2.5 m.
- **Obstacle avoidance is mandatory** — it must not run into people or objects.
- **Maximum distance / timeout** — do not allow it to drive across the whole
  course unsupervised.
- **Physical stop override** — the operator must be able to stop it at any time.
- **Speed limit** — summon should be slow and cautious.
- **Operator interaction** — on an unexpected obstacle mid-route, stop and ask
  the operator how to proceed via the phone page.

## Implementation

- `golfcart_navigation/summon_node` — live-target tracking, targeting modes,
  state machine, safety checks.
- `summon_math.hpp` — pure math (velocity estimation, predictive target,
  approach-cone) for unit testing.
- `golfcart_msgs` — `PhoneFix.msg`, `SummonStatus.msg`, `SummonTrigger.srv`.
- `web/summon.html` — phone page.
- `hmi_node` — `summon_to()`/`cancel_summon()` + summon status.
- `golfcart_gazebo/gps_dropout_node.py` — simulate GPS loss for testing.
- `scripts/summon_check.sh` — sim smoke test.

See `plans/plan-autonomous-navigation.prompt.md` for the full navigation plan.