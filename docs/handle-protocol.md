# Handle Unit Serial Protocol — Raspberry Pi ↔ ESP32

> **Status:** Design
> **Purpose:** Define the single USB serial link between the Raspberry Pi (front
> of the cart, runs ROS) and the ESP32 handle unit (display + joystick + touch +
> load cell).
> **Goal:** One cable. All HMI rendering and input handling live on the ESP32;
> the Pi sends display state and receives input events.
> **Display:** Elecrow 3.5" IPS SPI LCD Touch (ST7796, 320×480) — see §10.
> **Handle interface:** the ESP32 is the **sole** handle interface — display,
> touch, joystick, and load cell all connect to it. **No Arduino.**

---

## 1. Overview

The ESP32 handle unit **replaces** the Pi-side HMI (`hmi_node.py` /
`hmi_draw.py`). The Pi keeps all ROS logic and exposes a thin serial gateway;
the ESP32 renders screens locally (LVGL) and reports input back.

```text
RASPBERRY PI (front)                        ESP32 HANDLE UNIT
─────────────────────────────               ─────────────────────────────
ROS nodes (battery, GPS, IMU,               LVGL screens (menu, hole, debug)
  LiDAR, safety, nav, costmap)              • joystick (analog)
        │  downlink: display state          • touch (capacitive/resistive)
        │  uplink: input + force            • load cell (HX711) → push force
        ▼                                   • one USB serial link
   serial gateway  ──────────────────────►
```

### 1.1 Responsibilities

| Side | Owns |
| --- | --- |
| **Pi** | All ROS state, screen *navigation decisions*, state values, debug summaries, **load-cell calibration** |
| **ESP32** | **HMI** (rendering, input capture) + **physical sensor interface** (joystick, touch, load-cell sampling) |

> **ESP32's primary role:** the HMI and the physical interface for the handle
> sensors (joystick, touch, load cell). It samples and forwards raw sensor data;
> it does **not** own calibration or control logic — those live on the Pi.

### 1.2 Design principles

1. **The Pi is the source of truth** for system state. The ESP32 never invents
   values; it only renders what the Pi sends.
2. **The ESP32 is the source of truth** for input. The Pi never guesses what
   the operator did; it acts on ESP32 events.
3. **Safety-relevant data is never dropped.** The load-cell force and the
   joystick button are high-priority and must not be starved by display traffic.
4. **Lossy is fine for display state** (a dropped battery % is harmless); it is
   **not** fine for input/force (a dropped stop or force sample is not).
5. **Versioned.** Both ends negotiate a protocol version on connect.

---

## 2. Transport

| Item | Value |
| --- | --- |
| Physical | USB serial (CDC) — the ESP32 enumerates as a serial device |
| Baud | **460800** (safe for USB CDC; gives ample headroom for force/joystick + debug) |
| Framing | Binary, length-prefixed, CRC-16 |
| Direction | Full-duplex (separate downlink/uplink streams) |

### 2.1 Frame format

```
+--------+--------+--------+--------+--------+------------------+--------+
| 0xAA   | 0x55   | TYPE   | LEN    | SEQ    | PAYLOAD[LEN]     | CRC16  |
| start  | start  | 1 byte | 1 byte | 1 byte | LEN bytes        | 2 bytes|
+--------+--------+--------+--------+--------+------------------+--------+
```

| Field | Size | Notes |
| --- | --- | --- |
| `0xAA 0x55` | 2 | Start-of-frame marker (byte-stuffed if it appears in payload) |
| `TYPE` | 1 | Message type (see §3) |
| `LEN` | 1 | Payload length (0–255) |
| `SEQ` | 1 | Sequence number (increments per direction; used for loss detection) |
| `PAYLOAD` | LEN | Message body |
| `CRC16` | 2 | CRC-16/CCITT over TYPE..PAYLOAD |

**Byte stuffing:** if `0xAA` or `0x55` appears in the payload, escape it with
`0xDB` + `(byte ^ 0x20)` to keep the frame unambiguous.

### 2.2 Rates

| Stream | Direction | Rate | Priority |
| --- | --- | --- | --- |
| Display state | Downlink | 10 Hz (throttled) | Low |
| Debug summaries | Downlink | 1–2 Hz | Low |
| Screen nav | Downlink | on-change | Low |
| Joystick | Uplink | 50 Hz | High |
| Load-cell force | Uplink | 50–100 Hz | **Critical** |
| Touch / menu | Uplink | on-event | High |
| Heartbeat | Both | 1 Hz | Low |

