# Geofencing / Stay-on-Course

Define the course boundary and warn or stop the trolley if it leaves.

> **Status:** Planned. The `CourseMap` message already carries `forbidden_zones`
> (used by navigation to stop in front of hazards); a full geofence boundary
> check is a future extension.

## Purpose

Prevent the trolley from driving into hazards, water, or off the course.

## Approach

- Define the course boundary as a polygon (from the course map or GPS).
- Monitor the trolley's GPS position against the boundary.
- If the trolley approaches or crosses the boundary:
  - warn the operator, and/or
  - request a stop via the Safety Controller.

## Architecture

```text
GPS
 ↓
Position
 ↓
Geofence Check
 ↓
Warn / MotionRequest (stop)
 ↓
Safety Controller
```

## Safety

- Geofencing is a safety-related function; a boundary violation should result
  in a safety-layer decision (stop), not a direct motor command.
- Requires reliable GPS; stale GPS must be treated as invalid.

## Effort

Medium. Requires GPS/localization plus a geofence-checking node.