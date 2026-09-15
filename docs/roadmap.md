# Roadmap: Next Features

This document captures the next batch of features to implement, selected for
being **software-only** (no new hardware) and building directly on the existing
autonomous stack. Each feature reuses already-implemented nodes, messages, and
patterns.

**Status: the software-only batch is complete.** The next batch is **camera-enabled** — the Pi Camera + Coral USB decision (see `docs/features/camera-vision.md`) unblocks features that previously required new hardware.

> **Completed:**
> - Remote E-Stop + Live Telemetry Dashboard (`docs/features/remote-estop-dashboard.md`)
> - GPS-Denied Dead-Reckoning Fallback (`docs/features/dead-reckoning.md`)
> - Obstacle Steering Assist (`docs/features/steering-assist.md`)
> - Battery Range Estimator (`docs/features/range-estimator.md`)
> - Course-Aware Speed Governor (`docs/features/course-governor.md`)
> - Energy Dashboard (HMI) (`docs/features/energy-dashboard.md`)
> - Wheel-Slip / Traction Detection (`docs/features/wheel-slip.md`)
> - Automatic Brake-Hold on Slopes (`docs/features/brake-hold.md`)

> **Not selected now:** camera person re-ID — requires new hardware (camera
> processing) and is documented separately in `docs/features/`.

---

## Active batch (camera-enabled)

Selected next features, enabled by the Pi Camera + Coral USB decision. In
priority order:

| # | Feature | Theme | Reuses |
| --- | --- | --- | --- |
| 1 | **Gesture control** | Operator UX | Camera-based gesture interface (wave to summon, hand signals to stop/follow) instead of voice. Runs on the Coral NPU. |
| 2 | **Live course segmentation** | Perception | Label fairway/rough/water/bunkers on unfamiliar courses (traveling-player case). Zero-shot SAM first, fine-tune only if needed. |
| 3 | **Hazard camera view** | HMI | Wire the camera feed + segmentation overlay to the HMI's existing "Segmentation: OFF" placeholder. |

---

## Candidate ideas

Not yet selected — these are under consideration. Each would follow the
existing pattern (plan → feature doc → unit tests → e2e check → FEATURES.md).

| Idea | Theme | Reuses |
| --- | --- | --- |
| **Person re-ID** | Follow-me | Re-identify the operator if the follow-me target is lost. Now viable with the camera. |
| **Multi-round battery learning** | Range | Persist the range estimator's learned `Wh/m` across rounds (currently in-memory). |
| **Trip logging / round summary** | UX | Log each round (distance, energy, time) to the web app. |
| **Predictive range on HMI** | Range | Show remaining holes vs. remaining range. |

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

---

Each feature should follow the existing pattern: a plan in `plans/`, a feature
doc in `docs/features/`, unit tests for pure math, a headless e2e check script,
and a `FEATURES.md` entry when implemented.