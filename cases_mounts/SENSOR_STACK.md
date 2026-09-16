# Compute & Sensor Stack

The **Compute & Sensor Stack** is a physical tower of 3D-printed modules that
house the Raspberry Pi 5 and the trolley's sensors. Each module is custom
designed for its sensor but shares a **standardized footprint** (size + screw
holes) so the modules stack into a tower. A **common cable duct** routes each
sensor's cable down to the compute unit.

> **Status:** Design concept. The stack is the physical mounting solution for
> the sensor set decided in `docs/features/camera-vision.md` and wired in
> `docs/wiring.md` / `docs/pi-hat-pcb.md`.

---

## 1. Sensor set

| Module | Sensor | Interface | Purpose |
| --- | --- | --- | --- |
| GPS | USB GPS dongle | USB | Position / heading (SBAS) |
| LiDAR (horizontal) | FHL-LD19P | UART0 (GPIO 14/15) | Follow-me legs + obstacle stopping zone |
| LiDAR (tilted ~25°) | FHL-LD19P | UART2 (GPIO 0/1) | Ditches / streams (ground-plane breaks) |
| Camera + IMU | Pi Camera Module 3 + IMU | CSI + I2C | RGB vision (detection/segmentation/gesture) + attitude |
| Compute | Raspberry Pi 5 + HAT + Coral USB | — | Main computer + sensor connectors + NPU |

> **Battery monitor (INA219)** also sits on the HAT (I2C, GPIO 2/3) but is
> part of the power system, not the sensor tower.

---

## 2. Stack layout

The Raspberry Pi is mounted **vertically** to reduce the footprint. The sensor
modules stack above it; the Pi + HAT + Coral form the compute base.

### Side view

```text
        +--------------------+---+
        | GPS + Ground Plate     |
        +--------------------+ C |
        | LiDAR (horizontal)   a |
      +-+--------------------+ b | +-----------+
      \ | LiDAR (tilted ~25°)  l | |  Raspi 5  |
        +--------------------+ e | |     +     |
        | Camera + IMU         s | | Conn. HAT |
        +--------------------+   +-+     +     |
        |                           Coral USB  |
        +--------------------+---+-+-----------+
                 (Golf trolley)

                 ^
<-- Front    Up |
```

### Top view

```text
        +-------------------+---+
        | *                 | * |
        |   +----------+    | C |
        |   | x        |    | a |
        |   | [SENSOR] |    | b |
        |   |        x |    | l |
        |   +----------+    | e |
        | *                 | * |
        +-------------------+---+

                 ^
<-- Front   Side |
```

- `*` = stack screw holes (standardized positions, shared across all modules).
- `x` = module screw holes (individual screw holes to attach the sensor to the module, not shares across modules).
- `[SENSOR]` = the sensor's mounting pocket (custom per module).
- The right-hand column is the **cable duct** (`C a b l e`), shared by all
  modules, running cables down to the compute unit.

---

## 3. Module design rules

Each module follows the same rules so any module can stack on any other:

| Rule | Spec |
| --- | --- |
| Footprint | Standardized (same width/depth for all modules) |
| Screw holes | Standardized positions + size (e.g. M3) for tower assembly |
| Sensor pocket | Custom cutout per sensor (the only module-specific part) |
| Cable duct | Aligned opening on the same side, forming a continuous duct when stacked |
| Stacking | Modules attach with the shared screw holes; no glue |

---

## 4. Compute stack

The compute base contains:

- **Raspberry Pi 5** — mounted vertically to reduce footprint.
- **Connector HAT** — the sensor interface board (`docs/pi-hat-pcb.md`). It
  provides the connectors for all **non-USB** sensors (LiDAR ×2 on UART0/UART2,
  IMU + INA219 on I2C), so sensors plug into the HAT instead of loose wires to
  the Pi's GPIO.
- **Coral USB Accelerator** — the NPU for vision inference (detection /
  segmentation / gesture). Plugs into a Pi USB port.

> **USB devices** (ODrive, ESP32 handle unit, USB GPS dongle, Coral) plug into
> the Pi's USB ports directly and are **not** on the HAT. The HAT only routes
> the non-USB sensors (see `docs/pi-hat-pcb.md`).

---

## 5. Cable routing

A **common cable duct** runs down one side of the tower. Each sensor's cable
enters the duct at its module and exits at the compute base, where it plugs
into the HAT connector (non-USB) or a Pi USB port (USB).

| Sensor | Cable lands on | Connector |
| --- | --- | --- |
| GPS (USB dongle) | Pi USB port | USB |
| LiDAR 1 (horizontal) | HAT | UART0 (GPIO 14/15) |
| LiDAR 2 (tilted) | HAT | UART2 (GPIO 0/1) |
| Camera | Pi CSI port | CSI |
| IMU | HAT | I2C (GPIO 2/3) |

---

## 6. Mounting notes

- **LiDAR 2 is tilted ~25° down** so its 2D scan plane reads the ground ahead
  for ditches/streams. The module's sensor pocket must hold it at that angle.
- **Camera** faces forward for detection/segmentation/gesture; its exact
  mounting angle is an open item (see `docs/features/camera-vision.md`).
- **GPS** sits on top with a **ground plate** (the top module) for a clear sky
  view.
- The whole tower mounts on the **golf trolley** (e.g. on the handle or a
  bracket); the exact attachment is a hardware-phase decision.

---

## 7. Open items

- Camera mounting angle for detection/segmentation.
- Tower attachment point on the trolley.
- Whether Pi 5 CPU-only is acceptable before adding the Coral.