# Hazard Camera View

Show the live camera feed with course-segmentation status so the operator can
see hazards (water, bunkers, rough) around the trolley.

> **Status:** Implemented (camera node + web hazard view). The actual camera
> capture (libcamera on the Pi) and model inference (Coral NPU) are abstracted
> for the hardware phase.

## Purpose

The trolley's camera is a perception/diagnostic aid. This feature wires the
camera feed to a view the operator can open on their phone/web browser, and
shows whether live course segmentation is running and what it sees.

## Overview

```text
Pi Camera (RGB) → camera_node → /camera/image
        ↓
   web_teleop_server (subscribes /camera/image, JPEG-encodes)
        ↓  /camera.mjpeg (MJPEG stream)
        ↓
   web/hazard.html (live feed + segmentation status)
```

## How it works

- **`camera_node`** (golfcart_vision) publishes `/camera/image`
  (`sensor_msgs/Image`, RGB, 640×480, ~5 Hz). The capture source (libcamera /
  rpicam on the Pi) is abstracted via `feed_frame()`; for testing it can be
  fed synthetic frames, otherwise it publishes a placeholder so the pipeline
  stays live.
- **`capability_node`** now auto-detects the camera by subscribing to
  `/camera/image` (frame presence = heartbeat), so the Camera capability
  reflects a live feed rather than only a config override.
- **`web_teleop_server`** subscribes to `/camera/image`, converts each RGB
  frame to JPEG (Pillow), and serves it as a multipart MJPEG stream at
  `/camera.mjpeg`. It degrades gracefully — no camera → no frames → the page
  shows "No camera feed".
- **`web/hazard.html`** shows the live feed (`<img src="/camera.mjpeg">`) plus
  segmentation status (active / class count / confidence) from
  `/segmentation/status` via rosbridge.

## Why a web view (not the HMI screen)

The ESP32 handle unit's 320×480 SPI LCD has no bandwidth for a live video
feed. The HMI Camera debug screen keeps the Camera OK/MISSING + Segmentation
ON/OFF indicators; the live feed lives on the web app, which the operator
already uses for teleop / summon / trip history.

## Configuration

See `config/golfcart.yaml`:

```yaml
camera_node:
  publish_rate_hz: 5.0
  width: 640
  height: 480
```

## Files

- `src/golfcart_vision/src/camera_node.cpp` — publishes `/camera/image`
- `src/golfcart_system/src/capability_node.cpp` — camera auto-detect
- `src/golfcart_teleop/golfcart_teleop/web_teleop_server.py` — MJPEG endpoint
- `src/golfcart_teleop/web/hazard.html` — live camera + segmentation view