# HMI Display

The handle-unit display provides the user with live system information.

## Purpose

Show the operator real-time status while driving:

- current speed (linear velocity)
- battery state (voltage, charge %)
- safety state (READY / MOVING / STOPPED / FAULT)
- current operating mode (manual, follow me, etc.)
- warnings and fault information

## Hardware

- **3.5" Elecrow IPS SPI LCD Touch** (ST7796, 320×480 portrait), driven by the
  **ESP32 handle unit** via LVGL / TFT_eSPI over SPI.
- **Capacitive touch** (FT6336U) over I2C, read by the ESP32. Touch is the
  primary input; the joystick remains a gloved fallback.

## Architecture

The HMI lives on the **ESP32 handle unit**, not the Pi. The ESP32 renders the
screens (LVGL), reads the touch / joystick / load cell, and talks to the Pi
over a single USB serial link (see `docs/handle-protocol.md`).

```text
ESP32 handle unit (LVGL screens, touch, joystick, load cell)
        │  single USB serial link
        ▼
Pi handle_gateway node  →  ROS bus
```

## Data Sources

The Pi's `handle_gateway` node subscribes to existing topics and pushes state
to the ESP32:

```text
/motor/state        → speed
/battery/state      → battery voltage, charge %
/safety/state       → safety state
```

## Node

The **ESP32 handle unit** renders the HMI and reads input. The Pi-side
`handle_gateway` node (`golfcart_hmi`) bridges the handle to the ROS bus: it
subscribes to state topics and sends `STATE_UPDATE` frames down, and receives
`JOYSTICK` / `TOUCH` / `MENU_SELECT` / `FORCE` frames up.

The HMI must never bypass the Safety Controller. Displayed information is for
the operator only; it does not command motion.

## Future Enhancements

- Distance to green/pin (requires GPS + course data)
- Operating-mode selection
- Configuration menus