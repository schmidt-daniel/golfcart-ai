"""Merge recorded observations into an existing course map (learning/refinement).

The map editor exports a course zip containing, per hole, a DEM-derived slope
costmap (uint8, 0=free, 254=lethal) and a ground-fixed terrain gradient
(float32 dz/dx, dz/dy). While playing a round, the trolley records discrete
observations ("drove here = drivable", "steep here" from the IMU, "obstacle
here"). This module merges those observations into the existing map to refine
it.

The critical requirement is **alignment**: new and old data must share a common
frame and blend smoothly so we don't get artifacts at the borders where they
collide. We achieve this by:

1. **Common frame** — observations are in the course map frame (equirectangular
   projection from the course origin, matching `geo.py` / `slope_node`).
2. **Same grid** — observations are rasterized onto the *exact same* grid
   (origin + resolution) as the existing costmap, so cells align 1:1.
3. **Confidence-weighted blending** — old and new are blended per cell using a
   baseline weight for the existing data and an accumulated weight for new
   observations. Where there is no new data, the old value is preserved.
4. **Spatial smoothing** — each observation is spread over a small Gaussian
   neighborhood so a single point does not create a hard 1-cell artifact, and
   the transition at the observed/unobserved border is smooth.
"""

from __future__ import annotations

import json
import math
import shutil
import tempfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np

from map_editor.dem import Costmap, write_costmap, write_gradient

#: Baseline confidence weight for the existing (DEM) data. New observations
#: must accumulate this much weight to fully override the existing value.
BASELINE_WEIGHT = 1.0

#: Gaussian kernel radius (in cells) over which an observation spreads.
#: Larger = smoother borders, less local precision.
SMOOTH_RADIUS_CELLS = 2.0

#: Cost value for a "drivable" observation (trolley drove here -> free).
DRIVABLE_COST = 0
#: Cost value for an "obstacle" observation (blocked -> lethal).
OBSTACLE_COST = 254
#: Cost value for a "steep" observation (IMU measured steep -> high cost).
STEEP_COST = 200


# ---------------------------------------------------------------------------
# Observation model
# ---------------------------------------------------------------------------


@dataclass
class Observation:
    """A single recorded observation in the course map frame.

    ``kind`` is one of ``drivable``, ``steep``, ``obstacle``. ``x``/``y`` are
    map-frame meters. ``conf`` is the confidence (0..1) of this observation.
    For ``steep`` observations, ``pitch``/``roll``/``yaw`` (radians) are the
    measured trolley-relative attitude and heading, used to recover the
    ground-fixed gradient.
    """

    kind: str
    x: float
    y: float
    conf: float = 1.0
    yaw: float = 0.0
    pitch: float = 0.0
    roll: float = 0.0

    def to_json(self) -> Dict:
        d: Dict = {"kind": self.kind, "x": self.x, "y": self.y, "conf": self.conf}
        if self.kind == "steep":
            d.update({"yaw": self.yaw, "pitch": self.pitch, "roll": self.roll})
        return d

    @classmethod
    def from_json(cls, d: Dict) -> "Observation":
        return cls(
            kind=d["kind"],
            x=float(d["x"]),
            y=float(d["y"]),
            conf=float(d.get("conf", 1.0)),
            yaw=float(d.get("yaw", 0.0)),
            pitch=float(d.get("pitch", 0.0)),
            roll=float(d.get("roll", 0.0)),
        )


def load_observations(path: Path) -> List[Observation]:
    """Load observations from a JSONL file (one JSON object per line)."""
    obs: List[Observation] = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            obs.append(Observation.from_json(json.loads(line)))
    return obs


def save_observations(obs: List[Observation], path: Path) -> None:
    """Write observations to a JSONL file."""
    with open(path, "w") as f:
        for o in obs:
            f.write(json.dumps(o.to_json()) + "\n")


# ---------------------------------------------------------------------------
# Rasterization + blending (the alignment core)
# ---------------------------------------------------------------------------


def _gaussian_kernel(radius_cells: float) -> np.ndarray:
    """A 2D Gaussian kernel of the given radius (in cells), normalized to sum 1."""
    r = int(math.ceil(radius_cells * 2.5))
    if r < 1:
        r = 1
    ax = np.arange(-r, r + 1, dtype=np.float64)
    xx, yy = np.meshgrid(ax, ax)
    k = np.exp(-(xx * xx + yy * yy) / (2.0 * radius_cells * radius_cells))
    k /= k.sum()
    return k


