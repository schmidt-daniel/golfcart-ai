# Raspberry Pi HAT — Sensor Interface Board (JLCPCB Design Specification)

> **Status:** Design spec (pre-EDA). This document is the input to an EDA tool
> (EasyEDA / KiCad) that generates the Gerber + BOM + drill files JLCPCB needs.
> It captures every design decision so the EDA step is mechanical.
>
> **Scope:** a Raspberry Pi **HAT** that provides connectors for all sensors
> that connect **directly** to the Pi (i.e. not over USB). USB-connected devices
> (ODrive, ESP32 handle unit, USB GPS dongle, Coral USB Accelerator) are **out of
> scope** — they plug into the Pi's USB ports directly.
>
> **Board form factor:** Raspberry Pi HAT (standard 58 × 49.5 mm footprint,
> 40-pin GPIO header, mounting holes at the standard HAT positions).

---

## 1. Design intent

| Requirement | Decision |
| --- | --- |
| Form factor | **Raspberry Pi HAT** — mounts on the Pi 5 GPIO header, standard HAT footprint + mounting holes |
| Purpose | Provide **connectors** for all non-USB sensors, so they plug into the HAT instead of loose wires to the Pi's GPIO |
| I2C | **INA219 battery monitor** + **IMU** (e.g. MPU-6050 / BNO055) — both on the I2C bus |
| UART | **LiDAR ×2** (FHL-LD19P) — LiDAR 1 on UART0 (GPIO 14/15), LiDAR 2 on UART2 (GPIO 0/1) |
| GPS | **USB GPS dongle** — NOT on the HAT (frees the hardware UART for the LiDAR) |
| Power | 5 V from the Pi's GPIO (or a dedicated 5 V regulator input) for the sensors |
| Out of scope | USB devices (ODrive, ESP32 handle, USB GPS dongle, Coral USB Accelerator) — plug into Pi USB directly |

---

## 2. Board overview

```text
        ┌──────────────────────────────────────────────┐
        │              Raspberry Pi 5                  │
        │          (HAT mounts on GPIO header)         │
        └──────────────────────────────────────────────┘
        ▲  standoff spacers + screws (4×, HAT corners)
        ┌──────────────────────────────────────────────┐
        │  [INA219 conn]  [IMU conn]                   │
        │                                              │
        │  [LiDAR 1 conn]  [LiDAR 2 conn]              │
        │                                              │
        │  [5V/GND in]    [status LED]                 │
        └──────────────────────────────────────────────┘
```

- **Bottom side:** the 40-pin GPIO header (female) plugs onto the Pi's GPIO
  pins. The HAT routes the needed signals to the sensor connectors.
- **Top side:** the sensor connectors (I2C, UART, power) and a status LED.

---

## 3. Sensor connections (non-USB)

| Sensor | Interface | Pi GPIO / Pin | Connector on HAT |
| --- | --- | --- | --- |
| **INA219 battery monitor** | I2C | GPIO 2 (SDA), GPIO 3 (SCL) | 4-pin (SDA, SCL, 3V3, GND) |
| **IMU** (MPU-6050 / BNO055) | I2C | GPIO 2 (SDA), GPIO 3 (SCL) | 4-pin (SDA, SCL, 3V3, GND) |
| **LiDAR 1** (FHL-LD19P, horizontal) | UART0 | GPIO 14 (TXD), GPIO 15 (RXD) | 4-pin (TXD, RXD, 3V3, GND) |
| **LiDAR 2** (FHL-LD19P, tilted) | UART2 | GPIO 0 (TXD), GPIO 1 (RXD) | 4-pin (TXD, RXD, 3V3, GND) |

> **Two LiDARs, two UARTs:** the Pi 5 exposes **6 UARTs** (UART0–UART5) on the
> GPIO header (unlike the Pi 4's single usable UART). LiDAR 1 (horizontal,
> follow-me/obstacles) uses UART0; LiDAR 2 (tilted, ditches/streams) uses
> UART2. The HAT routes both UARTs to the two LiDAR connectors — **no USB
> adapter needed** (USB ports are full: ODrive, ESP32, GPS, Coral).

