# Remote E-Stop + Live Telemetry Dashboard

A phone/web page with a large emergency-stop button and a live view of the
trolley's state (position, battery, speed, mode, faults).

> **Status:** Implemented. See `plans/plan-dead-reckoning.prompt.md` for the
> feature-planning pattern; the dashboard itself is `web/dashboard.html`.

## Purpose

Give the operator a phone/web page with a large emergency-stop button and a
live view of the trolley's state, so they can stop the trolley remotely and
monitor it at a glance.

## Overview

```text
Phone browser (dashboard.html)
        ↓  rosbridge (roslib.js)
   /safety/stop  (Trigger)   →  Safety Controller → safe stop (priority-3)
   /safety/enable (Trigger)  →  Safety Controller → resume
   /safety/state, /motor/state, /battery/state, /gps/fix,
   /navigation/status, /geofence/status, /speed_zone/status,
   /slope/status, /obstacles/state
        ↓
   Live telemetry panel (position, battery %, speed, mode, fault badges)
```

## E-STOP button

- **Hold-to-confirm:** the operator must press and hold the button for 1.5 s to
  trigger a stop, preventing accidental stops.
- Calls `/safety/stop` (a `std_srvs/Trigger` service) — a hard stop through the
  Safety Controller's arbitration, never bypassing it.
- Visually distinct: large round red button; turns green ("STOPPED") after a
  stop.
- **RE-ENABLE** button calls `/safety/enable` to resume.

## Telemetry panel

Subscribes to the existing status topics and renders:

| Field | Topic | Message |
| --- | --- | --- |
| Safety state | `/safety/state` | `std_msgs/String` |
| Speed | `/motor/state` | `golfcart_msgs/MotorState` |
| Battery | `/battery/state` | `golfcart_msgs/BatteryState` |
| Position | `/gps/fix` | `golfcart_msgs/GpsFix` |
| Navigation | `/navigation/status` | `golfcart_msgs/NavigationStatus` |
| Geofence | `/geofence/status` | `golfcart_msgs/GeofenceStatus` |
| Speed limit | `/speed_zone/status` | `golfcart_msgs/SpeedZoneStatus` |
| Slope | `/slope/status` | `golfcart_msgs/SlopeStatus` |

## Fault badges

- **Battery LOW** — charge < 20%.
- **Geofence CROSSED / OUT_OF_FIX** — fault; **NEAR** — warning.
- **OBSTACLE** — obstacle in the stopping zone.
- **Steep slope** — |slope| > 10°.

## Safety notes

- The E-STOP is a **request source** — it goes through the Safety Controller's
  arbitration, never bypassing it. It must not command motors directly.
- Network loss must not leave the trolley in an ambiguous state; the Safety
  Controller's existing request-timeout already stops motion if no request
  arrives, so a lost connection is fail-safe.

## Deployment

- `dashboard.html` is added to `setup.py` `data_files` so it's installed and
  served by the web teleop server.
- Linked from `index.html` ("E-Stop & Telemetry →").
- Served alongside `index.html` and `summon.html` by `web_teleop_server`.