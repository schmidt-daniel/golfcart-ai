# Roadmap: Next Features

This document captures the next batch of features to implement, selected for
being **software-only** (no new hardware) and building directly on the existing
autonomous stack. Each feature reuses already-implemented nodes, messages, and
patterns.

**Status: the software-only batch is complete.**

> **Completed:**
> - Remote E-Stop + Live Telemetry Dashboard (`docs/features/remote-estop-dashboard.md`)
> - GPS-Denied Dead-Reckoning Fallback (`docs/features/dead-reckoning.md`)
> - Obstacle Steering Assist (`docs/features/steering-assist.md`)
> - Battery Range Estimator (`docs/features/range-estimator.md`)

> **Not selected now:** camera person re-ID, weather sensing — these require
> new hardware (camera processing, sensors) and are documented separately in
> `docs/features/`.

> **Future idea:** **gesture control** — a camera-based gesture interface
> (e.g. wave to summon, hand signals to stop/follow) instead of voice. More
> appropriate than shouting commands across the course; requires a camera and
> gesture-recognition processing.

---

Each feature should follow the existing pattern: a plan in `plans/`, a feature
doc in `docs/features/`, unit tests for pure math, a headless e2e check script,
and a `FEATURES.md` entry when implemented.