---

## 3. Message Types

### 3.1 Downlink (Pi → ESP32)

| TYPE | Name | Payload |
| --- | --- | --- |
| `0x01` | `HELLO` | protocol version (1 byte) |
| `0x02` | `SCREEN_NAV` | screen id (1 byte) + optional arg (varies) |
| `0x03` | `STATE_UPDATE` | state id (1 byte) + value (see §4) |
| `0x04` | `DEBUG_SUMMARY` | debug id (1 byte) + summary payload |
| `0x05` | `CONFIG` | config id (1 byte) + value |
| `0x06` | `ACK` | acked seq (1 byte) + status (1 byte) |
| `0x07` | `BOOT_STATUS` | progress (1 byte, 0–100) + status text (≤31 bytes) |

### 3.2 Uplink (ESP32 → Pi)

| TYPE | Name | Payload |
| --- | --- | --- |
| `0x81` | `HELLO` | protocol version (1 byte) + capabilities (bitmask) |
| `0x82` | `JOYSTICK` | x (2), y (2), button (1) — see §5.1 |
| `0x83` | `TOUCH` | x (2), y (2), gesture (1) — see §5.2 |
| `0x84` | `MENU_SELECT` | item id (1 byte) |
| `0x85` | `FORCE` | force (2 bytes, signed) — see §5.3 |
| `0x86` | `ACK` | acked seq (1 byte) + status (1 byte) |

---

## 4. State Values (downlink `STATE_UPDATE`)

The Pi pushes named state values. The ESP32 caches them and re-renders the
active screen when one it cares about changes.

| ID | Name | Type | Example |
| --- | --- | --- | --- |
| `0x01` | `BATTERY_PCT` | uint8 | 82 |
| `0x02` | `SPEED_MPS` | int16 (×100) | 120 → 1.20 m/s |
| `0x03` | `SAFETY_STATE` | uint8 enum | 0=STOPPED,1=READY,2=MOVING,3=LIMITED,4=FAULT |
| `0x04` | `MODE` | uint8 enum | 0=MANUAL,1=FOLLOW,2=AUTONOMOUS,3=TELEOP |
| `0x05` | `GPS_LAT` | int32 (×1e7) | 481234500 |
| `0x06` | `GPS_LON` | int32 (×1e7) | 116789000 |
| `0x07` | `GPS_SPEED` | uint16 (×100) | — |
| `0x08` | `GPS_SATS` | uint8 | 9 |
| `0x09` | `IMU_ROLL` | int16 (×1000 rad) | — |
| `0x0A` | `IMU_PITCH` | int16 (×1000 rad) | — |
| `0x0B` | `OBSTACLE` | uint8 | 0=none,1=in_zone |
| `0x0C` | `OBSTACLE_NEAREST_M` | uint16 (×100) | — |
| `0x0D` | `GEOFENCE` | uint8 enum | 0=OK,1=NEAR,2=CROSSED,3=NO_FIX |
| `0x0E` | `SPEED_ZONE_LIMIT` | int16 (×100, −1=none) | — |
| `0x0F` | `SLOPE_DEG` | int16 (×100) | — |
| `0x10` | `NAV_STATUS` | uint8 enum | 0=IDLE,1=PLANNING,2=DRIVING,3=PAUSED,4=ARRIVED,5=ERROR |
| `0x11` | `HOLE_NUMBER` | uint8 | 5 |
| `0x12` | `HOLE_DISTANCE_M` | uint16 | 380 |
| `0x13` | `HOLE_REMAINING_M` | uint16 | 120 |
| `0x14` | `PUSH_FORCE_N` | int16 (×100) | — (echo of uplink, for display) |
| `0x15` | `ASSIST_LEVEL` | uint8 | 3 |
| `0x16` | `ASSIST_ENABLED` | uint8 | 0/1 |
| `0x17` | `HILL_ASSIST_ENABLED` | uint8 | 0/1 |
| `0x18` | `TIME_HHMM` | uint16 | 1234 → 12:34 |

