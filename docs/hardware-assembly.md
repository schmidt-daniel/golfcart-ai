# Hardware Assembly Plan

This document lays out the **order** in which to assemble the golf cart's
hardware, from the mechanical frame up to the full sensor stack. It's designed
so each stage is **testable on its own** before moving on — you can validate
each subsystem as you build it, rather than assembling everything and hoping it
works.

> **Status:** Plan. Cross-references: `docs/wiring.md` (electrical),
> `docs/pi-hat-pcb.md` (HAT), `cases_mounts/SENSOR_STACK.md` (mounting),
> `docs/features/camera-vision.md` (sensor decisions).

---

## Principle: build bottom-up, test each stage

Each stage ends with a **checkpoint** — a way to verify that stage works before
proceeding. This catches wiring/mounting errors early, when they're cheap to
fix.

---

## Stage 0 — Frame & Drive (the trolley itself)

**Goal:** a rolling trolley with powered wheels.

**Tools:** hex keys, torque wrench, cable ties, heat-shrink, multimeter.

1. Assemble the golf trolley frame (wheels, handle, bag rack).
2. Mount the two hoverboard motors + encoders (left/right).
3. Wire each motor to the ODrive 3.6:
   - `M0` (A/B) → **left** motor phase wires
   - `M1` (A/B) → **right** motor phase wires
   - Encoder 0 → left motor encoder; Encoder 1 → right motor encoder
4. Wire the 36 V battery → **fuse/circuit breaker** → ODrive `VIN`/`GND`.
5. Add the braking resistor to the ODrive `BRN`/`BRP` terminals
   (~10–15 Ω, ≥150 W, ≥60 V — see `wiring.md` §3.1).

**Checkpoint:** power the ODrive, verify it enumerates over USB and both
encoders read. (Software: `odrive_node` + `motion_controller_node`.)

> **Verify before power-on:** motor phase + encoder wiring against the ODrive
> docs, and the differential-drive sign convention (forward/left/right).

---

## Stage 1 — Compute base (Pi 5 + HAT + Coral)

**Goal:** the compute unit that everything plugs into.

**Tools:** standoffs, M2.5 screws, USB-C cable, multimeter.

1. Mount the Raspberry Pi 5 **vertically** in the compute module.
2. Seat the **Connector HAT** on the Pi's GPIO header (standoffs + screws).
3. Plug the **Coral USB Accelerator** into a Pi USB port.
4. Wire the **5 V/5 A regulator** from the battery to the Pi's USB-C power.

**Checkpoint:** Pi boots; HAT's status LED lights; Coral enumerates
(`lsusb`). (Software: `capability_node` should report ODrive + Coral present.)

> **Power sequencing:** power the ODrive before or simultaneously with the Pi.

---

## Stage 2 — Power & battery monitoring

**Goal:** measure the battery.

**Tools:** soldering iron, multimeter, 2× 100 kΩ resistors.

1. Wire the **INA219** to the HAT's I2C connector (GPIO 2/3).
2. **Voltage divider** on the INA219 VBUS sense (÷2, since 42 V > 26 V max):
   - `VBUS` → R1 (100 kΩ) → junction → R2 (100 kΩ) → GND
   - junction → INA219 `VBUS` sense pin
3. Wire the INA219 in series with the battery + lead (current sense):
   - `VIN+` → battery +; `VIN-` → ODrive power input
4. Connect `SDA` → GPIO 2, `SCL` → GPIO 3, `VCC` → 3.3 V, `GND` → GND.

**Checkpoint:** `battery_node` publishes a sane voltage/current on
`/battery/state`. (Software: `battery_node`.)

> **Note:** the measured voltage is scaled ×2 in `battery_node` to undo the
> divider. The current (shunt) measurement is unaffected.

---

## Stage 3 — Handle unit (ESP32)

**Goal:** the operator interface.

**Tools:** ribbon cable, JST connectors, USB-C cable.

1. Assemble the ESP32 handle unit (ESP32-S3 + display + joystick + load cell).
2. Connect the display (SPI + I2C), joystick (ADC), load cell (HX711).
3. Plug the handle unit into a Pi USB port (USB serial).

**Checkpoint:** the HMI boots to the splash screen; joystick/touch/force
report to the Pi. (Software: `handle_gateway` + firmware.)

---

## Stage 4 — GPS

**Goal:** position/heading.

**Tools:** USB cable, cable tie.

1. Mount the **USB GPS dongle** in the top GPS module (clear sky view).
2. Plug it into a Pi USB port.

**Checkpoint:** `gps_node` publishes a valid `/gps/fix` with satellites.
(Software: `gps_node`.)

> **Tip:** test outdoors with a clear sky — GPS needs a fix before it reports
> valid data.

---

## Stage 5 — IMU

**Goal:** attitude for tip-over/slope.

**Tools:** JST connector, orientation marker.

1. Mount the **IMU** in the Camera+IMU module.
2. Wire it to the HAT's I2C connector (GPIO 2/3, distinct address from INA219 —
   e.g. MPU-6050 `0x68` / BNO055 `0x28`).

