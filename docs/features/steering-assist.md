# Obstacle Steering Assist (Manual)

While the operator drives manually, the trolley gently steers around nearby
obstacles instead of only hard-stopping on them.

> **Status:** Implemented. See `plans/plan-dead-reckoning.prompt.md` for the
> feature-planning pattern; the node is `steering_assist_node.cpp`.

## Purpose

The hard stop is safe but frustrating. A soft steering nudge improves the
manual driving experience: the operator keeps full control, but gets help
steering away from obstacles.

## Overview

```text
/scan → obstacle_awareness_node → /obstacles/awareness (soft steering view)
        ↓
   steering_assist_node
        │   ├─ only active while operator is driving manually (priority 1)
        │   ├─ compute a steering nudge away from nearby obstacles
        │   └─ publish MotionRequest (priority 1) with the nudge
        ↓
   Safety Controller (hard stop on obstacle_in_zone still applies in auto)
```

## How it works

- **Inputs:** `/obstacles/awareness` (nearest obstacle distance + angle),
  `/mode/state` (only active in MANUAL mode), `/assist/config` (HMI on/off).
- **Nudge:** `nudge = -sign(angle) * gain * (1/distance)`, clamped to
  `max_nudge_radps`. Angular only — it steers, it does not drive.
- **Priority:** the assist is a **manual-priority (1)** request, so it never
  overrides autonomous driving (priority 0) or safety (priority 3).
- **Safety backstop:** the existing hard stop on `obstacle_in_zone` remains for
  non-manual modes; in MANUAL mode the Safety Controller disables the obstacle
  hard-stop (the operator has full control) and the assist nudges instead.
- **Tunable:** `assist_gain`, `min_distance_m`, `max_distance_m`,
  `max_nudge_radps` so it doesn't fight the operator's own steering.

## HMI integration

The ESP32 handle unit has a **Steering Assist** toggle on the Assist screen
(`ST_STEERING_ASSIST`). It flows through `handle_gateway.py` → `/assist/config`
(`AssistConfig.msg`) → `steering_assist_node`, which only nudges when enabled.

## Configuration

See `config/golfcart.yaml`:

```yaml
steering_assist_node:
  assist_gain: 0.5                  # rad/s nudge per (1/distance)
  min_distance_m: 0.5               # no nudge closer than this (m)
  max_distance_m: 2.0               # no nudge farther than this (m)
  max_nudge_radps: 0.4              # clamp on the steering nudge (rad/s)
```

## Messages

- `golfcart_msgs/AssistConfig` — `/assist/config` (steering assist on/off).
- `golfcart_msgs/ModeState` — `/mode/state` (operating mode).

## Validation

- Pure nudge math in `test_steering_assist.cpp` — unit-tested.
- Full workspace test suite passes; firmware builds.