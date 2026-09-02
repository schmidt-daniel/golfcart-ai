# Learning

The trolley improves its course map and navigation over time by learning from
previous visits.

## Overview

Learning is split between the **on-board** trolley and an **off-board**
processing machine (dev PC / cloud), for two reasons:

1. The RPi 5 must keep the real-time safety loop (safety/motion/obstacle) free
   of heavy computation.
2. Map refinement and ML are too heavy for the RPi 5.

```text
ON-BOARD (RPi 5)                        OFF-BOARD (dev PC / cloud)
─────────────────                       ─────────────────────────
ROS 2 bags (record trips)       →       Process bags
Cheap immediate flags          →        Refine course map
( "drove here = drivable",            ( drivable / steep / obstacle )
  "steep here" from IMU )              Route optimization
        │                                              │
        └────────────────  updated map  ←──────────────┘
```

## On-Board (data collection + light tasks)

- Record sensor data (GPS, IMU, LiDAR, odometry) as **ROS 2 bags**.
- Record the route taken and whether it succeeded.
- Write **cheap immediate flags** that require no heavy processing:
  - "drove here = drivable"
  - "steep here" (from the IMU)
  - "obstacle encountered here"
- These are discrete, low-cost updates that do not compete with the real-time
  safety loop.

## Off-Board (heavy learning)

- Process recorded bags on a dev PC / cloud.
- Refine the course map: merge drivable/steep/obstacle data over visits.
- Optimize routes and prefer known-good routes.
- Optionally train ML models (e.g., terrain/obstacle classification) using
  labeled data.
- Produce an updated course map for the course.

## Sync

- **Data off:** recorded bags and flags are transferred to the off-board
  machine (via git/rsync or the web app).
- **Map on:** the updated course map is pushed back to the trolley (via the
  web app / `rosbridge_server` or git).

This is a **record → process → update** loop.

## Scope

Learning is **per-course** — each course is mapped and refined separately.

## Storage

Accumulated bags, flags, and refined maps for each course are stored off-board;
the trolley carries only the current course map needed for navigation.

## Effort

- On-board: low (recording + discrete flags).
- Off-board: medium (bag processing + map refinement utilities).
- Sync: low (reuses existing web app / rosbridge / git paths).