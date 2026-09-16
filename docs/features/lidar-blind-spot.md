# LiDAR Blind-Spot Masking

Ignore a configurable angular window in the LiDAR scan so the trolley doesn't
react to a known blind spot (e.g. the bag behind the trolley).

> **Status:** Implemented.

## Purpose

The LiDARs are mounted at the **front** of the trolley, so they're also in
front of the bag. The space **behind** the trolley (where the bag sits) is a
backwards blind spot. Without masking, the bag itself can appear as an
obstacle or interfere with person detection. This feature lets the operator
define an angular window to ignore completely.

## How it works

- **`lidar_node`** reads the scan and, for every point whose angle falls inside
  the blind-spot window, sets the range to **infinity** (the same as "no
  return"). Downstream nodes (`obstacle_detection_node`,
  `person_detection_node`) then treat that region as empty.
- The window is defined by a **center angle** and a **half-width** (radians),
  with wrap-around handling (so a window straddling 0°/360° works).

## Configuration

See `config/golfcart.yaml`:

```yaml
lidar_node:
  implementation: mock              # mock | real
  device: /dev/ttyUSB0              # serial device (real implementation)
  blind_spot_center_rad: 3.14159    # blind-spot center angle (rad); 0 = forward
  blind_spot_half_angle_rad: 0.0    # blind-spot half-width (rad); 0 = disabled
```

- Angle **0 rad = forward** (the scan's angle 0). The bag is behind, so the
  default center is **π rad** (180°).
- Set `blind_spot_half_angle_rad` to `0` to disable masking.
- The window is `[center - half, center + half]` (wrapped to 0–2π).

## Files

- `src/golfcart_lidar/src/lidar_node.cpp` — `in_blind_spot()` + masking
- `config/golfcart.yaml` — `lidar_node` params
- `src/golfcart_bringup/launch/core.launch.py` — passes `lidar_node` params