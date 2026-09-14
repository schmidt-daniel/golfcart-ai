# Handle Unit PCB — JLCPCB Design Specification

> **Status:** Design spec (pre-EDA). This document is the input to an EDA tool
> (EasyEDA / KiCad) that generates the Gerber + BOM + drill files JLCPCB needs.
> It captures every design decision so the EDA step is mechanical.
>
> **Scope:** the ESP32 handle unit — display, touch, joystick, load cell, and
> the Pi link — on a single custom PCB. See `docs/handle-protocol.md` §12 for
> the hardware decisions this spec builds on.

---

## 1. Design intent

| Requirement | Decision |
| --- | --- |
| ESP32 | **ESP32-S3-WROOM-1 (N8R8)** — 8 MB flash + 8 MB PSRAM, soldered onto the PCB |
| Display | Elecrow 3.5" IPS SPI LCD Touch (ST7796, 320×480), **plugs into a female header on the PCB** (no flying cable) |
| Display mounting | PCB + display joined by **standoff spacers + screws** |
| Touch | FT6336U (I2C), read by the ESP32 |
| Joystick | 2-axis analog + push button, read via ADC |
| Load cell | **HX711 amplifier soldered on the PCB**; the load cell plugs in via a small connector |
| Backlight | **Controllable** (PWM, GPIO 9) — dim/off when idle |
| Power | **Pi-powered over USB-C** (see §6) |
| Pi link | USB-C (CDC serial) to the Raspberry Pi |

---

## 2. Board overview

```text
        ┌──────────────────────────────────────────────┐
        │              E L E C R O W  3.5"             │
        │          (display module, plugs in)          │
        │                                              │
        │   ┌──────────────────────────────────────┐   │
        │   │          (viewing area)              │   │
        │   └──────────────────────────────────────┘   │
        └──────────────────────────────────────────────┘
        ▲  standoff spacers + screws (4×, corners)
        ┌──────────────────────────────────────────────┐
        │  [ESP32-S3 WROOM-1]      [HX711]  [LC conn]  │
        │                                              │
        │  [USB-C]   [joystick conn]  [button]         │
        └──────────────────────────────────────────────┘
```

- **Top side:** the display module sits on standoffs above the PCB; the WROOM-1
  module, HX711, USB-C, and connectors are on the PCB.
- **Board size (suggested):** ~110 × 70 mm (fits the 3.5" display footprint
  plus a margin for the WROOM-1 module and connectors). **Confirm** against the
  display module's exact outer dimensions.

---

## 3. Component selection & BOM (JLCPCB)

> JLCPCB part numbers are for the **PCB fabrication** (board, finish, silkscreen)
> plus **assembly** (they can solder the through-hole/SMD parts you supply or
> source). The module/display/joystick/load-cell are **customer-supplied parts**
> you ship to JLCPCB or solder yourself. **Verify current part numbers** on the
> JLCPCB site before ordering.

### 3.1 PCB fabrication

| Item | Spec |
| --- | --- |
| Board size | ~110 × 70 mm (confirm) |
| Layers | 2 (double-sided) |
| Material | FR-4, 1.6 mm (or 1.0 mm if you want a thinner handle) |
| Copper weight | 1 oz (35 µm) |
| Surface finish | ENIG (or HASL) |
| Silkscreen | 1 color (black) on top |
| Edge | no edge plating (or plated if you want a nicer edge) |

### 3.2 Customer-supplied parts (solder/assemble yourself or ship to JLCPCB)

| Part | Notes |
| --- | --- |
| **ESP32-S3-WROOM-1 (N8R8)** | 8 MB flash + 8 MB PSRAM. Soldered onto the PCB (castellated edge pads). Matches the `esp32-s3-devkitc-1` board target in `platformio.ini`. |
| **Elecrow 3.5" display** | ST7796, 320×480, FT6336U touch, 14-pin header |
| **2-axis analog joystick** | with push button (e.g. PS2-style thumbstick) |
| **Load cell** | e.g. 50 kg straight-bar load cell (for push force) |
| **HX711** | amplifier module (soldered to the PCB) |

### 3.3 PCB-mounted parts (JLCPCB can assemble)

| Part | Type | Qty |
| --- | --- | --- |
| HX711 amplifier | module footprint | 1 |
| USB-C receptacle | SMD | 1 |
| Joystick connector | e.g. JST-PH 2.0 (5-pin) | 1 |
| Load-cell connector | e.g. JST-PH 2.0 (4-pin) | 1 |
| Display female header | 14-pin (matches display) | 1 |
| Standoff inserts | M2 brass inserts (4×) | 4 |

---

## 4. Pin mapping (ESP32-S3-WROOM-1)

> These match the firmware constants in `src/golfcart_hmi/firmware/`. **Verify**
> the exact GPIO numbering against the WROOM-1 module's pinout when laying out
> the PCB.

| Function | Firmware constant | GPIO | Notes |
| --- | --- | --- | --- |
| Joystick X | `JOY_X_PIN 4` | GPIO4 (ADC) | analog |
| Joystick Y | `JOY_Y_PIN 5` | GPIO5 (ADC) | analog |
| Joystick button | `JOY_BTN_PIN 6` | GPIO6 | digital |
| HX711 DOUT | `HX711_DOUT_PIN 7` | GPIO7 | data |
| HX711 SCK | `HX711_SCK_PIN 8` | GPIO8 | clock |
| **Backlight** | `BACKLIGHT_PIN 9` | GPIO9 | PWM (dim/off) |
| Display SPI | — | module SPI pins | ST7796 |
| Touch I2C | — | module I2C pins | FT6336U @ 0x38 |

