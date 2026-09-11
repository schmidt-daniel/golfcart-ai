"""Unit tests for the learning/refinement merge math (map_editor.refine)."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import numpy as np
import pytest

from map_editor.refine import (
    Observation,
    blend_costmaps,
    gradient_from_imu,
    rasterize_observations,
    refine_course,
    load_observations,
    save_observations,
)


def test_rasterize_same_grid_alignment():
    """Observations rasterize onto the exact existing grid (1:1 cell alignment)."""
    # 10x10 grid, 5 m cells, origin at (0,0).
    shape = (10, 10)
    obs = [Observation(kind="drivable", x=12.5, y=12.5, conf=1.0)]
    new_cost, weight = rasterize_observations(obs, 0.0, 0.0, 5.0, shape)
    assert new_cost.shape == shape
    assert weight.shape == shape
    # The observation at (12.5, 12.5) lands in cell (row=2, col=2).
    assert weight[2, 2] > 0
    # Neighbors get some weight (smoothing), and the center dominates.
    assert weight[2, 3] > 0
    assert weight[2, 2] > weight[2, 3]
    # A genuinely far cell gets (essentially) no weight.
    assert weight[9, 9] < 1e-6


def test_rasterize_outside_grid_ignored():
    """Observations outside the grid are ignored (no crash, no contribution)."""
    shape = (10, 10)
    obs = [Observation(kind="obstacle", x=9999.0, y=9999.0, conf=1.0)]
    new_cost, weight = rasterize_observations(obs, 0.0, 0.0, 5.0, shape)
    assert np.all(weight == 0)
    assert np.all(new_cost == 0)


def test_blend_preserves_old_where_no_data():
    """Where there's no new data, the old costmap is preserved exactly."""
    old = np.full((10, 10), 100, dtype=np.uint8)
    new_cost = np.zeros((10, 10))
    weight = np.zeros((10, 10))
    refined = blend_costmaps(old, new_cost, weight)
    assert np.array_equal(refined, old)


def test_blend_approaches_new_value_with_weight():
    """High observation weight pulls the result toward the new value."""
    old = np.full((10, 10), 100, dtype=np.uint8)
    new_cost = np.zeros((10, 10))
    weight = np.zeros((10, 10))
    # Contract: new_cost = target * weight (from rasterize). weight=100 of
    # value 254 -> new_cost = 254*100, so the blend approaches 254.
    weight[5, 5] = 100.0  # >> baseline -> result ~ new value
    new_cost[5, 5] = 254.0 * 100.0
    refined = blend_costmaps(old, new_cost, weight)
    assert refined[5, 5] > 200
    # Far cells unchanged.
    assert refined[0, 0] == 100


def test_blend_smooth_transition():
    """Blending produces a smooth transition, not a hard edge."""
    old = np.full((20, 20), 50, dtype=np.uint8)
    obs = [Observation(kind="obstacle", x=52.5, y=52.5, conf=1.0)]  # cell (10,10)
    new_cost, weight = rasterize_observations(obs, 0.0, 0.0, 5.0, (20, 20))
    refined = blend_costmaps(old, new_cost, weight)
    # Center is high, edge is unchanged, and there's a monotonic ramp between.
    assert refined[10, 10] > refined[10, 11] > refined[0, 0]
    assert refined[0, 0] == 50


def test_gradient_from_imu_roundtrip():
    """gradient_from_imu inverts the slope_node projection."""
    # slope_node: pitch = dzdx*cos(yaw) + dzdy*sin(yaw)
    #             roll  = -dzdx*sin(yaw) + dzdy*cos(yaw)
    dzdx, dzdy = 0.1, -0.05
    yaw = 0.7
    cy, sy = np.cos(yaw), np.sin(yaw)
    pitch = dzdx * cy + dzdy * sy
    roll = -dzdx * sy + dzdy * cy
    rx, ry = gradient_from_imu(pitch, roll, yaw)
    assert rx == pytest.approx(dzdx, abs=1e-9)
    assert ry == pytest.approx(dzdy, abs=1e-9)


def test_gradient_from_imu_facing_east():
    """Facing east (yaw=0), pitch maps directly to dzdx, roll to dzdy."""
    dzdx, dzdy = gradient_from_imu(0.1, -0.05, 0.0)
    assert dzdx == pytest.approx(0.1)
    assert dzdy == pytest.approx(-0.05)


def test_observations_roundtrip():
    """Observations serialize to JSONL and load back."""
    obs = [
        Observation(kind="drivable", x=1.0, y=2.0, conf=0.9),
        Observation(kind="steep", x=3.0, y=4.0, conf=1.0, yaw=0.5, pitch=0.3, roll=0.1),
        Observation(kind="obstacle", x=5.0, y=6.0, conf=0.8),
    ]
    import tempfile
    d = Path(tempfile.mkdtemp())
    p = d / "obs.jsonl"
    save_observations(obs, p)
    loaded = load_observations(p)
    assert len(loaded) == 3
    assert loaded[0].kind == "drivable"
    assert loaded[1].kind == "steep"
    assert loaded[1].yaw == pytest.approx(0.5)
    assert loaded[2].kind == "obstacle"


