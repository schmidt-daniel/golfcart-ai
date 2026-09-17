# ESP32 Handle-Unit Firmware

Firmware for the ESP32-S3 handle unit (HMI + physical sensor interface).

## Hardware

- **ESP32-S3 with PSRAM**
- **Elecrow 3.5" IPS SPI LCD Touch** (ST7796, 320×480 portrait, FT6336U touch)
- **2-axis analog joystick** + button (read via ADC)
- **HX711 load cell** (push force)

## Layout

```
src/
  handle_protocol.h/.c   # serial protocol (mirrors golfcart_hmi/protocol.py)
  main.ino               # setup/loop: serial, input sampling, LVGL tick
```

## Build

```bash
cd firmware
pio run -e esp32s3
pio run -e esp32s3 -t upload
```

## Status

- **Protocol layer:** complete and testable (framing, CRC-16, byte-stuffing,
  message/state tables).
- **LVGL screens, FT6336U touch, HX711:** implemented — the FT6336U is
  registered as LVGL's pointer input device; the HX711 load cell is read via
  the HX711 library. Pin/register assignments should be confirmed against the
  actual hardware wiring.

## Protocol

See `docs/handle-protocol.md` for the full spec. The ESP32 sends `HELLO` on
boot with its capabilities, then streams `JOYSTICK` / `FORCE` at fixed cadence
and `MENU_SELECT` / `TOUCH` on events. It renders `SCREEN_NAV` /
`STATE_UPDATE` / `DEBUG_SUMMARY` frames from the Pi.