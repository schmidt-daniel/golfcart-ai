# Speed Limiting by Zone

Automatically limit the trolley's speed in specific areas.

## Purpose

Slow the trolley near greens, tees, cart paths, or other sensitive areas.

## Approach

- Define speed-limit zones (polygons) on the course map.
- Monitor the trolley's position.
- When inside a zone, reduce the maximum allowed speed.

## Architecture

```text
GPS
 ↓
Position
 ↓
Zone Check
 ↓
Speed Limit
 ↓
Safety Controller
```

The Safety Controller applies the zone speed limit as a motion limit.

## Safety

- Speed limits are enforced by the Safety Controller, not by the behavior
  layer.
- Requires reliable GPS; stale GPS must be treated as invalid.

## Effort

Medium. Requires GPS/localization plus a zone-checking node that feeds speed
limits to the Safety Controller.