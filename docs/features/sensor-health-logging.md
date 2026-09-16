# Sensor Health Logging

Log capability dropouts over time so intermittent sensor failures can be
diagnosed.

> **Status:** Implemented.

## Purpose

The capability system tracks which sensors are present, but doesn't record
*when* they drop out. This feature logs every present→absent (and absent→present)
transition to a file, so an intermittent sensor (e.g. a flaky GPS or LiDAR)
can be diagnosed after the fact.

## Overview

```text
capability_node → /capability/status → sensor_health_logger_node → JSONL file
```

## How it works

- **`sensor_health_logger_node`** subscribes to `/capability/status`.
- It keeps the previous capability snapshot and, on each update, detects
  transitions (present→absent = dropout, absent→present = recovery).
- Each transition is appended as a JSON Lines entry with the sensor name, the
  event, a timestamp, and the full capability snapshot.

## Configuration

See `config/golfcart.yaml`:

```yaml
sensor_health_logger:
  log_file: /var/lib/golfcart/sensor_health.jsonl
```

## Example log entry

```json
{"sensor":"gps","event":"absent","ts":1737000000.0,"lidar_horizontal":1,"lidar_tilted":1,"gps":0,"imu":1,"battery":1,"camera":0,"coral":0,"odrive":1}
```

## Files

- `src/golfcart_system/src/sensor_health_logger_node.cpp` — the logger node
- `src/golfcart_system/CMakeLists.txt` — build/install
- `src/golfcart_bringup/launch/core.launch.py` — launch wiring
- `config/golfcart.yaml` — `sensor_health_logger` params