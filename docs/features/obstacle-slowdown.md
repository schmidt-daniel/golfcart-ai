# Obstacle-Aware Slowdown (Autonomous Modes)

Slow the trolley down as it approaches an obstacle so it can maneuver around
it safely — in **all autonomous modes**.

> **Status:** Implemented.

## Purpose

The Safety Controller already hard-stops when an obstacle enters the stopping
zone. But for autonomous modes (follow-me, summon, drive-distance, Nav2), a
hard stop at the last moment is jarring. This feature scales the max speed
down as the nearest obstacle approaches, so the trolley slows smoothly and can
steer around it.

## How it works

- **`safety_math.hpp`** — `obstacle_slowdown_factor(distance, start, min_factor)`
  returns a speed factor that ramps linearly from `min_factor` (at distance 0)
  to `1.0` (at the start distance).
- **`safety_controller_node`** subscribes to `/obstacles/state` (which carries
  `nearest_distance_m`). In `handle_request`, when **not** in MANUAL mode, it
  caps the max linear speed at `max_linear * obstacle_slowdown_factor(...)`.
- Because the Safety Controller is the **central authority** for all motion,
  the slowdown applies to every autonomous source automatically.
- In **MANUAL mode** the operator keeps full control (no slowdown), matching
  the existing obstacle hard-stop behavior.

## Configuration

See `config/golfcart.yaml`:

```yaml
safety_controller:
  obstacle_slowdown_start_m: 2.0    # begin slowing when obstacle within this (m)
  obstacle_slowdown_min_factor: 0.3 # slowest speed factor (0-1)
```

## Files

- `src/golfcart_control/include/golfcart_control/safety_math.hpp` — `obstacle_slowdown_factor()`
- `src/golfcart_control/src/safety_controller_node.cpp` — applies the cap
- `src/golfcart_control/test/test_safety_math.cpp` — unit tests