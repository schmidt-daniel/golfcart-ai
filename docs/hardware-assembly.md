# Hardware Assembly Plan

This document lays out the **order** in which to assemble the golf cart's
hardware, from the mechanical frame up to the full sensor stack. It's designed
so each stage is **testable on its own** before moving on — you can validate
each subsystem as you build it, rather than assembling everything and hoping it
works.

> **Status:** Plan. Cross-references: `docs/wiring.md` (electrical),
> `docs/pi-hat-pcb.md` (HAT), `cases_mounts/SENSOR_STACK.md` (mounting),
> `docs/features/camera-vision.md` (sensor decisions).

## Bring-up commands

Run these commands on the Pi after sourcing ROS 2 and the workspace in every
terminal:

```bash
source /opt/ros/lyrical/setup.bash
cd ~/golfcart-ai
source install/setup.bash
```

### Check the ODrive

Keep the drive wheels off the ground for the first test. Before starting ROS,
confirm that Linux sees the ODrive and identify its stable serial path:

```bash
lsusb
ls -l /dev/serial/by-id/
dmesg --follow
```

In a second terminal, run only the ODrive node, replacing the device path with
the path found above:

```bash
ros2 run golfcart_odrive odrive_node --ros-args \
   -p implementation:=odrive \
   -p device:=/dev/serial/by-id/USB-ODrive
```

In a third terminal, inspect the state and fault fields:

```bash
ros2 topic hz /motor/state
ros2 topic echo --once /motor/state --qos-reliability best_effort
```

A healthy idle result should update at about 20 Hz, report `state: idle`, an
empty `fault`, and stable encoder positions. Rotate each wheel by hand and
repeat the echo; the corresponding position/velocity should change. Do not
publish a non-zero motor command until the motor direction and emergency stop
have been verified:

```bash
ros2 topic echo /motor/state --qos-reliability best_effort
```

The current driver reads ODrive state through its JSON serial protocol, but it
does not configure or tune the axes. The motors and encoders must already be
configured on the ODrive; an `idle` state proves communication, not that a
motor can safely drive. Stop the test with `Ctrl-C`.

### Check ROS nodes and sensor topics

Start the base stack in a separate terminal:

```bash
ros2 launch golfcart_bringup core.launch.py implementation:=odrive
```

The launch file currently uses mock implementations for the IMU, GPS, and
LiDAR unless their nodes are started separately with `implementation:=real`.
Check that nodes, publishers, and rates exist:

```bash
ros2 node list
ros2 topic list
ros2 topic hz /motor/state
ros2 topic hz /battery/state
ros2 topic hz /imu/data
ros2 topic hz /gps/fix
ros2 topic hz /scan
ros2 topic hz /camera/image
```

For a representative message from each topic:

```bash
ros2 topic echo --once /motor/state --qos-reliability best_effort
ros2 topic echo --once /battery/state --qos-reliability best_effort
ros2 topic echo --once /imu/data --qos-reliability best_effort
ros2 topic echo --once /gps/fix --qos-reliability best_effort
ros2 topic echo --once /scan --qos-reliability best_effort
ros2 topic echo --once /camera/image --qos-reliability best_effort
```

For real sensor checks, stop the mock node from the full launch first, then
start the relevant node directly:

```bash
# IMU on the HAT I2C bus
ros2 run golfcart_imu imu_node --ros-args \
   -p implementation:=real -p device:=/dev/i2c-1

# GPSD owns the USB serial device; use the actual /dev/serial/by-id path
sudo apt install gpsd gpsd-clients
sudo systemctl stop gpsd.socket gpsd
sudo gpsd -N -n /dev/serial/by-id/USB-GPS

# In another terminal, verify gpsd receives NMEA and emits TPV/SKY JSON.
cgps -s
gpspipe -w

# The ROS node connects to gpsd on localhost:2947.
ros2 run golfcart_gps gps_node --ros-args \
   -p implementation:=real -p device:=/dev/serial/by-id/USB-GPS

# LiDAR; use a unique device path for each LiDAR
ros2 run golfcart_lidar lidar_node --ros-args \
   -p implementation:=real -p device:=/dev/serial/by-id/USB-LiDAR
```

Then run the matching `ros2 topic hz` and `ros2 topic echo --once` commands
above. Expected rates are approximately 50 Hz for IMU, 1 Hz for GPS, 10 Hz
for LiDAR, and 1 Hz for the battery monitor. A topic that exists but reports
`valid: false`, zero values, or never changes is not a successful hardware
check.

For the HAT and camera hardware, use the Linux-level checks as well:

```bash
# INA219 and IMU should appear at their configured, different I2C addresses.
sudo i2cdetect -y 1

# Pi Camera Module 3 should be listed by the Raspberry Pi camera stack.
rpicam-hello --list-cameras
```

`/camera/image` is currently published by `camera_node` as a placeholder when
no capture backend is connected, so a topic rate alone does not prove that the
camera is producing real images. Confirm the camera with `rpicam-hello` and a
non-blank image before accepting the camera checkpoint.

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

