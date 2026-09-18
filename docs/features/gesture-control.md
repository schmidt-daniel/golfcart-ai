# Gesture Control

Control the trolley with hand gestures instead of voice or the phone — wave to
summon, hand signals to stop/follow. More appropriate than shouting across the
course.

> **Status:** Implemented. CPU skin-based keypoint source with an optional
> Coral pose backend. Requires the Pi Camera Module 3 (see
> `docs/features/camera-vision.md`).

## Purpose

The operator is often several meters from the trolley (walking to a ball,
standing on the tee). Voice control was rejected as impractical on a golf
course. Gestures give a natural, silent, hands-free way to command the trolley
at a distance — the same intent as the rejected voice control, but socially
acceptable.

## Overview

```text
Pi Camera (RGB) → coral_pose_node (Coral) → /pose/keypoints
        ↓  (or CPU skin estimator fallback)
   gesture_recognition_node
        ↓  /gesture/command (GestureCommand)
        ↓
   gesture_controller_node
        │   ├─ map gesture → command (SUMMON / STOP / FOLLOW / SLOW)
        │   ├─ debounce + confirm (avoid false triggers)
        │   └─ publish to the existing command topics
        ↓
   mode_node / summon_node / follow_controller / safety
```

## Gesture set (v1)

| Gesture | Command | Maps to |
| --- | --- | --- |
| **Windmill + hold** (upper arm ~90° to the side, lower arm up, circling around the z-axis, then hold) | SUMMON | `summon_node` (like the phone summon) |
| **Double palm, held up** (both hands up) | STOP | Safety Controller (stop) |
| **Choo-choo** (upper arm ~90° to the side, lower arm pumping up/down along the z-axis) | FOLLOW | `mode_node` → FOLLOW mode |
| **Repeated pat-down** (hand lowered 2–3×) | SLOW | Speed limit (e.g. near greens) |

Each gesture is **debounced** (must persist ~0.5–1 s) and **confirmed** before
acting, to avoid false triggers from normal arm movement. All gestures are
**sustained or repetitive** motions — no single one-off pose triggers an
action, so natural walking/carrying movements don't cause false positives.

### SUMMON gesture (windmill)

The summon gesture is a **circular arm rotation**:

- **Upper arm:** angled ~90° out to the side (shoulder abducted, roughly
  horizontal).
- **Lower arm:** pointed up (elbow bent ~90°), and the whole arm **circles
  around the z-axis** (a vertical "windmill" / stirring motion).

This is detected as a **rotational motion of the hand/arm around the vertical
axis** while the upper arm stays roughly horizontal — distinct from a simple
wave (which is a side-to-side sweep of the hand only). The recognition model
tracks the shoulder–elbow–wrist chain and looks for sustained circular motion
of the wrist around the shoulder's vertical axis.

**Confirmation:** SUMMON requires a **windmill + hold** — the operator does the
windmill, then **holds the arm up** for ~1 s. This confirms the intent and
avoids accidental summons (which would send the cart driving toward the
operator).

### FOLLOW gesture (choo-choo)

The follow gesture is a **vertical arm pump** — the classic "choo-choo" train
signal:

- **Upper arm:** angled ~90° out to the side (shoulder abducted, roughly
  horizontal).
- **Lower arm:** moving **up and down along the z-axis** (elbow flexing/
  extending), like pulling a train whistle cord.

This is detected as **repeated vertical oscillation of the hand/wrist** while
the upper arm stays roughly horizontal — distinct from the windmill (which
circles around the z-axis) and from a wave (which sweeps side-to-side). The
recognition model tracks the shoulder–elbow–wrist chain and looks for sustained
up/down pumping of the wrist.

## How it works

- **`gesture_recognition_node`** (`golfcart_vision`, new package) subscribes to
  `/camera/image` (RGB from the Pi Camera). Runs a lightweight pose/hand model
  on the **Coral NPU** (e.g. MediaPipe Hands / a small YOLO-pose variant).
- Publishes a recognized gesture on `/gesture/command` (`GestureCommand.msg`):
  `gesture` (enum) + `confidence` + `timestamp`.
- **`gesture_controller_node`** consumes it, applies **debounce + confirmation**
  (N consecutive frames above a confidence threshold), then maps the gesture to
  the existing command path:
  - SUMMON → call the summon service / publish to `summon_node`.
  - STOP → publish a stop request to the Safety Controller.
  - FOLLOW → set `mode_node` to FOLLOW.
  - SLOW → publish a temporary speed limit.
- **Safety:** gestures are **advisory** — they never bypass the Safety
  Controller. A STOP gesture is a *request*; the Safety Controller still
  arbitrates. Gestures are ignored while the operator is actively driving
  (MANUAL mode with joystick input).

## HMI integration

- The HMI shows the **last recognized gesture** + confidence on the status
  screen (new state IDs in the `0x40–0x7F` reserved range, e.g. `ST_GESTURE`).
- A **Gesture Control toggle** on the Assist screen enables/disables it
  (mirrors the Steering Assist toggle pattern).
- The web dashboard shows the same gesture status.

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

## Messages

New message `GestureCommand.msg` in `golfcart_msgs`:

```text
# A recognized gesture from gesture_recognition_node.
uint8 gesture    # 0=NONE, 1=SUMMON, 2=STOP, 3=FOLLOW, 4=SLOW
float32 confidence
builtin_interfaces/Time stamp
```

## Safety & edge cases

- **False positives:** debounce + confidence threshold; gestures require a
  clear, sustained pose.
- **Operator in control:** gestures ignored in MANUAL mode with active joystick
  input (the operator is driving, not gesturing).
- **No gesture = no action:** a `NONE` or low-confidence result never triggers
  anything.
- **STOP is advisory:** the Safety Controller still arbitrates; a gesture STOP
  is a request, not a bypass.

## Open questions

- ~~Exact gesture vocabulary~~ — **resolved:** all gestures are sustained/
  repetitive motions (windmill+hold, double palm, choo-choo, repeated pat-down)
  to avoid conflicts with natural walking/carrying.
- ~~Whether SUMMON should require confirmation~~ — **resolved:** SUMMON requires
  a **windmill + hold** (~1 s) to confirm intent.
- ~~Model choice~~ — **resolved:** **MoveNet single-pose** (full-body
  keypoints, COCO 17-keypoint layout), because the windmill/choo-choo are
  defined by arm geometry (shoulder–elbow–wrist), not hand shape. Runs on the
  Coral NPU at ~10–30 FPS on the Pi 5. (MediaPipe Pose is an equivalent
  alternative — same keypoint layout.)
- **New:** whether STOP (double palm) and SLOW (repeated pat-down) need a
  distinct hand classifier, or whether the arm pose alone is sufficient.
  — **resolved:** **arm pose alone is sufficient.** STOP and SLOW are defined by
  arm pose/motion (both wrists up; repeated lowering), not hand shape. Adding a
  hand classifier would mean a second model (more compute/latency on the Pi 5)
  for marginal benefit. False-positive protection comes from the debounce +
  sustained/repetitive requirement, not hand shape. Keep **MediaPipe Pose only**.