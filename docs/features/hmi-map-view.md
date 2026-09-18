# HMI Map View

Show a top-down course map with the trolley position on the handle-unit
display, so the operator can see where they are on the course at a glance.

> **Status:** Implemented. The Pi renders a compact map bitmap from
> `CourseMap` and blits it to the ESP32; the trolley pose comes from
> `/odometry/filtered`.

## Purpose

The ESP32 handle unit's 320×480 SPI LCD has limited RAM and no room for a full
occupancy grid. This feature gives the operator a simplified top-down map —
forbidden zones (greens, water, bunkers) and course features (holes, tees) —
with the trolley's live position and heading.

## Overview

```text
CourseMap (/course/map) ──┐
                          ├─> handle_gateway (Pi) ── DL_MAP_FRAME ──> ESP32
/odometry/filtered ───────┘        (renders RGB565 bitmap)   (blits + marker)
```

## How it works

- **`handle_gateway`** subscribes to `/course/map` and `/odometry/filtered`.
- On a new `CourseMap`, the Pi **downsamples** it to a compact RGB565 bitmap
  (148×190) using Pillow: forbidden zones are drawn as red polygons, course
  features (holes/tees) as cyan markers. It sends the bitmap once via the new
  `DL_MAP_FRAME` downlink.
- The trolley pose (`/odometry/filtered`) is sent as `ST_MAP_X/Y/HEADING`
  state values (cm / deg×10).
- **`SCR_MAP`** and **`SCR_HOLE`** screens on the ESP32 blit the cached bitmap
  into their map areas and show the trolley x/y + a MAP/No-map indicator. The
  MAP main-menu item opens `SCR_MAP`; the hole view (`SCR_HOLE`) shows the
  same map alongside the hole distance/remaining. The bitmap is cached on the
  ESP32 so it re-renders when either screen is shown.

## Protocol additions

| Item | Value | Notes |
| --- | --- | --- |
| `DL_MAP_FRAME` | `0x08` | Payload: `map_w` (u16) + `map_h` (u16) + packed RGB565 |
| `ST_MAP_X` | `0x47` | Trolley map x (uint16, cm) |
| `ST_MAP_Y` | `0x48` | Trolley map y (uint16, cm) |
| `ST_MAP_HEADING` | `0x49` | Trolley heading (int16, deg×10) |
| `ST_MAP_AVAILABLE` | `0x4A` | 1 when a course map is loaded |
| `SCR_MAP` | `0x13` | Map screen ID |

## Why the Pi renders the bitmap

The ESP32's LVGL + 40-row partial buffer can't hold a full occupancy grid or
do polygon rasterization. Rendering on the Pi (which has the map + Pillow)
keeps the ESP32 firmware simple: it just blits pixels and draws the marker.

## Files

- `src/golfcart_hmi/golfcart_hmi/protocol.py` — `DL_MAP_FRAME` + `ST_MAP_*` +
  `build_map_frame()`
- `src/golfcart_hmi/golfcart_hmi/handle_gateway.py` — map rendering + pose
  downlink
- `src/golfcart_hmi/firmware/src/screens.cpp` / `screens.h` — `SCR_MAP` screen
  + `screens_set_map_bitmap()`
- `src/golfcart_hmi/firmware/src/main.ino` — `DL_MAP_FRAME` + `ST_MAP_*` handling
- `src/golfcart_hmi/firmware/src/handle_protocol.h` — protocol constants