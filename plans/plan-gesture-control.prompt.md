# Plan: Gesture Control

**TL;DR** — Control the trolley with hand/arm gestures instead of voice or the
phone: windmill+hold to summon, double palm up to stop, choo-choo to follow,
repeated pat-down to slow. Runs on the Pi Camera + Coral USB (see
`docs/features/camera-vision.md`).

**User decisions (confirmed)**
- Scope: gesture control only (SUMMON / STOP / FOLLOW / SLOW).
- Model: **MediaPipe Pose** (full-body keypoints) on the Coral NPU — the
  gestures are defined by arm geometry (shoulder–elbow–wrist), not hand shape.
- All gestures are **sustained/repetitive** motions (no one-off pose triggers).
- SUMMON requires **windmill + hold** (~1 s) to confirm.
- STOP = **double palm up** (both wrists up); SLOW = **repeated pat-down** (2–3×).
- Arm pose alone is sufficient — **no hand classifier**.
- Gestures are **advisory** — never bypass the Safety Controller.

---

## Current state (verified)

- **Pi Camera Module 3** — RGB source on CSI (planned; see `camera-vision.md`).
- **Coral USB Accelerator** — NPU on the freed 4th USB port (planned).
- **`mode_node`** (`golfcart_control`) — publishes `/mode/state` (`ModeState`),
  modes MANUAL/FOLLOW/AUTONOMOUS/TELEOP. Mode can be set via `/mode/set` service
  or by publishing `ModeState`.
- **`summon_node`** — handles summon (phone page + HMI render `SummonStatus`).
- **Safety Controller** (`safety_controller_node`) — arbitrates all motion
  requests; the natural consumer of a STOP request.
- **HMI** — Assist screen has toggles (e.g. Steering Assist) that flow through
  `handle_gateway.py` → config topics. Status screen uses state IDs in the
  `0x40–0x7F` reserved range for new values.

### Key gaps for gesture control (what this plan fills)
1. **No camera node** — nothing publishes `/camera/image` from the Pi Camera.
2. **No gesture recognition** — nothing runs MediaPipe Pose or maps poses to
   gestures.
3. **No gesture command message** — nothing carries a recognized gesture to the
   command path.
4. **No gesture controller** — nothing maps gestures to summon/mode/safety.

---

## Architecture

```text
Pi Camera (RGB) → camera_node → /camera/image
        ↓
   gesture_recognition_node (Coral NPU, MediaPipe Pose)
        ↓  /gesture/command (GestureCommand)
        ↓
   gesture_controller_node
        │   ├─ debounce + confirm (windmill+hold for SUMMON)
        │   ├─ map gesture → command
        │   └─ publish to existing command topics
        ↓
   summon_node / mode_node / safety_controller
```

### New message: `GestureCommand.msg`
Published on `/gesture/command`.

```text
# A recognized gesture from gesture_recognition_node.
uint8 gesture    # 0=NONE, 1=SUMMON, 2=STOP, 3=FOLLOW, 4=SLOW
float32 confidence
builtin_interfaces/Time stamp
```

### New package: `golfcart_vision`
- `camera_node` — publishes `/camera/image` (RGB) from the Pi Camera via
  `rpicam`/`libcamera`.
- `gesture_recognition_node` — runs MediaPipe Pose on the Coral NPU, tracks the
  shoulder–elbow–wrist chain, and classifies the motion into a gesture.

### Pure math: `gesture_math.hpp`
- `arm_angle(shoulder, elbow, wrist)` — the elbow angle from the keypoints.
- `is_windmill(shoulder, elbow, wrist, frames)` — sustained circular motion of
  the wrist around the shoulder's vertical axis.
- `is_choo_choo(shoulder, elbow, wrist, frames)` — repeated vertical oscillation
  of the wrist while the upper arm stays horizontal.
- `is_double_palm_up(shoulder_l, wrist_l, shoulder_r, wrist_r)` — both wrists
  above their shoulders.
- `is_pat_down(wrist, frames)` — repeated lowering of the hand (2–3×).
- A **debounce** (gesture must persist `debounce_frames`) before accepting.

### New node: `gesture_controller_node`
- Subscribes `/gesture/command` and `/mode/state`.
- Applies debounce + confirmation (SUMMON needs windmill + hold).
- Maps gestures to existing command paths:
  - SUMMON → trigger `summon_node`.
  - STOP → publish a stop request to the Safety Controller.
  - FOLLOW → set `mode_node` to FOLLOW.
  - SLOW → publish a temporary speed limit.
- Ignores gestures while the operator is actively driving (MANUAL + joystick).

---

## Configuration

See `config/golfcart.yaml`:

```yaml
gesture_recognition_node:
  model: "mediapipe_pose"          # full-body keypoints on the Coral NPU
  confidence_threshold: 0.7         # min confidence to accept a gesture
  debounce_frames: 15               # frames a gesture must persist (~0.5 s)
  publish_rate_hz: 10
  summon_hold_s: 1.0                # hold time to confirm SUMMON (windmill + hold)

gesture_controller_node:
  enabled: true                     # master on/off (HMI toggle)
  slow_speed_mps: 0.5               # speed cap for the SLOW gesture
  ignore_in_manual: true            # ignore gestures while joystick is active
```

---

## HMI integration

- **Gesture Control toggle** on the Assist screen (mirrors Steering Assist).
- **Last recognized gesture + confidence** on the status screen (new state ID
  `ST_GESTURE` in the `0x40–0x7F` reserved range).
- Web dashboard shows the same gesture status.

---

## Testing

- **Unit tests** for `gesture_math.hpp` (pure math): arm angle, windmill,
  choo-choo, double-palm, pat-down, debounce.
- **Headless e2e check** — feed synthetic keypoint sequences and verify the
  correct gesture is recognized and the right command is published.
- **Mock camera** — a synthetic RGB source so the pipeline runs without the Pi
  Camera / Coral hardware.

---

## Out of scope (deferred)

- Live course segmentation (separate roadmap item).
- Person re-ID (candidate).
- Hazard camera view (separate roadmap item).

---

## Open items

- Exact MediaPipe Pose model variant + Coral NPU conversion (TFLite).
- Whether the camera node uses `rpicam`/`libcamera` directly or a ROS camera
  driver package.