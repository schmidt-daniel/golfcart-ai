# Automatic Brake-Hold on Slopes

Keep the trolley stationary on a slope when stopped, preventing creep, and
release the brake when the operator commands forward (hill-start assist).

> **Status:** Implemented as an enhancement to `hill_rollback_node`. See
> `plans/plan-wheel-slip.prompt.md` for the feature-planning pattern.

## Purpose

Rollback protection is **reactive** — it catches the cart *after* it starts
rolling back. Brake-hold is **proactive**: it holds the brake while the cart is
stopped on a slope, so the rollback never starts. It also smooths hill starts
by releasing the brake only when the operator commands forward.

## Overview

```text
/imu/data (pitch)  +  /motor/state (wheel velocity)
        ↓
   hill_rollback_node
        │   ├─ stopped on a slope (|wheel| <= threshold) → BRAKE_HOLD
        │   │     (priority-3 zero MotionRequest)
        │   └─ operator commands forward (|wheel| > threshold) → release
        ↓
   Safety Controller
```

## How it works

- **Brake-hold** engages when the cart is **stopped** (`|wheel_velocity| <=
  `brake_hold_threshold_mps`) on a **slope** (uphill or downhill). It publishes
  a priority-3 zero `MotionRequest` (`BRAKE_HOLD`) so the Safety Controller
  holds the cart.
- **Release** happens when the operator commands forward (wheel velocity above
  the threshold), so the cart can drive off — this is the hill-start assist.
- It's the **proactive** counterpart to rollback protection (which catches
  backward motion already in progress).

## Safety considerations

- The brake-hold is a priority-3 request (same as rollback protection), so it
  overrides manual/autonomous requests while engaged.
- It only holds the cart **stopped** on a slope — it never commands forward
  motion on its own.
- It can be disabled via `brake_hold_enabled` if the operator prefers.

## Configuration

See `config/golfcart.yaml`:

```yaml
hill_rollback_node:
  brake_hold_enabled: true          # hold the brake while stopped on a slope
  brake_hold_threshold_mps: 0.02    # wheel speed below which the hold applies (m/s)
```

## Validation

- `test_hill_rollback.cpp` — brake-hold engage/release/disable tests.
- Full workspace test suite passes.