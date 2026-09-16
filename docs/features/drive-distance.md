# Drive Distance

Drive the trolley a fixed distance (10/20/30/40/50 m) in its current heading
direction, using the existing navigation stack so obstacle avoidance applies
automatically.

> **Status:** Implemented. `golfcart_navigation` package, `drive_distance_node`.

## Purpose

The operator picks a distance on the HMI and the trolley drives straight ahead
that far — useful for repositioning without manual driving.

## Overview

```text
HMI (10/20/30/40/50 m) → /drive_distance (DriveDistance)
        ↓
   drive_distance_node
        │   ├─ get current pose + yaw from /odometry/filtered
        │   ├─ goal = pose + distance * (cos(yaw), sin(yaw))
        │   └─ forward to /set_goal → Nav2 navigate_to_pose
        ↓
   obstacle avoidance + safety apply automatically
```

## How it works

- **`drive_distance_node`** subscribes to `/odometry/filtered` for the current
  pose + yaw, computes the goal point N meters ahead in the heading direction,
  and forwards it to `/set_goal` (Nav2 `navigate_to_pose`).
- **Reuses Nav2** → obstacle avoidance, safety, and navigation status all work
  for free.
- **Gated on GPS** capability (autonomous driving requires GPS).
- **Cancel** stops the drive.

## HMI

A **Drive Distance** screen (2nd in the main menu) with 10/20/30/40/50 m buttons
+ Cancel. The gateway calls `/drive_distance`.

## Configuration

See `config/golfcart.yaml`:

```yaml
drive_distance_node:
  max_distance_m: 50.0   # max drivable distance (m)
```