**Checkpoint:** `imu_node` publishes valid roll/pitch. (Software: `imu_node`.)

> **Note:** the IMU and INA219 share the I2C bus (GPIO 2/3) — they MUST have
> different addresses.

---

## Stage 6 — LiDAR (horizontal)

**Goal:** obstacle stopping + follow-me legs.

**Tools:** 4-pin JST, cable tie.

1. Mount **LiDAR 1** (horizontal) in its module.
2. Wire it to the HAT's **UART0** connector (GPIO 14 TXD / GPIO 15 RXD).

**Checkpoint:** `lidar_node` publishes a `/scan`; `obstacle_detection_node`
reports obstacles. (Software: `lidar_node` + `obstacle_detection_node`.)

---

## Stage 7 — LiDAR (tilted)

**Goal:** ditch/stream detection.

**Tools:** 4-pin JST, angle gauge.

1. Mount **LiDAR 2** at **~25° down** in its module.
2. Wire it to the HAT's **UART2** connector (GPIO 0 TXD / GPIO 1 RXD).

**Checkpoint:** both LiDARs publish `/scan`; the tilted one reads the ground
plane. (Software: `lidar_node` ×2.)

> **Note:** the tilted LiDAR's scan plane must read the ground ahead — verify
> the ~25° angle with a gauge before finalizing the mount.

---

## Stage 8 — Camera

**Goal:** vision (detection/segmentation/gesture).

**Tools:** CSI ribbon cable, mounting bracket.

1. Mount the **Pi Camera Module 3** in the Camera+IMU module (forward-facing).
2. Connect it to the Pi's **CSI** port.

**Checkpoint:** `camera_node` publishes `/camera/image`; the web hazard view
shows a live feed. (Software: `camera_node` + `web_teleop_server`.)

> **Open item:** the camera mounting angle for detection/segmentation is not
> yet decided (`camera-vision.md`).

---

## Stage 9 — Assemble the sensor stack tower

**Goal:** combine everything into the final tower.

**Tools:** M3 screws, cable ties, strain relief.

1. Stack the modules (GPS on top, then LiDARs, then Camera+IMU) on the compute
   base, using the standardized screw holes.
2. Route each sensor's cable through the **common cable duct** down to the HAT
   / Pi.
3. Add strain relief so cables don't pull on the connectors.
4. Mount the tower on the trolley.

**Checkpoint:** all capabilities report present on the HMI Sensors screen.

---

## Stage 10 — Full-system validation

**Goal:** everything works together.

**Tools:** laptop (for `ros2` CLI), phone (for web app).

1. Run the full bringup (`core.launch.py`).
2. Verify all sensors on the HMI Sensors screen.
3. Test each mode: manual (joystick), follow-me, summon, drive-distance.
4. Validate the safety behaviors (obstacle stop, tip-over, geofence).
5. Run a full round end-to-end (course select → holes → end round).

**Checkpoint:** a full round works end-to-end.

---

## Suggested order summary

| Stage | What | Depends on |
| --- | --- | --- |
| 0 | Frame & drive (motors, ODrive, battery) | — |
| 1 | Compute base (Pi + HAT + Coral) | 0 (power) |
| 2 | Battery monitor (INA219) | 1 (HAT) |
| 3 | Handle unit (ESP32) | 1 (USB) |
| 4 | GPS | 1 (USB) |
| 5 | IMU | 1 (HAT I2C) |
| 6 | LiDAR horizontal | 1 (HAT UART0) |
| 7 | LiDAR tilted | 1 (HAT UART2) |
| 8 | Camera | 1 (CSI) |
| 9 | Sensor stack tower | 4–8 |
| 10 | Full-system validation | all |

---

## USB port budget (Pi 5 = 4 ports)

| # | Device | Interface |
| --- | --- | --- |
| 1 | ODrive 3.6 | USB |
| 2 | ESP32 handle unit | USB |
| 3 | USB GPS dongle | USB |
| 4 | Coral USB Accelerator | USB |
| — | Pi Camera Module 3 | CSI (no USB) |

> All 4 USB ports are used. The two LiDARs + IMU + INA219 connect via the HAT
> (UART/I2C), not USB.

---

## Pre-flight checklist (before first power-on)

- [ ] Fuse/circuit breaker between battery and ODrive.
- [ ] Motor phase + encoder wiring verified against the ODrive docs.
- [ ] Differential-drive sign convention validated (forward/left/right).
- [ ] INA219 voltage divider in place (VBUS ≤ 26 V).
- [ ] IMU and INA219 on different I2C addresses.
- [ ] Braking resistor wired to `BRN`/`BRP` (not inline with battery).
- [ ] Power sequencing: ODrive before/simultaneous with the Pi.

---

## Notes & open items

- **E-stop** is recommended but not part of the MVP (see `architecture.md` §34).
- **Camera mounting angle** for detection/segmentation is an open item
  (`camera-vision.md`).
- **Braking resistor** sizing depends on the actual cart+operator mass
  (`wiring.md` §3.1).
- The **HAT** must be fabricated (JLCPCB) before Stages 2/5/6/7 — it's the
  connector hub for the non-USB sensors.