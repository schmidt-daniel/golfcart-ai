# HMI Speed Bar

Let the operator select the max speed in manual/push-assist mode from the
handle unit.

> **Status:** Implemented.

## Purpose

The joystick's linear speed was previously scaled by a fixed `max_linear`. This
feature adds a speed selector on the handle unit so the operator can cap the
top speed (e.g. slow down on a crowded path) without changing config.

## Overview

```text
SCR_SPEED (ESP32) → MENU_SELECT → handle_gateway (_menu_speed)
        ↓  ST_SPEED_LIMIT (downlink)
        ↓
   _on_joystick caps linear speed at the selected limit
```

## How it works

- **`SCR_SPEED`** screen shows the current limit and has **− / +** buttons plus
  **Slow / Med / Fast** presets.
- **`handle_gateway`** `_menu_speed()` adjusts `self.speed_limit` (m/s) and
  downlinks it as `ST_SPEED_LIMIT` (cm/s).
- **`_on_joystick`** caps the linear speed at `min(max_linear, speed_limit)`.
- The **SPEED** item was added to the main menu (after ASSIST).

## Protocol additions

| Item | Value | Notes |
| --- | --- | --- |
| `ST_SPEED_LIMIT` | `0x51` | Selected max speed (uint16, cm/s) |
| `SCREEN_SPEED` | `0x15` | Speed bar screen ID |

## Files

- `src/golfcart_hmi/golfcart_hmi/protocol.py` — `ST_SPEED_LIMIT` + `SCREEN_SPEED`
- `src/golfcart_hmi/golfcart_hmi/handle_gateway.py` — `_menu_speed()` + joystick cap
- `src/golfcart_hmi/firmware/src/screens.cpp` / `screens.h` — `SCR_SPEED`
- `src/golfcart_hmi/firmware/src/main.ino` — `ST_SPEED_LIMIT` handling
- `src/golfcart_hmi/firmware/src/handle_protocol.h` — protocol constants