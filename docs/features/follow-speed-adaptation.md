# Follow-Me Speed Adaptation

Vary the follow-me speed with the target distance so the trolley slows when the
person is close (avoid crowding) and speeds up as they pull away, instead of a
fixed speed.

> **Status:** Implemented.

## Purpose

The follow controller previously capped its speed at a fixed `max_speed_mps`.
This meant it drove at full speed even when the person was only just ahead of
the follow distance, which feels aggressive and can crowd the operator. Speed
adaptation makes the follow feel natural: gentle when close, faster when the
person walks away.

## How it works

- **`follow_math.hpp`** — `follow_speed(distance, follow_distance, min_speed,
  max_speed, full_speed_distance)` returns the desired speed cap:
  - At/below the follow distance → `min_speed_mps` (don't crowd).
  - At/above `full_speed_distance_m` → `max_speed_mps`.
  - Between them → linear ramp.
- **`follow_controller_node`** caps the P-controller's linear output at the
  adapted speed instead of the fixed max. The turn (angular) control is
  unchanged.

## Configuration

See `config/golfcart.yaml`:

```yaml
follow_controller_node:
  follow_distance_m: 1.5            # desired distance behind the person (m)
  max_speed_mps: 0.8                # max follow speed (m/s)
  min_speed_mps: 0.2                # speed when at the follow distance (m/s)
  full_speed_distance_m: 3.0        # distance at which max speed is reached (m)
```

## Files

- `src/golfcart_follow/include/golfcart_follow/follow_math.hpp` — `follow_speed()`
- `src/golfcart_follow/src/follow_controller_node.cpp` — uses the adapted cap
- `src/golfcart_follow/test/test_follow_math.cpp` — unit tests