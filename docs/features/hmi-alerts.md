# HMI Alert / Notification Queue

Surface the trolley's fault conditions as a dismissible banner on the handle
unit, so the operator sees warnings (low battery, geofence, obstacle, slip,
range) without digging into the debug screens.

> **Status:** Implemented. The Pi computes the highest-priority active alert
> from the existing fault topics and downlinks it; the ESP32 draws a banner.

## Purpose

The HMI already shows individual fault states on debug screens, but the
operator has to navigate there to see them. This feature pushes the most
important active alert to the top of whatever screen is showing.

## Overview

```text
/battery/state ─┐
/geofence/status┤
/obstacles/state┤
/slip/status    ├─> handle_gateway (_update_alert) ─ ST_ALERT ─> ESP32 banner
/range/status   │
/navigation/status┘
```

## How it works

- **`handle_gateway`** caches the fault states from the existing subscriptions
  (battery %, geofence, obstacle, slip, range state, nav status).
- `_update_alert()` computes the **highest-priority** active alert and sends it
  as `ST_ALERT` (0 = none). Priority (highest first):
  1. Battery critical (≤10%)
  2. Geofence crossed
  3. Obstacle in zone
  4. Battery low (≤20%)
  5. Range critical
  6. Wheel slip
  7. Geofence near
  8. Range caution
  9. Nav error
- **`SCR_*`** — the ESP32 draws a colored banner across the top of the current
  screen (`screens_set_alert`). Alert 0 clears it. The banner color reflects
  severity (warn = amber, danger = red).

## Protocol additions

| Item | Value | Notes |
| --- | --- | --- |
| `ST_ALERT` | `0x4B` | Active alert code (uint8) |
| `ALERT_NONE` | `0` | No alert |
| `ALERT_BATTERY_LOW` | `1` | Battery ≤ 20% |
| `ALERT_BATTERY_CRITICAL` | `2` | Battery ≤ 10% |
| `ALERT_GEOFENCE_NEAR` | `3` | Near geofence boundary |
| `ALERT_GEOFENCE_CROSSED` | `4` | Geofence crossed |
| `ALERT_OBSTACLE` | `5` | Obstacle in stopping zone |
| `ALERT_SLIP` | `6` | Wheels slipping |
| `ALERT_RANGE_CAUTION` | `7` | Range caution |
| `ALERT_RANGE_CRITICAL` | `8` | Range critical |
| `ALERT_NAV_ERROR` | `9` | Navigation error |

## Files

- `src/golfcart_hmi/golfcart_hmi/protocol.py` — `ST_ALERT` + alert codes
- `src/golfcart_hmi/golfcart_hmi/handle_gateway.py` — `_update_alert()`
- `src/golfcart_hmi/firmware/src/screens.cpp` / `screens.h` — alert banner
- `src/golfcart_hmi/firmware/src/main.ino` — `ST_ALERT` handling
- `src/golfcart_hmi/firmware/src/handle_protocol.h` — protocol constants