def _build_course_with_costmap(d, grid, resolution=5.0):
    """Build + export a course zip whose hole 1 has the given costmap grid."""
    from map_editor.model import Course, CourseOrigin, Hole
    from map_editor.exporter import export_course
    from map_editor.dem import write_costmap, Costmap

    course = Course(origin=CourseOrigin(latitude_deg=48.0, longitude_deg=11.0,
                                        course_name="Test", course_id="1"))
    hole = Hole(number=1, boundary=[(48.0, 11.0), (48.001, 11.0), (48.001, 11.001), (48.0, 11.001)])
    course.holes = [hole]
    pgm = d / "hole1_slope.pgm"
    write_costmap(Costmap(data=grid, resolution=resolution,
                          origin_x=0.0, origin_y=0.0), pgm, pgm.with_suffix(".yaml"))
    hole.costmap_pgm = str(pgm)
    hole.costmap_yaml = str(pgm.with_suffix(".yaml"))
    return export_course(course, d)


def _read_costmap_from_zip(zip_path):
    """Read the hole-1 costmap grid back from a course zip."""
    import zipfile
    import io
    with zipfile.ZipFile(zip_path) as zf:
        raw = zf.read("holes/costmap1.pgm")
    # Parse P5 (P5\n<w> <h>\n255\n<payload>).
    import io as _io
    f = _io.BytesIO(raw)
    assert f.readline().strip() == b"P5"
    w, h = map(int, f.readline().split())
    assert f.readline().strip() == b"255"
    return np.frombuffer(f.read(), dtype=np.uint8).reshape(h, w)


def test_refine_course_roundtrip():
    """refine_course produces a zip with a refined costmap."""
    import tempfile
    import zipfile

    d = Path(tempfile.mkdtemp())
    src = _build_course_with_costmap(d, np.full((10, 10), 100, dtype=np.uint8))

    # Observations: an obstacle in the middle of the grid.
    obs = d / "obs.jsonl"
    save_observations([Observation(kind="obstacle", x=25.0, y=25.0, conf=1.0)], obs)

    out = d / "refined.zip"
    refine_course(src, obs, out)

    with zipfile.ZipFile(out) as zf:
        names = zf.namelist()
        assert "holes/costmap1.pgm" in names
        refined = zf.read("holes/costmap1.pgm")
        assert len(refined) > 0


def test_refine_course_alignment_and_smoothing():
    """Refinement raises cost at an obstacle, smooth at the border, keeps far.

    This is the key alignment property: an observation at a point should raise
    the cost there, fall off smoothly to neighbors, and leave far, unobserved
    cells unchanged (no border artifacts from old/new data colliding).
    """
    import tempfile

    d = Path(tempfile.mkdtemp())
    base = np.full((20, 20), 50, dtype=np.uint8)
    src = _build_course_with_costmap(d, base, resolution=2.0)

    # Obstacle at (20,20)m -> cell (row=10, col=10) at 2 m/cell.
    obs = d / "obs.jsonl"
    save_observations([Observation(kind="obstacle", x=20.0, y=20.0, conf=1.0)], obs)

    out = d / "refined.zip"
    refine_course(src, obs, out)
    refined = _read_costmap_from_zip(out)

    assert refined.shape == (20, 20)
    # Center cell (obstacle) is much higher than baseline.
    assert refined[10, 10] > 150
    # Smooth falloff: center > immediate neighbor > far corner, all monotonic.
    assert refined[10, 10] > refined[10, 11]
    assert refined[10, 11] >= refined[0, 0]
    # Far, unobserved cells are preserved exactly (no drift).
    assert refined[0, 0] == 50
    assert refined[19, 19] == 50


def test_refine_course_drivable_lowers_cost():
    """A 'drivable' flag (trolley drove here) lowers the steep cost there."""
    import tempfile

    d = Path(tempfile.mkdtemp())
    # A uniformly steep grid (high DEM-derived cost), so drivable is meaningful.
    base = np.full((20, 20), 254, dtype=np.uint8)
    src = _build_course_with_costmap(d, base, resolution=2.0)

    # Trolley drove through (20,20)m and (22,20)m -> along the x-axis.
    obs = d / "obs.jsonl"
    save_observations([
        Observation(kind="drivable", x=20.0, y=20.0, conf=1.0),
        Observation(kind="drivable", x=22.0, y=20.0, conf=1.0),
    ], obs)

    out = d / "refined.zip"
    refine_course(src, obs, out)
    refined = _read_costmap_from_zip(out)

    # The two driven cells drop well below the lethal baseline.
    assert refined[10, 10] < 200
    assert refined[10, 11] < 200
    # A far, unobserved cell stays lethal (unchanged).
    assert refined[0, 0] == 254