### 4.1 Display 14-pin header (Elecrow, authoritative)

From the Elecrow datasheet:

| Pin | Label | Description | Function |
| --- | --- | --- | --- |
| 1 | VCC | Power positive | 5V power |
| 2 | GND | Power ground | GND |
| 3 | LCD_CS | LCD selection control, low = active | SPI CS |
| 4 | LCD_RST | LCD reset control, low = reset | Reset |
| 5 | LCD_RS | Command/data select (high=data, low=command) | SPI DC |
| 6 | SDI (MOSI) | SPI write data (SD + LCD) | SPI MOSI |
| 7 | SCK | SPI clock (SD + LCD) | SPI SCLK |
| 8 | LED | LCD backlight control | **Backlight (GPIO 9)** |
| 9 | SDO (MISO) | SPI read data (SD + LCD) | SPI MISO |
| 10 | CTP_SCL | Touch I2C clock | I2C SCL |
| 11 | CTP_RST | Touch reset control, low = reset | Touch reset |
| 12 | CTP_SDA | Touch I2C data | I2C SDA |
| 13 | CTP_INT | Touch interrupt (low on touch) | Touch interrupt |
| 14 | SD_CS | SD card select (optional) | SD CS (unused) |

**Display → ESP32 mapping:**

| Display pin | Signal | ESP32-S3 GPIO | Firmware |
| --- | --- | --- | --- |
| 6 (SDI) | SPI MOSI | SPI MOSI | TFT_eSPI |
| 7 (SCK) | SPI SCLK | SPI SCLK | TFT_eSPI |
| 9 (SDO) | SPI MISO | SPI MISO | TFT_eSPI |
| 3 (LCD_CS) | SPI CS | SPI CS | TFT_eSPI |
| 5 (LCD_RS) | SPI DC | SPI DC | TFT_eSPI |
| 4 (LCD_RST) | LCD reset | GPIO | TFT_eSPI |
| 8 (LED) | Backlight | GPIO 9 | `screens_set_backlight()` |
| 10 (CTP_SCL) | I2C SCL | I2C SCL | Wire |
| 12 (CTP_SDA) | I2C SDA | I2C SDA | Wire |
| 11 (CTP_RST) | Touch reset | GPIO | sensors.cpp |
| 13 (CTP_INT) | Touch interrupt | GPIO | sensors.cpp |
| 14 (SD_CS) | SD CS | — | unused |
| 1 (VCC) | Power | — | 5V |
| 2 (GND) | GND | — | GND |

---

## 5. Board layout concept

### 5.1 Zones

1. **Display zone (top):** the 14-pin female header + 4 standoff inserts at the
   corners, positioned to match the display module's screw holes.
2. **ESP32 zone (bottom-left):** the WROOM-1 module footprint.
3. **HX711 zone (bottom-center):** the amplifier footprint + the load-cell
   connector.
4. **I/O zone (bottom-right):** USB-C receptacle + joystick connector.

### 5.2 Routing

- Keep the **SPI** traces (display) short and away from the **I2C** (touch) and
  the **HX711** to avoid crosstalk.
- The **joystick** and **load-cell** connectors are on the board edge so their
  cables exit cleanly into the handle.
- **USB-C** on the bottom edge for the Pi tether.

### 5.3 Mounting

- **Standoffs:** M2 standoff spacers (e.g. 10–15 mm) at the 4 display corners,
  screwed into brass inserts in the PCB. The display module's own screw holes
  align with these.
- **Confirm** the display module's hole pattern and thread size before fixing
  the insert positions.

---

## 6. Power budget (Pi-powered over USB-C)

From `docs/handle-protocol.md` §12.1:

| Component | Typical draw |
| --- | --- |
| ESP32-S3 | ~0.2–0.5 W |
| ST7796 + backlight | ~0.5–1.0 W |
| FT6336U touch | ~0.1 W |
| HX711 + load cell | ~0.05 W |
| **Total** | **~0.9–1.7 W** |

A Raspberry Pi 5 USB port supplies up to ~7.5 W (5 V × 1.5 A), so the handle
(~1–2 W) is well within budget. **Verify** the Pi's PSU is ≥ 27 W to sustain
full USB output.

- **USB-C:** use a USB-C receptacle wired for **power + data** (CDC serial).
  The ESP32 enumerates as a serial device to the Pi.
- **No separate power feed** needed for the tethered design.

---

## 7. EDA workflow (EasyEDA → JLCPCB)

1. **Create the board** in EasyEDA at the size in §3.1.
2. **Place the footprints:** WROOM-1 module, 14-pin display header, HX711,
   USB-C, joystick + load-cell connectors, 4 standoff inserts.
3. **Route the traces** per §5.2 and the pin map in §4.
4. **Run the DRC** (clearance, trace width, hole spacing).
5. **Export:**
   - Gerber files (copper top/bottom, silkscreen, solder mask, drill)
   - BOM (with JLCPCB part numbers)
   - Drill file
6. **Upload to JLCPCB** with the fabrication spec in §3.1.

---

## 8. Open items (verify before ordering)

- [ ] **WROOM-1 module pinout** — confirm the exact GPIO numbering matches §4.
- [ ] **Display module outer dimensions + screw-hole pattern** — for board size
      and standoff positions.
- [ ] **Joystick + load-cell connector types** — JST-PH sizes.
- [ ] **Standoff length** — enough to clear the WROOM-1 module height.
- [ ] **FT6336U library support** on the ESP32 (I2C driver / LVGL touch backend).
- [ ] **Touch ↔ display orientation** — confirm axes line up (or rotate in FW).
- [ ] **Backlight control** — GPIO 9 PWM; confirm the WROOM-1 pin is free.