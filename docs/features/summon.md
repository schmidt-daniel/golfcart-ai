# Summon

The trolley automatically drives to the operator's current position, triggered
from a smartphone.

## Purpose

Allow the operator to call the trolley to them without walking to it. The
operator presses "summon" on their phone, and the trolley drives to the phone's
GPS position.

## Overview

```text
Phone GPS position
        ↓
Target waypoint
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

## Phone Side

The phone must:

- send its GPS position to the trolley (over the network, e.g. HTTP/WebSocket
  or `rosbridge_server`)
- trigger the summon (a service call)

This is a small phone app or web page.

## Safety Considerations

Summon is autonomous motion toward a person, so it requires extra care:

- **Stop if the target is lost** — if the phone GPS drops or the network fails,
  the trolley must stop, never drive blind.
- **Obstacle avoidance is mandatory** — it must not run into people or objects.
- **Maximum distance / timeout** — do not allow it to drive across the whole
  course unsupervised.
- **Physical stop override** — the operator must be able to stop it at any time.
- **Speed limit** — summon should be slow and cautious.
- **Operator interaction** — on an unexpected obstacle mid-route, stop and ask
  the operator how to proceed via the web app.

## Effort and Sequencing

Summon is a **later-stage feature**. It depends on:

1. GPS / localization (planned)
2. Course mapping (planned)
3. Autonomous navigation / Nav2 (planned)

The phone side is easy; the hard part is the navigation underneath.

See `plans/plan-autonomous-navigation.prompt.md` for the full navigation plan.