| ID | Name | Type | Example |
| --- | --- | --- | --- |
| `0x40` | `RANGE_M` | uint16 | 2500 → 2500 m |
| `0x41` | `RETURN_M` | uint16 | 1200 → 1200 m |
| `0x42` | `RANGE_STATE` | uint8 enum | 0=OK,1=CAUTION,2=CRITICAL |

> **Extensible:** IDs 0x40–0x7F are reserved for future state values. Unknown
> IDs are ignored (the ESP32 logs and continues).

---

## 5. Input Events (uplink)

### 5.1 Joystick (`0x82`)

```
x: int16  (-32768..32767)   raw analog axis (0 = center)
y: int16  (-32768..32767)   raw analog axis (0 = center)
button: uint8               0 = released, 1 = pressed
```

- **The ESP32 reads the analog joystick directly** (ADC) — no Arduino in the
  path. The joystick is wired to the ESP32's analog inputs + a button pin.
- Sent at 50 Hz while the joystick moves; on button change.
- The Pi maps raw axes → linear/angular velocity (same as the current
  `arduino_joystick_node`).

### 5.2 Touch (`0x83`)

```
x: uint16   (0..319)   screen x (portrait)
y: uint16   (0..479)   screen y (portrait)
gesture: uint8         0 = tap, 1 = hold (>=3s), 2 = swipe
```

- Sent on tap/hold/swipe. Coordinates are already in the ESP32's screen space
  (the ESP32 owns the display, so it reports display-native coords).
- **Raw touch is a fallback.** The ESP32 normally resolves a tap to a menu
  item locally and sends `MENU_SELECT` (§5.3). `TOUCH` is used for raw
  coordinates where the ESP32 has no layout knowledge (e.g. a map view where
  the Pi needs the tapped position).

### 5.3 Menu select (`0x84`) — **decided: local tap mapping**

```
item: uint8   menu item id (screen-specific)
```

- **The ESP32 resolves taps to menu items locally** (it knows the screen
  layout) and sends `MENU_SELECT` directly, avoiding a round-trip. This is the
  **primary** path for menu screens.
- The Pi sends the screen layout / item ids via `SCREEN_NAV` + `CONFIG` so the
  ESP32 knows what each screen's items are.
- `TOUCH` (§5.2) is used only where the Pi needs raw coordinates (e.g. tapping
  a point on the hole map).

### 5.4 Load-cell force (`0x85`) — push-assist

```
force: int16  (×100 N, signed)   push force; + = push, − = pull/brake
```

- Sampled at **50–100 Hz** and sent continuously while the cart is in
  push-assist mode.
- **Critical priority:** this is the proportional-assist input. It must not be
  starved by display traffic. The ESP32 sends it on a dedicated high-priority
  slot (see §6).
- **The ESP32 sends raw (uncalibrated) force.** Calibration (zero offset, gain)
  is owned by the **Pi** — it applies the calibration to the raw samples and
  feeds the result to the push-assist controller (which requests motion through
  the Safety Controller — never bypassing it).
- The Pi may push the calibrated force back to the ESP32 as `STATE_UPDATE`
  `PUSH_FORCE_N` (§4, `0x14`) for display only.

---

## 6. Priority & Flow Control

The uplink carries both high-rate (joystick, force) and event (touch, menu)
traffic. To keep force/joystick from being starved:

1. **Force and joystick are sent on a fixed cadence** (50–100 Hz) regardless of
   other traffic.
2. **Touch/menu are event-driven** and interleave between the fixed-cadence
   frames.
3. **The ESP32 never blocks** on the downlink. If the Pi is slow, the ESP32
   drops display-state frames (lossy) but never drops input/force (reliable).

### 6.1 Downlink lossiness

- `STATE_UPDATE` / `DEBUG_SUMMARY` are **lossy** — a dropped frame is harmless
  (the next one supersedes it).
- `SCREEN_NAV` / `CONFIG` are **reliable** — the ESP32 ACKs them; the Pi
  retries on timeout.

### 6.2 Heartbeat & reconnect

- Both ends send a heartbeat at 1 Hz by **reusing `ACK`** (no separate
  heartbeat message type).
- If the Pi sees no uplink for > 2 s, it treats the handle as **disconnected**:
  it stops push-assist (no force data) and, if the cart was moving under
  push-assist, requests a safe stop.
- If the ESP32 sees no downlink for > 2 s, it shows a **"LINK LOST"** overlay
  and stops sending force (fail-safe: no assist without a live link).

---

