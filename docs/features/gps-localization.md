# GPS and Localization

GPS provides coarse global localization.

> **Status:** Implemented. `golfcart_localization` fuses GPS + IMU + wheel
> odometry via `robot_localization` EKF (`/odometry/filtered`), with a
> `localization_quality_node` monitoring covariance. See `navigation.md` and
> `docs/architecture.md` for the full stack.

It may be used for:

- mapping
- recording trolley routes
- future navigation
- hole/course localization

GPS must not be used as the sole mechanism for:

- collision avoidance
- precise obstacle positioning
- short-range positioning

A future localization system may combine:

```text
GPS
IMU
wheel odometry
LiDAR
```

using sensor fusion.

---

## GPS Accuracy

**Decision:** use a **multi-GNSS + dual-band receiver with SBAS** (~1–2 m
accuracy). **RTK is not used.**

**Rationale:** the operator is a **player on different courses** (not the
course owner), so the "own base station" / LoRa approaches don't apply — you
can't install equipment on courses you don't control. Public NTRIP (RTK over
cellular) is coverage-dependent and adds complexity. For following the player
and staying on course, ~1–2 m from SBAS is plenty.

### Recommendations

| Priority | Action | Accuracy gain |
| --- | --- | --- |
| 1 | **Antenna placement** — high, flat, clear sky, away from metal/motors | Big (free) |
| 2 | **Multi-GNSS + dual-band receiver** (GPS + GLONASS + Galileo + BeiDou) | ~5 m → ~1–2 m |
| 3 | **Active antenna with ground plane** (rejects multipath) | Reduces multipath |
| 4 | **SBAS enabled** (WAAS/EGNOS) | ~5 m → ~1–2 m |

### Not used

- **RTK** — needs a base station (own or public NTRIP), which doesn't fit the
  traveling-player use case. Revisit only if cm-level precision is ever needed
  (e.g. precise course mapping) and a public NTRIP caster covers the course.