# Person Re-ID

Re-acquire the follow-me operator after a target loss, so follow resumes
without a manual restart.

> **Status:** Implemented. `golfcart_follow` package, `person_reid_node`.

## Purpose

When follow-me loses the operator (`TARGET_LOST`), it previously stopped and
required a manual restart. Re-ID lets the trolley re-acquire the operator if
they reappear.

## Overview

```text
/follow/status (TARGET_LOST)  +  /person/target
        ↓
   person_reid_node
        │   ├─ enter SEARCHING on target loss
        │   ├─ watch for a person reappearing near the last-known position
        │   └─ re-publish the target on re-acquire
        ↓
   follow_controller resumes
```

## How it works

- **`person_reid_node`** watches `/follow/status`. When follow is active and
  the state becomes `TARGET_LOST`, it enters **SEARCHING** (remembering the
  last-known position).
- It watches `/person/target` for a person reappearing **within `max_gap_m`**
  of the last-known position and **within `search_timeout_s`**.
- On re-acquire, it **re-publishes the target** so the follow controller
  resumes automatically.
- Publishes `/reid/status` (`ReIdStatus`: IDLE/SEARCHING/REACQUIRED/TIMEOUT).

## Design notes

- **Continuity-based** (works with LiDAR) — re-acquires the person who
  reappears near where the operator was, not appearance recognition (which
  would need the camera).
- **Time-limited** — gives up after `search_timeout_s`.
- **Spatial gate** — avoids locking onto a different person far away.

## Configuration

See `config/golfcart.yaml`:

```yaml
person_reid_node:
  search_timeout_s: 30.0   # how long to search before giving up (s)
  max_gap_m: 3.0           # max distance from last-known position (m)
```