## 7. Capabilities negotiation

On connect, the ESP32 sends `HELLO` with a **capabilities bitmask** so the Pi
knows what the handle supports:

| Bit | Capability |
| --- | --- |
| `0x01` | Joystick |
| `0x02` | Touch |
| `0x04` | Load cell (push force) |
| `0x08` | Display (LVGL) |

The Pi uses this to decide which features to enable (e.g. only enable
push-assist if `0x04` is set).

---

## 8. Example session

```text
# Power-on: the ESP32 boots in <1 s and shows the splash screen immediately.
ESP32 → Pi : HELLO  v1, caps=0x0F (joystick+touch+force+display)   (re-sent 1 Hz)
# ... Pi is still booting; it pushes progress to the splash screen ...
Pi    → ESP32: BOOT_STATUS  progress=10 "Starting ROS"
Pi    → ESP32: BOOT_STATUS  progress=30 "Loading gateway"
# ... Pi gateway is up and answers the HELLO ...
Pi    → ESP32: HELLO  v1
Pi    → ESP32: SCREEN_NAV  screen=COURSE      (leaves the splash screen)
ESP32 → Pi : MENU_SELECT  item=course:0
Pi    → ESP32: SCREEN_NAV  screen=TEE
ESP32 → Pi : MENU_SELECT  item=tee:1
Pi    → ESP32: SCREEN_NAV  screen=HOLE
Pi    → ESP32: STATE_UPDATE  BATTERY_PCT=82
Pi    → ESP32: STATE_UPDATE  SPEED_MPS=120
ESP32 → Pi : FORCE  +35   (push force 0.35 N)
ESP32 → Pi : FORCE  +42
ESP32 → Pi : JOYSTICK  x=0 y=512 btn=1
...
```

### 8.1 Boot / splash sequence

The ESP32 boots in under a second, so it shows a **splash screen** (logo +
animated spinner) while the Raspberry Pi takes 30–60 s to boot ROS and start
the gateway:

1. **ESP32 powers on** → shows `SCREEN_SPLASH` and sends `HELLO` (re-sent every
   1 s until the Pi answers).
2. **Pi boots** → the gateway starts and pushes `BOOT_STATUS` frames (progress
   0–100 + short text) so the splash shows live progress.
3. **Pi gateway is ready** → on the first `HELLO` it replies `HELLO` and sends
   `SCREEN_NAV screen=COURSE`, which switches the ESP32 off the splash screen.

The ESP32 keeps re-sending `HELLO` until it gets a `DL_HELLO`, so it works even
if the Pi boots after the ESP32's first `HELLO`.

---

## 9. Open items

- Exact screen-id and menu-item-id tables (depends on the final screen set —
  see §11).

## 9.1 Decided

- **Tap mapping:** the ESP32 maps taps to menu items locally and sends
  `MENU_SELECT` (§5.3). `TOUCH` is a fallback for raw coordinates (e.g. map
  taps).
- **Load-cell calibration:** stored on the **Pi**. The ESP32 sends raw force
  (§5.4); the Pi applies zero offset + gain and feeds the calibrated value to
  the push-assist controller.
- **Joystick:** moves to the **ESP32** (read directly via ADC). **No Arduino
  in the handle path** — the ESP32 is the sole handle interface (display +
  touch + joystick + load cell).
- **Heartbeat:** reuse `ACK` (no separate heartbeat message).
- **Baud:** 460800 (safe for USB CDC).
- **Link-loss:** a lost handle link stops push-assist **and** manual joystick
  driving (no input = no motion).
- **Data handling:** all data handling/control logic lives on the **Pi**; the
  ESP32 only samples/renders/forwards.
- **Pi HMI:** delete `hmi_node.py` / `hmi_draw.py` — the ESP32 replaces them.

---

## 11. HMI scope (decided: implement all screens)

The ESP32 handle unit implements the **full** HMI screen set from
`docs/hmi-spec.md`:

- Main Menu (MAP / MODE / ASSIST / CHANGE HOLE / SELECT COURSE / WIFI / DEBUG /
  SHUTDOWN)
- Course selection, Tee selection, Map/Hole view
- Mode selection (Manual / Follow Me / Autonomous)
- Assist (push-assist + hill-assist toggles)
- Change Hole
- WiFi (SSID / pass / QR)
- Debug menu + 6 debug views (System / GPS / LiDAR / Camera / IMU / Navigation)

