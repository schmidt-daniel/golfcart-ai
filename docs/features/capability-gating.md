# Hardware Capability Detection + Feature Gating

Enable or disable features based on which hardware is actually present, so the
software degrades gracefully when components are missing (build the cart step
by step without breaking the stack).

> **Status:** Implemented. `golfcart_system` package, `capability_node`.

## Purpose

The cart is built incrementally. If a sensor is missing, the software should
still run — it just disables the features that need that sensor. Manual driving
always works; only autonomous/assisted features gate on hardware.

## Overview

```text
sensor topics (gps/fix, imu/data, battery/state, scan, motor/state)
        ↓
   capability_node (heartbeat + valid-flag detection)
        ↓  /capability/status (CapabilityStatus)
        ↓
   mode_node / summon_node / follow_controller / safety_controller / ...
```

## How it works

- **`capability_node`** subscribes to the sensor topics and tracks each via a
  **heartbeat + valid flag**: a sensor is "absent" if it stops publishing or
  reports `valid=false` for a timeout (`heartbeat_timeout_s`).
- Publishes `CapabilityStatus` on `/capability/status` with 8 booleans:
  `lidar_horizontal`, `lidar_tilted`, `gps`, `imu`, `battery`, `camera`,
  `coral`, `odrive`.
- **Coral** is detected via the `/pose/keypoints` heartbeat (the Coral pose
  node only publishes when the EdgeTPU + a model are working).
- **Config overrides** (`-1` auto, `0` absent, `1` present) allow testing
  without hardware.

## Feature gating

| Node | Gate |
| --- | --- |
| `mode_node` | FOLLOW needs horizontal LiDAR; AUTONOMOUS needs GPS |
| `summon_node` | Requires GPS |
| `follow_controller` | Requires horizontal LiDAR |
| `safety_controller` | Enable requires ODrive |
| `gesture_controller` | Requires camera |
| `segmentation_node` | Requires camera + Coral |

**Manual driving always works** regardless of capabilities.

## Configuration

See `config/golfcart.yaml`:

```yaml
capability_node:
  heartbeat_timeout_s: 2.0
  publish_rate_hz: 5.0
  override_lidar_horizontal: -1   # -1 auto, 0 absent, 1 present
  override_gps: -1
  # ... one override per capability
```

## HMI

The **Sensors** screen (Debug menu) shows each sensor as OK/MISSING from the
capability bitmask (`ST_CAPABILITY`).