def rasterize_observations(
    obs: List[Observation],
    origin_x: float,
    origin_y: float,
    resolution: float,
    shape: Tuple[int, int],
    smooth_radius_cells: float = SMOOTH_RADIUS_CELLS,
) -> Tuple[np.ndarray, np.ndarray]:
    """Rasterize observations onto the existing costmap grid.

    Returns ``(new_cost, weight)`` grids, both of shape ``shape`` (rows, cols),
    aligned cell-for-cell with the existing costmap (same origin + resolution).
    ``new_cost`` holds the target cost value each observation contributes;
    ``weight`` holds the accumulated confidence weight per cell (spread by a
    Gaussian kernel so borders are smooth).

    Args:
        obs: observations in map-frame meters.
        origin_x, origin_y: map-frame origin of the grid (bottom-left).
        resolution: meters per cell.
        shape: (rows, cols) of the existing costmap grid.
        smooth_radius_cells: Gaussian smoothing radius in cells.

    Returns:
        (new_cost, weight): float64 arrays of shape ``shape``.
    """
    rows, cols = shape
    new_cost = np.zeros((rows, cols), dtype=np.float64)
    weight = np.zeros((rows, cols), dtype=np.float64)
    kernel = _gaussian_kernel(smooth_radius_cells)
    kr = kernel.shape[0] // 2

    for o in obs:
        # Map-frame position -> grid cell (row 0 = north, col 0 = west).
        col = int(math.floor((o.x - origin_x) / resolution))
        row = int(math.floor((o.y - origin_y) / resolution))
        if row < 0 or row >= rows or col < 0 or col >= cols:
            continue  # outside the grid; ignore

        # Target cost for this observation kind.
        if o.kind == "drivable":
            target = DRIVABLE_COST
        elif o.kind == "obstacle":
            target = OBSTACLE_COST
        elif o.kind == "steep":
            target = STEEP_COST
        else:
            continue

        # Spread over the Gaussian neighborhood (clamped to grid bounds).
        r0, r1 = max(0, row - kr), min(rows, row + kr + 1)
        c0, c1 = max(0, col - kr), min(cols, col + kr + 1)
        k_r0, k_r1 = r0 - (row - kr), r1 - (row - kr)
        k_c0, k_c1 = c0 - (col - kr), c1 - (col - kr)
        patch = kernel[k_r0:k_r1, k_c0:k_c1]
        new_cost[r0:r1, c0:c1] += target * patch * o.conf
        weight[r0:r1, c0:c1] += patch * o.conf

    return new_cost, weight


def blend_costmaps(
    old: np.ndarray,
    new_cost: np.ndarray,
    weight: np.ndarray,
    baseline_weight: float = BASELINE_WEIGHT,
) -> np.ndarray:
    """Blend the existing costmap with new observations (confidence-weighted).

    ``old`` is the existing uint8 costmap. ``new_cost``/``weight`` come from
    :func:`rasterize_observations`. The result is a confidence-weighted average:

        refined = (old * baseline + new_cost) / (baseline + weight)

    Where ``weight`` is 0 (no new data), the old value is preserved exactly.
    Where ``weight`` is large, the result approaches the new observation value.
    This gives a smooth transition at the observed/unobserved border.

    Returns a uint8 array of the same shape as ``old``.
    """
    old = np.asarray(old, dtype=np.float64)
    denom = baseline_weight + weight
    refined = (old * baseline_weight + new_cost) / denom
    return np.clip(np.round(refined), 0, 254).astype(np.uint8)


def gradient_from_imu(
    pitch_rad: float,
    roll_rad: float,
    yaw_rad: float,
) -> Tuple[float, float]:
    """Recover the ground-fixed gradient from trolley-relative IMU attitude.

    Inverts the slope_node projection:
        pitch = dzdx*cos(yaw) + dzdy*sin(yaw)
        roll  = -dzdx*sin(yaw) + dzdy*cos(yaw)
    Solving for (dzdx, dzdy):
        dzdx = pitch*cos(yaw) - roll*sin(yaw)
        dzdy = pitch*sin(yaw) + roll*cos(yaw)

    Args:
        pitch_rad, roll_rad: trolley-relative attitude (radians).
        yaw_rad: trolley heading (radians, 0 = +x/east, CCW positive).

    Returns:
        (dzdx, dzdy): ground-fixed gradient components (east, north).
    """
    cy = math.cos(yaw_rad)
    sy = math.sin(yaw_rad)
    dzdx = pitch_rad * cy - roll_rad * sy
    dzdy = pitch_rad * sy + roll_rad * cy
    return dzdx, dzdy


# ---------------------------------------------------------------------------
# Course zip refinement
# ---------------------------------------------------------------------------


