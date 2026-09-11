# Plan: Learning / Map Refinement

**TL;DR** — Close the learning loop: the trolley records environment observations
while playing a round, then an off-board tool merges those observations into the
existing DEM-derived course map to refine it. The critical requirement is
**alignment** — new and old data must share a common frame and blend smoothly so
we don't get artifacts at the borders where they collide.

## The alignment problem

The existing course map (from the map editor) contains, per hole:
- `costmapN.pgm` + `.yaml` — Nav2 costmap (uint8, 0=free, 254=lethal), grid in
  the course map frame at a fixed resolution (e.g. 5 m).
- `holeN_slope_gradx.pgm` / `_grady.pgm` / `_grad.yaml` — float32 terrain
  gradient (ground-fixed east/north).

The recorded observations are points in the same map frame. To merge without
artifacts we need:

1. **A common frame** — both datasets in the course map frame (equirectangular
   projection from the course origin, matching `geo.py` / `slope_node`).
2. **Same grid** — rasterize observations onto the *exact same* grid
   (origin + resolution) as the existing costmap, so cells align 1:1.
3. **Confidence-weighted blending** — blend old and new per cell using a
   baseline weight for the existing data and an accumulated weight for new
   observations. Where there's no new data, the old value is preserved.
4. **Spatial smoothing** — spread each observation over a small Gaussian
   neighborhood so a single point doesn't create a hard 1-cell artifact, and
   the transition at the observed/unobserved border is smooth.

## Architecture (rosbag as the single source of truth)

We do NOT add a separate on-board recorder. The existing `record_bag.sh` already
records `/gps/fix`, `/imu/data`, `/odometry/filtered`, `/obstacles/state` as a
rosbag. The flags are a *materialized view* of that bag, so we derive them
off-board instead of recording them twice.

```text
ON-BOARD (RPi 5)                          OFF-BOARD (dev PC)
─────────────────                         ─────────────────
record_bag.sh --all  ──>  rosbag2/<round>  ──>  extract_flags.py
  (/gps/fix /imu/data /odometry/filtered        (bag -> obs.jsonl:
   /obstacles/state /course/selected)            drivable/steep/obstacle)
                                                       │
                                                       ▼
                                              refine_map.py (merge with course zip)
                                                       -> refined costmap+gradient
                                                       -> refined course zip
```

## Steps

### Phase 1 — Merge math (pure, testable)
1. `tools/map_editor/map_editor/refine.py`:
   - `rasterize_observations(obs, origin_x, origin_y, resolution, shape)` ->
     `(new_cost, weight)` grids on the existing costmap grid.
   - `blend_costmaps(old, new, weight, baseline_weight)` -> refined uint8
     costmap (confidence-weighted average, smooth transition).
   - `gradient_from_imu(pitch, roll, yaw)` -> `(dzdx, dzdy)` (invert the
     slope_node projection).
   - `refine_course(zip_path, obs_path, out_path)` -> refined course zip.
2. Tests: `test_refine.py` — alignment (same grid), no-artifact blending
   (smooth transition), gradient inversion round-trip.

### Phase 2 — Bag -> observations extraction (off-board)
3. `scripts/extract_flags.py`: reads a recorded bag (rosbag2_py, like
   `extract_bag_polygon.py`), converts GPS->map-frame using the course origin
   (from `/course/selected` or a `--origin` flag), and writes `obs.jsonl`.
   Flags: `drivable` (trolley drove here), `steep` (IMU pitch/roll exceeded
   threshold), `obstacle` (obstacle in stopping zone).

### Phase 3 — Off-board merge tool
4. `scripts/refine_map.sh` + `refine_map.py` CLI: `refine_map.py <course.zip>
   <obs.jsonl> -o <refined.zip>`.

### Phase 4 — Verify
5. Unit tests for merge math; full workspace build + tests.

## Gotchas
- **Frame consistency:** GPS->map-frame must use the SAME origin + rotation as
  `geo.py`/`slope_node` (equirect R=6371000, x=east, y=north, then rotate).
- **Grid alignment:** rasterize onto the existing costmap's exact grid; never
  resample the existing grid to a new one (that's where border artifacts come
  from).
- **Blending:** use confidence-weighted average with a baseline weight for the
  existing data, not hard replacement.
- **Gradient inversion:** `dzdx = pitch*cos(yaw) - roll*sin(yaw)`,
  `dzdy = pitch*sin(yaw) + roll*cos(yaw)` (inverse of slope_node projection).