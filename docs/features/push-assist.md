# Push Assist (Pedelec-Style)

Assist the user by detecting how much they push or brake the cart, and add
proportional motor assistance — the same principle as electric bike (pedelec)
assist.

## Purpose

Most electric trolleys use a speed dial. Instead, we detect the user's
**push/brake force** and assist proportionally, giving a natural, effort-
amplifying feel.

## Principle (from Pedelecs)

Pedelecs measure the rider's input torque and add motor power proportional to
it:

```text
assist_torque = rider_torque × assist_ratio
```

For the golf cart, the equivalent is:

```text
assist_velocity = user_force × assist_gain
```

The motor **amplifies the user's input** rather than driving independently.

## Sensing Approach

| Pedelec | Golf cart |
| --- | --- |
| Rider pedals → crank torque | User pushes → handle force |
| Torque sensor at crank | **Load cell in handle** |
| Motor adds proportional assist | Motor adds proportional push-assist |
| Assist ratio (eco/tour/sport) | Assist gain (configurable) |

**Recommended:** a **load cell + HX711 amplifier** mounted in the handlebar to
measure push/pull/brake force directly. This is the "torque sensor" equivalent
and gives a natural, proportional feel.

**Not recommended:** ODrive current / wheel torque alone — it is indirect and
cannot separate user force from slope or motor state.

## Slope Compensation

Use the **IMU (pitch)** to subtract gravity, so assist is based on *user* force
only, not the hill:

```text
user_force_effective = handle_force - gravity_component(pitch)
assist = user_force_effective × assist_gain
```

This is an advantage over pedelecs, which do not compensate for slope the same
way.

## Control Details

- **Dead zone:** no assist below a minimum force (avoid noise-triggered assist).
- **Hysteresis:** prevent rapid on/off switching around the threshold.
- **Brake assist:** when the user pulls back (negative force), reduce motor
  power or apply braking proportionally.

## Architecture

```text
Handle load cell
        ↓
force_sensor_node (HandleForce)
        ↓
IMU (pitch) → slope compensation
        ↓
Push-assist controller (assist_gain)
        ↓
MotionRequest
        ↓
Safety Controller
```

## Relationship to Hill Assist

This extends the existing `hill_rollback_node` (Hill Assist) to be
**force-proportional** rather than fixed. The handle force becomes an input to
the assist controller.

## Effort

- Hardware: load cell + HX711 (~$15–25).
- Software: `force_sensor_node` (load cell + HX711 + mock) + assist controller
  update. Medium.