def _read_costmap_grid(pgm_path: Path) -> Tuple[np.ndarray, float, float, float]:
    """Read a Nav2 costmap PGM + YAML, returning (data, resolution, ox, oy)."""
    import yaml as _yaml

    yaml_path = pgm_path.with_suffix(".yaml")
    meta = _yaml.safe_load(open(yaml_path)) if yaml_path.exists() else {}
    resolution = float(meta.get("resolution", 1.0))
    origin = meta.get("origin", [0.0, 0.0, 0.0])
    ox, oy = float(origin[0]), float(origin[1])

    with open(pgm_path, "rb") as f:
        assert f.readline().strip() == b"P5"
        dims = f.readline().split()
        assert f.readline().strip() == b"255"
        data = np.frombuffer(f.read(), dtype=np.uint8)
    cols, rows = int(dims[0]), int(dims[1])
    return data.reshape(rows, cols), resolution, ox, oy


def _read_gradient_pgm(pgm_path: Path) -> np.ndarray:
    """Read a float32 gradient PGM (P5, maxval 65535)."""
    with open(pgm_path, "rb") as f:
        assert f.readline().strip() == b"P5"
        dims = f.readline().split()
        assert f.readline().strip() == b"65535"
        data = np.frombuffer(f.read(), dtype=np.float32)
    cols, rows = int(dims[0]), int(dims[1])
    return data.reshape(rows, cols)


def refine_course(
    zip_path: Path,
    obs_path: Path,
    out_path: Path,
    smooth_radius_cells: float = SMOOTH_RADIUS_CELLS,
) -> Path:
    """Merge recorded observations into a course zip, producing a refined zip.

    For each hole with a costmap, rasterizes the observations onto the existing
    costmap grid and blends them in (confidence-weighted). Steep observations
    also refine the gradient (dz/dx, dz/dy) via :func:`gradient_from_imu`.

    The output zip is a copy of the input with the refined costmap + gradient
    files replaced. Returns the output path.
    """
    obs = load_observations(obs_path)
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        # Extract the zip so we can rewrite individual files.
        with zipfile.ZipFile(zip_path) as zf:
            zf.extractall(tmp)

        # Refine each hole's costmap + gradient.
        for pgm in sorted(tmp.glob("holes/costmap*.pgm")):
            grid, resolution, ox, oy = _read_costmap_grid(pgm)
            new_cost, weight = rasterize_observations(
                obs, ox, oy, resolution, grid.shape, smooth_radius_cells
            )
            refined = blend_costmaps(grid, new_cost, weight)
            write_costmap(Costmap(data=refined, resolution=resolution,
                                  origin_x=ox, origin_y=oy), pgm, pgm.with_suffix(".yaml"))

            # Refine the gradient from steep observations (if present).
            gradx = pgm.with_name(pgm.stem + "_gradx.pgm")
            grady = pgm.with_name(pgm.stem + "_grady.pgm")
            if gradx.exists() and grady.exists():
                gx = _read_gradient_pgm(gradx)
                gy = _read_gradient_pgm(grady)
                # Accumulate gradient corrections from steep observations.
                for o in obs:
                    if o.kind != "steep":
                        continue
                    col = int(math.floor((o.x - ox) / resolution))
                    row = int(math.floor((o.y - oy) / resolution))
                    if 0 <= row < gx.shape[0] and 0 <= col < gx.shape[1]:
                        dzdx, dzdy = gradient_from_imu(o.pitch, o.roll, o.yaw)
                        # Blend toward the measured gradient (weighted by conf).
                        gx[row, col] = (1 - o.conf) * gx[row, col] + o.conf * dzdx
                        gy[row, col] = (1 - o.conf) * gy[row, col] + o.conf * dzdy
                write_gradient(gx, gy, resolution, ox, oy, pgm)

        # Re-zip.
        with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as zf:
            for f in sorted(tmp.rglob("*")):
                if f.is_file():
                    zf.write(f, f.relative_to(tmp))

    return out_path


def main(argv: Optional[List[str]] = None) -> int:
    """CLI: `python3 -m map_editor.refine <course.zip> <obs.jsonl> -o <out.zip>`."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Merge recorded observations into a course map (refinement).")
    parser.add_argument("course_zip", help="existing course zip")
    parser.add_argument("obs_jsonl", help="observations JSONL (from extract_flags.py)")
    parser.add_argument("-o", "--out", help="output refined zip (default: <course>_refined.zip)")
    parser.add_argument("--smooth-radius", type=float, default=SMOOTH_RADIUS_CELLS,
                        help="Gaussian smoothing radius in cells (default: 2.0)")
    args = parser.parse_args(argv)

    out = args.out or (Path(args.course_zip).stem + "_refined.zip")
    refine_course(Path(args.course_zip), Path(args.obs_jsonl), Path(out),
                  smooth_radius_cells=args.smooth_radius)
    print(f"Refined course written to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())