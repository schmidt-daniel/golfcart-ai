# Plan: Autonomous Navigation to Target (Golf Course)

**TL;DR** — Build a navigation system that drives the trolley to a target location on a golf course, avoiding hazards/obstacles, forbidden areas (greens/tees), narrow-pass constraints, and steep (tip-over) zones. Uses **Nav2** for planning, a course map built from **OSM + own sensor mapping + recorded visits**, **map-based + live IMU** slope safety, and **recorded-route + map refinement** learning.

**Steps**

**Phase 0 — Map representation**
1. Define the course map model: drivable zones, forbidden zones (greens/tees), hazards, narrow passes, steep zones, speed-limit zones.
2. Sources: OSM (base geometry) + own sensor mapping + refinement from recorded visits. **Maps are per-course.**

**Phase 1 — Localization** *(prereq for all navigation)*
3. Fuse GPS + IMU + wheel odometry → continuous pose in the `map` frame.
4. **GPS for coarse global position + LiDAR SLAM for local precision** (so narrow passes and forbidden-zone boundaries are accurate).

**Phase 2 — Costmap / planner (Nav2)** *(depends on 1)*
5. Nav2 costmap from the course map + live LiDAR obstacles.
6. Planner (NavFn/Smac) → path avoiding forbidden/hazard/steep zones.

**Phase 3 — Controller (Nav2)** *(depends on 2)*
7. Follow the path; respect speed limits by zone; slow in narrow passes.

**Phase 4 — Slope safety** *(depends on 1)*
8. Map-based steep zones (from IMU during visits) + live IMU roll/pitch check → stop/avoid.

**Phase 5 — Negative-obstacle detection** *(depends on 1)*
9. Add the **depth camera (GIXVISION)** to detect ditches/lakes/streams (negative obstacles the 2D LiDAR can't see), feeding the costmap and safety.

**Phase 6 — Learning** *(depends on 2, 4)*
10. Record successful routes; refine the map (drivable/steep/obstacle) from each visit; prefer known-good routes. **Per-course.**
    - **On-board:** record ROS 2 bags + cheap immediate flags ("drove here = drivable", "steep here" from IMU).
    - **Off-board:** process bags (dev PC / cloud) to refine the course map and optimize routes; ship the updated map back to the trolley.
    - Reason: heavy learning is isolated from the real-time safety loop on the RPi 5 (see `docs/features/learning.md`).

**Phase 7 — Target interface + operator interaction** *(depends on 2)*
11. Set a target (web map / summon); navigate to it.
12. **On an unexpected obstacle mid-route: stop and ask the operator** how to proceed, **via the web app** (e.g. "retry / replan / stop").

**Relevant files**
- `docs/features/navigation.md`, `route-replay.md`, `summon.md`, `geofencing.md`, `speed-zones.md`, `obstacle-detection.md` — reference the existing designs.
- New packages under `src/` (localization, mapping, navigation, depth-camera).

**Verification**
1. `colcon build` + tests.
2. Simulated course: navigate to a target avoiding forbidden/hazard/steep zones.
3. Replay a recorded bag; verify the path and stops.
4. Manual course test.

**Decisions**
- **Nav2** for planning.
- **OSM + own mapping** for the course map.
- **Map-based + live IMU** for slope/tip-over safety.
- **Recorded-route + map refinement** for learning.
- **Localization:** GPS for coarse + LiDAR SLAM for precision.
- **Depth camera (GIXVISION)** added for negative obstacles (ditches/lakes).
- **Learning is per-course** (each course mapped separately).
- **Learning is on-board + off-board:** on-board records bags + cheap immediate flags; off-board processes bags and ships the refined map back (see `docs/features/learning.md`).
- **On unexpected obstacle mid-route: stop and ask the operator** how to proceed, **via the web app**.