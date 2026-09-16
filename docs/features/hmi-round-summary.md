# HMI Round Summary Screen

Show the just-finished round's stats (distance, energy, time, avg speed) on
the handle unit after the operator ends the round.

> **Status:** Implemented.

## Purpose

When the operator presses **End round** in the main menu, the round ends and
the trip logger publishes a final `TripSummary`. This feature surfaces those
stats on the handle-unit display so the operator sees the round result without
opening the web app.

## Overview

```text
End round (HMI) → /end_round → trip_logger_node → /trip/summary (active=false)
        ↓
   handle_gateway (on_trip_summary) → ST_ROUND_* + SCREEN_NAV(ROUND_SUMMARY)
        ↓
   ESP32 SCR_ROUND_SUMMARY screen
```

## How it works

- **`trip_logger_node`** publishes `/trip/summary` (`TripSummary`) — live while
  a round is active, and a final one with `active=false` when the round ends.
- **`handle_gateway`** subscribes to `/trip/summary`. It downlinks the round
  stats as `ST_ROUND_*` state values. When `active=false` (round just ended),
  it navigates to the round-summary screen.
- **`SCR_ROUND_SUMMARY`** screen shows Distance, Energy, Time, and Avg Speed.

## Protocol additions

| Item | Value | Notes |
| --- | --- | --- |
| `ST_ROUND_ACTIVE` | `0x4C` | 1 while a round is in progress (uint8) |
| `ST_ROUND_DISTANCE` | `0x4D` | Round distance (uint16, m) |
| `ST_ROUND_ENERGY` | `0x4E` | Round energy (uint16, Wh×10) |
| `ST_ROUND_DURATION` | `0x4F` | Round duration (uint16, s) |
| `ST_ROUND_AVG_SPEED` | `0x50` | Round avg speed (uint16, cm/s) |
| `SCREEN_ROUND_SUMMARY` | `0x14` | Round summary screen ID |

## Files

- `src/golfcart_hmi/golfcart_hmi/protocol.py` — `ST_ROUND_*` + `SCREEN_ROUND_SUMMARY`
- `src/golfcart_hmi/golfcart_hmi/handle_gateway.py` — `on_trip_summary()`
- `src/golfcart_hmi/firmware/src/screens.cpp` / `screens.h` — `SCR_ROUND_SUMMARY`
- `src/golfcart_hmi/firmware/src/main.ino` — `ST_ROUND_*` handling
- `src/golfcart_hmi/firmware/src/handle_protocol.h` — protocol constants