**Debug data rate:** throttled to 1–2 Hz (see §2.2). Heavy views (LiDAR point
cloud, camera) render **summaries** on the handle; full visualizations are
optional and on-demand.

---

## 12. Hardware (decided)

| Item | Decision |
| --- | --- |
| **ESP32 variant** | **ESP32-S3-WROOM-1 (N8R8)** — 8 MB flash + 8 MB PSRAM (safest for a 320×480 LVGL UI) |
| **Joystick** | 2-axis analog + button, read via ADC on the ESP32 |
| **Load cell** | HX711 amplifier, connects to the ESP32 |
| **Backlight** | **Controllable** (PWM, GPIO 9) — dim/off when idle |
| **Power** | **Powered by the Pi** (see §12.1) |
| **Display connector** | 14-pin header on the display board (see `docs/handle-pcb.md` §4.1) |
| **Enclosure** | **Custom PCB** — plug in the display module + ESP32, connect load cell + joystick (see `docs/handle-pcb.md`) |

### 12.1 Power budget (powered by the Pi)

The Pi must supply the handle unit over the USB link. Estimate the draw:

| Component | Typical draw |
| --- | --- |
| ESP32-S3 | ~0.2–0.5 W (active) |
| ST7796 display + backlight | ~0.5–1.0 W |
| FT6336U touch | ~0.1 W |
| HX711 + load cell | ~0.05 W |
| **Total** | **~0.9–1.7 W** |

A Raspberry Pi 5 USB port supplies **up to ~7.5 W** (5 V × 1.5 A) per port, so
the handle unit (~1–2 W) is comfortably within budget. **Verify:** the Pi 5's
USB-C power supply must be ≥ 27 W to sustain full USB output; confirm the
specific Pi model's USB port rating.

> **Note:** if the handle unit ever needs to run independently (e.g. a
> standalone joystick/display without the Pi), a separate feed would be needed.
> For the current design (always tethered to the Pi), Pi-powered is correct.

---

## 13. Repository layout (decided)

The HMI implementation currently lives in `golfcart_teleop`. It gets its own
directory:

```text
src/
  golfcart_teleop/        # joystick/keyboard/web teleop (unchanged)
  golfcart_hmi/           # NEW: ESP32 handle-unit HMI
    firmware/             #   ESP32 firmware (LVGL screens, input, protocol)
    gateway/              #   Pi-side serial gateway node (ROS)
    docs/                 #   handle-protocol.md, screen specs
```

The Pi-side `hmi_node.py` / `hmi_draw.py` are **deleted** (E13); the ESP32
firmware + Pi gateway live in the new `golfcart_hmi` package.

## 10. Display (selected)

**Elecrow 3.5" IPS SPI LCD Touch Module, ST7796 driver, 320×480.**

| Aspect | Value |
| --- | --- |
| Panel | 3.5" IPS, 320×480 portrait |
| Display driver | ST7796 (SPI) |
| Touch controller | **FT6336U** (FocalTech, capacitive) |
| Touch interface | I2C (FT6336U) |
| Interface | SPI (display) + I2C (touch) |

**Why it fits:** the ST7796 is well-supported by **TFT_eSPI** and **LVGL** on
the ESP32; 320×480 matches the portrait screen layout; it is a generic SPI
module (not a Pi peripheral), so the ESP32 drives it directly.

**Touch controller (FT6336U):**
- FocalTech capacitive controller, communicated over **I2C**.
- Reports single/multi-touch points + gesture data (tap, swipe, etc.).
- The ESP32 reads it via I2C and maps points to the 320×480 screen space.
- **Library support:** confirm the ESP32 touch library you use (e.g. a
  dedicated FT6336 driver, or a generic I2C touch handler in LVGL) supports
  the FT6336U. This is the main integration point to verify.

**To verify before committing:**
- **FT6336U library support** on the ESP32 (I2C driver / LVGL touch backend).
- **Touch ↔ display orientation** — confirm the touch axes line up with the
  320×480 portrait (or can be rotated in firmware).
- **Backlight/power** — driven over SPI or a separate pin; whether the ESP32
  can power it (see §12.1 power budget).
- **14-pin header pinout** — the display board has a 14-pin header; confirm the
  exact pin mapping (SPI, I2C, backlight, power) for the custom PCB (§12).