> **I2C bus sharing:** the INA219 and IMU share the same I2C bus (GPIO 2/3).
> They must have **different I2C addresses** (INA219 default `0x40`; IMU
> typically `0x68` for MPU-6050 / `0x28` for BNO055). The HAT just routes the
> bus to both connectors — no address conflict handling on the board.
>
> **GPS:** a **USB GPS dongle** (NMEA over USB serial) is used instead of a
> UART GPS. This keeps the Pi's UARTs free for the **two LiDARs** (UART0 +
> UART2). The GPS dongle plugs into a Pi USB port (out of scope for the HAT).

---

## 4. Component selection & BOM (JLCPCB)

> JLCPCB part numbers are for the **PCB fabrication** (board, finish, silkscreen)
> plus **assembly** (they can solder the through-hole/SMD parts you supply or
> source). The sensors themselves are **customer-supplied parts** you ship to
> JLCPCB or solder yourself. **Verify current part numbers** on the JLCPCB site
> before ordering.

### 4.1 PCB fabrication

| Item | Spec |
| --- | --- |
| Board size | Raspberry Pi HAT footprint (58 × 49.5 mm) |
| Layers | 2 (double-sided) |
| Material | FR-4, 1.6 mm |
| Copper weight | 1 oz (35 µm) |
| Surface finish | ENIG (or HASL) |
| Silkscreen | 1 color (black) on top |
| Edge | no edge plating |
| Mounting holes | Standard HAT positions (4×, for M2.5 standoffs) |

### 4.2 Customer-supplied parts (solder/assemble yourself or ship to JLCPCB)

| Part | Notes |
| --- | --- |
| **40-pin GPIO header (female)** | Plugs onto the Pi 5 GPIO pins. Standard HAT header. |
| **INA219 battery monitor** | I2C, address `0x40`. Plugs into the INA219 connector. |
| **IMU** (MPU-6050 / BNO055) | I2C. Plugs into the IMU connector. |
| **LiDAR 1** (FHL-LD19P, horizontal) | UART0. Plugs into the LiDAR 1 connector. |
| **LiDAR 2** (FHL-LD19P, tilted) | UART2. Plugs into the LiDAR 2 connector. |
| **USB GPS dongle** | USB (out of scope — plugs into a Pi USB port). |
| **Status LED** | Optional; indicates HAT power / I2C activity. |

---

## 5. Power

- The HAT draws **5 V** from the Pi's GPIO (the Pi is powered by a 5 V/5 A
  regulator from the 36 V battery — see `docs/wiring.md` §2).
- The INA219 and IMU run on **3.3 V** (from the Pi's 3V3 GPIO pin).
- The GPS and LiDAR may need **5 V** (check each module's spec); the HAT routes
  the appropriate rail to each connector.
- A **fuse** is not needed on the HAT (the sensors are low-power); the main
  battery fuse is upstream (see `docs/wiring.md` §6).

---

## 6. Status LED

- A small SMD LED (e.g. green) on the HAT, driven by a GPIO pin, indicates the
  HAT is powered and the I2C bus is alive. Optional — can be omitted to keep
  the board minimal.

---

## 7. Open decisions (confirm before EDA)

1. **IMU model** — MPU-6050 vs BNO055 (affects I2C address and connector pinout).
2. **LiDAR power rail** — 3.3 V vs 5 V for the FHL-LD19P.
3. **Status LED** — include or omit.
4. **Connector type** — JST, Molex, or bare pads for each sensor connector.

---

## 8. Validation

- **I2C:** both INA219 and IMU appear on the I2C bus (`i2cdetect -y 1`) at their
  addresses.
- **UART:** LiDAR 1 produces serial data on UART0 (GPIO 14/15); LiDAR 2 on
  UART2 (GPIO 0/1).
- **GPS:** the USB GPS dongle produces NMEA data on its USB serial port.
- **Power:** all sensors powered and reporting valid data.
- **Software:** `battery_node`, `imu_node`, `gps_node`, `lidar_node` all report
  `valid=true` (currently scaffolds using mocks — see `docs/architecture.md`).