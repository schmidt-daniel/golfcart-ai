# Tilted-LiDAR Terrain Hazard Detection

Detect ditches, bunkers, high grass, and raised obstacles ahead of the trolley
using the tilted forward-facing LiDAR, and stop in autonomous modes.

> **Status:** Implemented (ground-plane residual model + statistical
> classifier). Physical calibration and validation remain.

## Purpose

The tilted LiDAR scans the ground ahead. Geometric depressions (ditches,
bunkers) and scattered returns (high grass) are hazards the trolley should not
drive into. This feature classifies the tilted scan and publishes a typed
hazard that the Safety Controller uses to stop in autonomous modes.

## Overview

```text
/scan_tilted → terrain_hazard_node → /terrain/hazard → Safety Controller
```

## How it works

- **Ground-plane model:** for each beam angle, the expected flat-ground range
  is computed from the mounting height and tilt angle:
  `r_expected = h / sin(tilt + angle)`.
- **Residual:** `residual = r_measured - r_expected`.
  - Large positive residual → ground drops away (ditch / drop-off).
  - Large negative residual → ground rises (raised obstacle).
  - Depression followed by a rise → bunker.
- **Statistical roughness:** high variance in the residuals indicates
  scattered returns (high grass), distinct from a clean geometric signal.
- **Confirmation:** a hazard must persist for `confirm_scans` scans before it
  is published, and clears after `clear_scans` clean scans.

## Configuration

See `config/golfcart.yaml`:

```yaml
terrain_hazard_node:
  input_topic: scan_tilted
  forward_half_angle_rad: 0.61
  mount_height_m: 0.65
  tilt_rad: 0.436
  drop_off_residual_m: 0.5
  obstacle_residual_m: 0.5
  min_valid_fraction: 0.5
  hazard_fraction: 0.25
  roughness_threshold_m: 0.35
  confirm_scans: 3
  clear_scans: 3
```

`mount_height_m` and `tilt_rad` must match the physical mounting. Calibrate by
driving on flat ground and recording the expected profile.

## Safety behavior

- Autonomous modes: a confirmed hazard forces a stop.
- Manual mode: the operator retains control (deliberate decisions allowed).

## Files

- `src/golfcart_lidar/src/terrain_hazard_node.cpp`
- `src/golfcart_lidar/include/golfcart_lidar/terrain_math.hpp`
- `src/golfcart_lidar/test/test_terrain_math.cpp`
- `src/golfcart_msgs/msg/TerrainHazard.msg`