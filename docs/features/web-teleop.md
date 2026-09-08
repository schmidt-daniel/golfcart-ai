# Web-Based Remote Teleop

Allow driving the trolley from a phone or tablet via a web interface.

## Purpose

Provide a convenient remote-control option for testing and demonstration.

## Approach

Use `rosbridge_server` to expose ROS 2 topics/services over WebSocket, and a
small web page that:

- publishes `MotionRequest` on `/motion/request`
- calls `safety/enable` and `safety/stop`
- displays live status (speed, battery, safety state)

The Raspberry Pi provides a local Wi-Fi hotspot for the phone. This network is
intentionally local-only and does not provide internet access; the phone and
trolley communicate directly over the Pi's network.

## Architecture

```text
Web Page (phone/tablet)
        ↓ WebSocket
rosbridge_server
        ↓
ROS 2 topics/services
```

## Safety

- The web interface is a **request source only**; it must never bypass the
  Safety Controller.
- Remote commands should be treated with the same priority as other manual
  sources.
- Consider requiring an explicit enable before remote motion is allowed.

## Effort

Low-Medium. Uses existing nodes; adds `rosbridge_server` and a static web page.