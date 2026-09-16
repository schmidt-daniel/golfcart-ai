# Roadmap: Next Features

This document captures the next batch of features to implement, selected for
being **software-only** (no new hardware) and building directly on the existing
autonomous stack. Each feature reuses already-implemented nodes, messages, and
patterns.

**Status: the software-only batch is complete.** The **camera-enabled batch is
also complete** — gesture control, live course segmentation, and the hazard
camera view are all implemented. The Pi Camera + Coral USB decision (see
`docs/features/camera-vision.md`) unblocks these; the remaining work is the
hardware-validation phase (real camera capture + Coral NPU inference).

> **Completed:**
> - Remote E-Stop + Live Telemetry Dashboard (`docs/features/remote-estop-dashboard.md`)
> - GPS-Denied Dead-Reckoning Fallback (`docs/features/dead-reckoning.md`)
> - Obstacle Steering Assist (`docs/features/steering-assist.md`)
> - Battery Range Estimator (`docs/features/range-estimator.md`)
> - Course-Aware Speed Governor (`docs/features/course-governor.md`)
> - Energy Dashboard (HMI) (`docs/features/energy-dashboard.md`)
> - Wheel-Slip / Traction Detection (`docs/features/wheel-slip.md`)
> - Automatic Brake-Hold on Slopes (`docs/features/brake-hold.md`)
> - Gesture Control (`docs/features/gesture-control.md`)
> - Live Course Segmentation (`docs/features/segmentation.md`)
> - Hazard Camera View (`docs/features/hazard-camera-view.md`)
> - HMI Map View (`docs/features/hmi-map-view.md`)
> - HMI Alert / Notification Queue (`docs/features/hmi-alerts.md`)
> - Follow-Me Speed Adaptation (`docs/features/follow-speed-adaptation.md`)
> - LiDAR Blind-Spot Masking (`docs/features/lidar-blind-spot.md`)

> **Not selected now:** camera person re-ID — requires new hardware (camera
> processing) and is documented separately in `docs/features/`.

---

## Active batch (HMI + autonomy polish)

Selected next features, all software-only. In priority order:

| # | Feature | Theme | Reuses |
| --- | --- | --- | --- |
| 1 | **HMI round summary screen** | UX | Show the just-finished round's stats (distance, energy, time, avg speed) on the handle unit after "End round". Reuses trip logging. |
| 2 | **HMI speed bar** | UX | Speed selector for manual/push-assist mode (from hmi-spec §6.4). |
| 3 | **HMI course/hole quick-select** | UX | Faster hole selection than the current change-hole list. |
| 4 | **Obstacle-aware slowdown** | Autonomy | Slow down to safely maneuver around obstacles — in **all autonomous modes** (not just follow-me). |
| 5 | **Sensor health logging** | Diagnostics | Record capability dropouts over time to a log (like trip logging) to diagnose intermittent sensor failures. |
| 6 | **Map editor → ROS integration** | Mapping | Wire the standalone map editor to publish `CourseMap` to ROS / Nav2 `static_layer`. |

---

## Rejected ideas

These were considered and explicitly rejected. Do not re-suggest them without
a material change in requirements or hardware.

| Idea | Why rejected |
| --- | --- |
| **Voice Control** | Not appropriate for a golf course — shouting commands across the fairway is impractical and socially unacceptable. Superseded by the gesture-control idea. |
| **Weather Sensing** | Redundant for a manually-driven cart — the operator is standing next to it and notices rain themselves. Only marginal value for autonomous operation (wet-grass traction), not worth the extra hardware. |
| **Go to Hole N (tee-to-green)** | Dropped as a roadmap item — the two-leg (tee → green) orchestration didn't make sense as a standalone feature at the time. (The underlying navigation stack it would reuse is implemented.) |
| **Return-to-base** | No real use case — holes are played in sequence and the round ends at the clubhouse anyway, so there's no scenario where the trolley would need to return mid-round autonomously. |
| **Range-aware route planning** | Useless — the course is played where the balls lie (not a fixed route), and there are no charging stops possible on the course, so there's no route to plan around range. |
| **"Where's my cart?" locator** | Already implemented — the web app (`index.html`, `summon.html`) shows the trolley's position on a Leaflet map from `/gps/fix`, and `dashboard.html` shows lat/lon. |
| **"Last hole" range warning** | Not required — the operator can judge this from the existing range/holes display. |
| **Energy usage breakdown on web** | Not required — the trip log already captures the data; a per-bucket breakdown adds little value. |
| **Re-ID confidence display** | Not required — the re-ID tracker works without surfacing its confidence. |
| **Boot self-test screen** | Not required — the capability/sensor info is already in the debug menu. |

---

Each feature should follow the existing pattern: a plan in `plans/`, a feature
doc in `docs/features/`, unit tests for pure math, a headless e2e check script,
and a `FEATURES.md` entry when implemented.