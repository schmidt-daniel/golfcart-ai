# HMI Course/Hole Quick-Select

Jump directly to a hole number from the handle unit, without navigating
through course → tee → hole.

> **Status:** Implemented.

## Purpose

The normal flow to change holes is course → tee → hole, which is several taps.
This feature adds a quick-select screen with a grid of hole numbers (1–18) so
the operator can jump straight to a hole.

## Overview

```text
SCR_QUICK_SELECT (ESP32) → MENU_SELECT(hole N) → handle_gateway
        ↓  /course/hole (HoleSelect, hole_number=N)
        ↓
   course_session_node selects the hole
```

## How it works

- **`SCR_QUICK_SELECT`** screen shows a 6×3 grid of hole buttons (1–18) plus a
  Main Menu button.
- **`handle_gateway`** `_menu_quick_select()` maps the tapped item to a hole
  number and calls `/course/hole` (`HoleSelect`) with `hole_number` set
  directly (reusing the existing service — no new message).
- The **QUICK SEL** item was added to the main menu (after SPEED).

## Protocol additions

| Item | Value | Notes |
| --- | --- | --- |
| `SCREEN_QUICK_SELECT` | `0x16` | Quick-select screen ID |

No new state values — the feature reuses the existing `HoleSelect` service.

## Files

- `src/golfcart_hmi/golfcart_hmi/protocol.py` — `SCREEN_QUICK_SELECT`
- `src/golfcart_hmi/golfcart_hmi/handle_gateway.py` — `_menu_quick_select()` +
  `_call_hole_select_number()`
- `src/golfcart_hmi/firmware/src/screens.cpp` / `screens.h` — `SCR_QUICK_SELECT`
- `src/golfcart_hmi/firmware/src/handle_protocol.h` — protocol constants