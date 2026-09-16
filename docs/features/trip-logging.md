# Trip Logging / Round Summary

Log each round (course + tee selection → "End round") and show a summary in the
web app's trip history page.

> **Status:** Implemented. `golfcart_navigation` package, `trip_logger_node`.

## Purpose

The operator plays a round on a course. Trip logging records each round's
distance, energy, duration, and average speed, so they can review their rounds
over time.

## Overview

```text
course/tee selection (/course/hole)  →  round START
        ↓
   trip_logger_node
        │   ├─ distance from /odometry/filtered
        │   ├─ energy from /battery/state
        │   └─ duration from wall clock
        ↓
   /end_round (HMI "End round")  →  round END → JSON log + /trip/summary
        ↓
   web trips.html (reads /trips endpoint)
```

## How it works

- **`trip_logger_node`** tracks the current round:
  - **Start:** a course + tee is selected (`/course/hole`).
  - **Distance:** accumulated from `/odometry/filtered` deltas.
  - **Energy:** `V × A × dt` from `/battery/state`, only while moving.
  - **End:** the `/end_round` service is called (HMI "End round" button).
- On end, it writes a **JSON Lines** entry to the log file and publishes the
  final `TripSummary` on `/trip/summary`.
- The course name + tee name come from `/course/map` (`CourseMap`).

## HMI

An **END ROUND** item in the main menu calls `/end_round`.

## Web

- **`trips.html`** — a trip history page showing all rounds (course, tee,
  distance, energy, time, avg speed).
- The web server serves a **`/trips`** endpoint that reads the JSON log.
- Linked from the dashboard.

## Configuration

See `config/golfcart.yaml`:

```yaml
trip_logger_node:
  log_file: /var/lib/golfcart/trips.json   # round history log (JSON Lines)
```