**Checkpoint:** power the ODrive, verify it enumerates over USB, then follow
the ODrive commands in [Bring-up commands](#check-the-odrive). Both axes must
report no fault and both encoders must change when their wheels are rotated by
hand. (Software: `odrive_node` + `motion_controller_node`.)

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
`/battery/state`; check it with `ros2 topic hz /battery/state` and
`ros2 topic echo --once /battery/state --qos-reliability best_effort`.
(Software: `battery_node`.)

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

**Checkpoint:** `gps_node` publishes a valid `/gps/fix` with satellites; check
it with `ros2 topic echo --once /gps/fix --qos-reliability best_effort`.
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

**Checkpoint:** `imu_node` publishes valid roll/pitch; check it with
`ros2 topic hz /imu/data` and `ros2 topic echo --once /imu/data
--qos-reliability best_effort` while gently tilting the cart. (Software:
`imu_node`.)

> **Note:** the IMU and INA219 share the I2C bus (GPIO 2/3) — they MUST have
> different addresses.

---

## Stage 6 — LiDAR (horizontal)

**Goal:** obstacle stopping + follow-me legs.

**Tools:** 4-pin JST, cable tie.

1. Mount **LiDAR 1** (horizontal) in its module.
2. Wire it to the HAT's **UART0** connector (GPIO 14 TXD / GPIO 15 RXD).

**Checkpoint:** `lidar_node` publishes a changing `/scan`; check it with
`ros2 topic hz /scan` and `ros2 topic echo --once /scan
--qos-reliability best_effort` while placing an object in front of the sensor.
`obstacle_detection_node` should also be running. (Software: `lidar_node` +
`obstacle_detection_node`.)

---

## Stage 7 — LiDAR (tilted)

**Goal:** ditch/stream detection.

**Tools:** 4-pin JST, angle gauge.

1. Mount **LiDAR 2** at **~25° down** in its module.
2. Wire it to the HAT's **UART2** connector (GPIO 0 TXD / GPIO 1 RXD).

**Checkpoint:** both LiDARs publish `/scan`; the tilted one reads the ground
plane. Check the scan rate and ranges with `ros2 topic hz /scan` and
`ros2 topic echo --once /scan --qos-reliability best_effort`. Run each LiDAR
with its own serial device path. (Software: `lidar_node` ×2.)

> **Note:** the tilted LiDAR's scan plane must read the ground ahead — verify
> the ~25° angle with a gauge before finalizing the mount.

---

## Stage 8 — Camera

**Goal:** vision (detection/segmentation/gesture).

**Tools:** CSI ribbon cable, mounting bracket.

1. Mount the **Pi Camera Module 3** in the Camera+IMU module (forward-facing).
2. Connect it to the Pi's **CSI** port.

**Checkpoint:** `rpicam-hello --list-cameras` finds the camera,
`camera_node` publishes `/camera/image`, and the web hazard view shows a live,
non-blank feed. Check the topic with `ros2 topic hz /camera/image`; see the
camera caveat in [Bring-up commands](#check-ros-nodes-and-sensor-topics).
(Software: `camera_node` + `web_teleop_server`.)

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

## Compile & Flash

This section covers how to build and flash the software onto the hardware.
Two pieces of software need to be built:

1. **The ROS 2 workspace** (runs on the Raspberry Pi 5).
2. **The ESP32 handle-unit firmware** (runs on the ESP32-S3).

### Prerequisites

- A host with Docker (the build container `golfcart:lyrical` includes ROS 2
  Lyrical + PlatformIO). See `README.md` → "Build".
- The Pi 5 and ESP32 connected to the host (or the Pi itself used as the host).

### 1. Build the ROS 2 workspace

**Option A — Docker (recommended):**

```bash
# Build the image (first time) + colcon build & tests:
./docker/build.sh test

# Or just build (no tests):
./docker/build.sh
```

**Option B — Native (on the Pi):**

```bash
source /opt/ros/lyrical/setup.bash
cd <workspace>
colcon build
source install/setup.bash
```

> **OOM protection:** the build host has limited RAM. Use the memory-limited
> helper for ad-hoc builds:
> ```bash
> ./docker/docker_run.sh golfcart:lyrical /workspace \
>     'source /opt/ros/${ROS_DISTRO}/setup.bash && colcon build'
> ```

### 2. Build & flash the ESP32 firmware

The firmware lives in `src/golfcart_hmi/firmware`.

**Build (two levels):**

```bash
cd src/golfcart_hmi/firmware

# Fast: compile the pure-C protocol layer only (no deps):
bash build_firmware.sh protocol

# Full: complete PlatformIO build (downloads ESP32 toolchain + LVGL/TFT_eSPI/
# HX711 on first run):
bash build_firmware.sh full
```

**Flash (upload) to the ESP32:**

```bash
cd src/golfcart_hmi/firmware
pio run -e esp32s3 -t upload
```

> The ESP32 connects to the host over USB. PlatformIO auto-detects the serial
> port; if it doesn't, set it in `platformio.ini` (`upload_port`).

### 3. Deploy the workspace to the Pi

Once built, deploy the ROS 2 workspace to the Pi (see `docs/architecture.md`
§34.1 for the deployment options). The standard flow uses `rsync` + systemd
units.

### 4. Verify

- **ESP32:** the HMI boots to the splash screen; the Pi's `handle_gateway`
  connects over serial.
- **Pi:** `ros2 node list` shows the expected nodes; `ros2 topic list` shows
  the sensor topics.

---

## Notes & open items

- **E-stop** is recommended but not part of the MVP (see `architecture.md` §34).
- **Camera mounting angle** for detection/segmentation is an open item
  (`camera-vision.md`).
- **Braking resistor** sizing depends on the actual cart+operator mass
  (`wiring.md` §3.1).
- The **HAT** must be fabricated (JLCPCB) before Stages 2/5/6/7 — it's the
  connector hub for the non-USB sensors.