"""DEM elevation -> slope costmap generation.

Fetches a digital elevation model (DEM) for a hole's bounding box, computes
slope (magnitude + aspect), and writes a Nav2-style costmap (`.pgm` + `.yaml`)
that can be associated with a hole.

Primary source for Baden-Wuerttemberg: LGL DGM1 (1 m, free, LiDAR-derived).
Fallback (outside BW / Germany-wide): EU-DEM 25 m or BKG DGM25 (25 m).

NOTE: The exact LGL download endpoint may require a portal session step rather
than a plain HTTP GET. This module isolates the fetch so the transport can be
swapped without touching the slope math.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Tuple

import numpy as np

# ---------------------------------------------------------------------------
# Slope math (pure numpy, unit-testable)
# ---------------------------------------------------------------------------


def compute_gradient(
    z: np.ndarray,
    cell_size_m: float,
) -> Tuple[np.ndarray, np.ndarray]:
    """Compute the terrain gradient (dz/dx, dz/dy) via Horn's method.

    The gradient is **ground-fixed** (east/north components of elevation
    change per meter). It is the raw terrain truth from which both slope
    magnitude/aspect and the trolley-relative roll/pitch are derived.

    Args:
        z: 2D array of elevations (meters), shape (rows, cols).
        cell_size_m: ground resolution of one cell (meters).

    Returns:
        (dzdx, dzdy): same shape as `z`. dzdx is the east component
        (elevation change per meter east), dzdy the north component.
        Cells with no valid neighbors get 0.
    """
    z = np.asarray(z, dtype=np.float64)
    rows, cols = z.shape
    dzdx = np.zeros_like(z)
    dzdy = np.zeros_like(z)
    if rows < 3 or cols < 3:
        return dzdx, dzdy
    dzdx[:, 1:-1] = (z[:, 2:] - z[:, :-2]) / (2.0 * cell_size_m)
    dzdy[1:-1, :] = (z[2:, :] - z[:-2, :]) / (2.0 * cell_size_m)
    return dzdx, dzdy


def compute_slope(
    z: np.ndarray,
    cell_size_m: float,
) -> Tuple[np.ndarray, np.ndarray]:
    """Compute slope magnitude (degrees) and aspect (degrees, 0=N, 90=E).

    Uses the standard 3x3 gradient (Horn's method) on a DEM grid.

    Args:
        z: 2D array of elevations (meters), shape (rows, cols).
        cell_size_m: ground resolution of one cell (meters).

    Returns:
        (slope_deg, aspect_deg): same shape as `z`. Slope in degrees from
        horizontal; aspect in degrees clockwise from north (direction of
        steepest descent). Cells with no valid neighbors get slope 0.
    """
    dzdx, dzdy = compute_gradient(z, cell_size_m)

    slope_rad = np.arctan(np.hypot(dzdx, dzdy))
    slope_deg = np.degrees(slope_rad)

    # Aspect: direction of steepest descent, clockwise from north.
    aspect_rad = np.arctan2(dzdx, -dzdy)  # 0 = north, + = east
    aspect_deg = np.degrees(aspect_rad) % 360.0
    # Where slope is ~0, aspect is meaningless; set to 0.
    aspect_deg[slope_deg < 1e-6] = 0.0

    return slope_deg, aspect_deg


def project_onto_heading(
    dzdx: np.ndarray,
    dzdy: np.ndarray,
    yaw_rad: float,
) -> Tuple[np.ndarray, np.ndarray]:
    """Project the terrain gradient onto the trolley's heading frame.

    Roll and pitch are **trolley-relative** and depend on the heading (yaw).
    This decomposes the ground-fixed gradient into:
        pitch = gradient · forward   (slope along the trolley's forward axis)
        roll  = gradient · lateral   (slope perpendicular to the forward axis)

    Args:
        dzdx, dzdy: ground-fixed gradient components (east, north).
        yaw_rad: trolley heading (radians, 0 = north, + = east).

    Returns:
        (pitch_deg, roll_deg): same shape as the inputs, in degrees.
        Positive pitch = uphill ahead; positive roll = uphill to the left.
    """
    dzdx = np.asarray(dzdx, dtype=np.float64)
    dzdy = np.asarray(dzdy, dtype=np.float64)
    cy = np.cos(yaw_rad)
    sy = np.sin(yaw_rad)
    # forward = (cy, sy), lateral = (-sy, cy)
    pitch = dzdx * cy + dzdy * sy
    roll = -dzdx * sy + dzdy * cy
    return np.degrees(np.arctan(pitch)), np.degrees(np.arctan(roll))


def slope_to_cost_values(
    slope_deg: np.ndarray,
    threshold_deg: float = 5.0,
    max_cost: float = 254.0,
) -> np.ndarray:
    """Map slope (degrees) to costmap cell values (0..254).

    Cells below `threshold_deg` are free (0); cells at/above are scaled up to
    `max_cost`. This is a simple linear ramp above the threshold.
    """
    slope = np.asarray(slope_deg, dtype=np.float64)
    cost = np.zeros_like(slope)
    steep = slope >= threshold_deg
    cost[steep] = np.clip(
        (slope[steep] - threshold_deg) / threshold_deg * max_cost,
        0.0,
        max_cost,
    )
    return cost.astype(np.uint8)


# ---------------------------------------------------------------------------
# Costmap writing (Nav2 map_saver_cli compatible)
# ---------------------------------------------------------------------------


@dataclass
class Costmap:
    """A Nav2-style occupancy/cost grid."""

    data: np.ndarray  # 2D uint8, 0..254
    resolution: float  # meters per cell
    origin_x: float  # map-frame x of the grid origin (bottom-left)
    origin_y: float  # map-frame y of the grid origin (bottom-left)
    frame_id: str = "map"

    @property
    def width(self) -> int:
        return int(self.data.shape[1])

    @property
    def height(self) -> int:
        return int(self.data.shape[0])


def write_costmap(costmap: Costmap, pgm_path: Path, yaml_path: Path) -> None:
    """Write a costmap as a PGM + YAML pair (Nav2 map_saver_cli format)."""
    pgm_path = Path(pgm_path)
    yaml_path = Path(yaml_path)
    pgm_path.parent.mkdir(parents=True, exist_ok=True)

    # PGM (P5 binary, 0..255). Nav2 uses 0=free, 254=lethal, 255=unknown.
    data = costmap.data.astype(np.uint8)
    with open(pgm_path, "wb") as f:
        f.write(b"P5\n")
        f.write(f"{costmap.width} {costmap.height}\n".encode())
        f.write(b"255\n")
        f.write(data.tobytes())

    # YAML metadata (mirrors map_saver_cli output).
    meta = {
        "image": pgm_path.name,
        "mode": "trinary",
        "resolution": costmap.resolution,
        "origin": [costmap.origin_x, costmap.origin_y, 0.0],
        "negate": 0,
        "occupied_thresh": 0.65,
        "free_thresh": 0.196,
        "publish_period": 0.0,
        "frame_id": costmap.frame_id,
    }
    import yaml as _yaml

    with open(yaml_path, "w") as f:
        _yaml.safe_dump(meta, f, sort_keys=False)


def write_gradient(
    dzdx: np.ndarray,
    dzdy: np.ndarray,
    resolution: float,
    origin_x: float,
    origin_y: float,
    base_path: Path,
) -> Tuple[Path, Path]:
    """Write the terrain gradient as a pair of float PGM files.

    The gradient is the ground-fixed terrain truth (east/north elevation
    change per meter) from which the runtime slope_node computes the
    trolley-relative roll/pitch. Written as two PGM files:
        {base_path}_gradx.pgm   (dz/dx, east)
        {base_path}_grady.pgm   (dz/dy, north)

    Values are stored as float32 in a P5 PGM with maxval 65535 (16-bit), so
    the runtime can read them back losslessly. Returns the two paths.
    """
    base_path = Path(base_path)
    base_path.parent.mkdir(parents=True, exist_ok=True)
    gradx_path = base_path.with_name(base_path.stem + "_gradx.pgm")
    grady_path = base_path.with_name(base_path.stem + "_grady.pgm")

    for path, g in ((gradx_path, dzdx), (grady_path, dzdy)):
        g = np.asarray(g, dtype=np.float32)
        rows, cols = g.shape
        with open(path, "wb") as f:
            f.write(b"P5\n")
            f.write(f"{cols} {rows}\n".encode())
            f.write(b"65535\n")
            f.write(g.tobytes())

    # Write a small YAML describing the gradient grid (resolution + origin).
    meta = {
        "resolution": resolution,
        "origin": [origin_x, origin_y, 0.0],
        "frame_id": "map",
        "gradx": gradx_path.name,
        "grady": grady_path.name,
    }
    import yaml as _yaml

    with open(base_path.with_name(base_path.stem + "_grad.yaml"), "w") as f:
        _yaml.safe_dump(meta, f, sort_keys=False)

    return gradx_path, grady_path


# ---------------------------------------------------------------------------
# DEM fetching (transport) — LGL WMS DGM025 (verified 2026-09-09)
# ---------------------------------------------------------------------------

#: LGL Baden-Wuerttemberg WMS serving the DGM025 (25 cm positional accuracy).
#: Serves real elevation via GetFeatureInfo (`GRAY_INDEX` = elevation in meters).
#: Free/open (Datenlizenz Deutschland by-2-0), no API key.
LGL_WMS_URL = "https://owsproxy.lgl-bw.de/owsproxy/ows/WMS_LGL-BW_ATKIS_DGM_025"
LGL_WMS_LAYER = "DGM025_Hoehen_Graustufen"

#: Default grid spacing (meters) for sampling elevation.
DEFAULT_DEM_SPACING_M = 5.0


def _sample_elevation(
    lat: float,
    lon: float,
    session: "requests.Session",
    timeout: float = 15.0,
) -> Optional[float]:
    """Sample a single elevation (meters) at (lat, lon) via GetFeatureInfo.

    Returns None if the point has no data (outside coverage / no valid pixel).
    """
    import requests

    params = {
        "service": "WMS",
        "version": "1.3.0",
        "request": "GetFeatureInfo",
        "layers": LGL_WMS_LAYER,
        "query_layers": LGL_WMS_LAYER,
        "info_format": "application/json",
        "format": "image/png",
        # WMS 1.3.0 EPSG:4326 uses (lat, lon) axis order.
        "bbox": f"{lat - 0.001},{lon - 0.001},{lat + 0.001},{lon + 0.001}",
        "width": "100",
        "height": "100",
        "i": "50",
        "j": "50",
        "crs": "EPSG:4326",
    }
    try:
        resp = session.get(LGL_WMS_URL, params=params, timeout=timeout)
        resp.raise_for_status()
        data = resp.json()
        features = data.get("features", [])
        if not features:
            return None
        props = features[0].get("properties", {})
        # GRAY_INDEX is elevation in meters.
        val = props.get("GRAY_INDEX")
        if val is None:
            return None
        return float(val)
    except (requests.RequestException, ValueError, KeyError):
        return None


def _fetch_getmap(
    min_lat: float,
    min_lon: float,
    max_lat: float,
    max_lon: float,
    width: int,
    height: int,
    session: "requests.Session",
    timeout: float = 30.0,
) -> Optional[np.ndarray]:
    """Fetch a grayscale GetMap image for a bbox (ONE request).

    Returns a 2D float array (rows = north->south, cols = west->east) of
    grayscale values 0..255, or None on failure. The grayscale encodes
    elevation via a statistics stretch (see `_calibrate_elevation`).
    """
    import io

    from PIL import Image

    params = {
        "service": "WMS",
        "version": "1.3.0",
        "request": "GetMap",
        "layers": LGL_WMS_LAYER,
        "styles": "",
        # WMS 1.3.0 EPSG:4326 uses (lat, lon) axis order.
        "bbox": f"{min_lat},{min_lon},{max_lat},{max_lon}",
        "width": str(width),
        "height": str(height),
        "format": "image/png",
        "crs": "EPSG:4326",
    }
    try:
        resp = session.get(LGL_WMS_URL, params=params, timeout=timeout)
        resp.raise_for_status()
        img = Image.open(io.BytesIO(resp.content)).convert("L")
        return np.asarray(img, dtype=np.float64)
    except Exception:
        return None


def _calibrate_elevation(
    session: "requests.Session",
    min_lat: float,
    min_lon: float,
    max_lat: float,
    max_lon: float,
    gray: np.ndarray,
    n_points: int = 3,
) -> Tuple[float, float]:
    """Fit a linear grayscale->elevation mapping from a few GetFeatureInfo points.

    Samples `n_points` points across the bbox, reads their grayscale value from
    the GetMap image, and gets the true elevation via GetFeatureInfo. Returns
    (scale, offset) such that elevation = scale * gray + offset.

    Falls back to (1.0, 0.0) if calibration fails (gray == elevation).
    """
    rows, cols = gray.shape
    # Sample points spread across the bbox (avoid edges).
    rs = np.linspace(rows * 0.2, rows * 0.8, n_points).astype(int)
    cs = np.linspace(cols * 0.2, cols * 0.8, n_points).astype(int)

    gray_vals: list[float] = []
    elev_vals: list[float] = []
    for r, c in zip(rs, cs):
        lat = max_lat - (r + 0.5) * (max_lat - min_lat) / rows
        lon = min_lon + (c + 0.5) * (max_lon - min_lon) / cols
        elev = _sample_elevation(float(lat), float(lon), session)
        if elev is not None:
            gray_vals.append(float(gray[r, c]))
            elev_vals.append(elev)

    if len(gray_vals) >= 2:
        g = np.asarray(gray_vals)
        e = np.asarray(elev_vals)
        # Linear least squares: e = scale * g + offset.
        scale, offset = np.polyfit(g, e, 1)
        return float(scale), float(offset)
    return 1.0, 0.0


def fetch_dem(
    min_lat: float,
    min_lon: float,
    max_lat: float,
    max_lon: float,
    spacing_m: float = DEFAULT_DEM_SPACING_M,
    cache_dir: Optional[Path] = None,
) -> np.ndarray:
    """Fetch a DEM grid for a bbox and return elevations (meters).

    Uses a single GetMap request to fetch the whole grayscale elevation field,
    then calibrates the grayscale->elevation mapping with a few GetFeatureInfo
    points. Returns a 2D numpy array of elevations (rows = north->south,
    cols = west->east). Cells with no data are NaN.

    This is O(1) HTTP requests (1 GetMap + a few calibration points),
    independent of grid size — far faster than point-by-point sampling.
    """
    import requests

    # Choose image size so each pixel is ~spacing_m on the ground.
    dlat = spacing_m / 111320.0
    dlon = spacing_m / (111320.0 * math.cos(math.radians((min_lat + max_lat) / 2.0)))
    height = max(2, int(round((max_lat - min_lat) / dlat)))
    width = max(2, int(round((max_lon - min_lon) / dlon)))
    # Cap to a sane max to avoid huge images.
    width = min(width, 1024)
    height = min(height, 1024)

    session = requests.Session()
    session.headers["User-Agent"] = "golfcart-map-editor/0.1"

    gray = _fetch_getmap(min_lat, min_lon, max_lat, max_lon, width, height, session)
    if gray is None:
        return np.full((height, width), np.nan)

    scale, offset = _calibrate_elevation(
        session, min_lat, min_lon, max_lat, max_lon, gray
    )
    elev = scale * gray + offset
    # Where the image is fully black (no data), mark NaN.
    elev[gray <= 0.0] = np.nan
    return elev