"""Unit tests for the golf course map editor."""

import sys
from pathlib import Path

# Ensure the package is importable when running pytest from the repo root.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pytest

from map_editor.model import Course, CourseOrigin, Hole, Shape
from map_editor.exporter import export_course
from map_editor.osm_client import _classify, _parse_course


def make_course() -> Course:
    origin = CourseOrigin(
        latitude_deg=48.12345,
        longitude_deg=11.67890,
        rotation_rad=0.0,
        course_name="Red Course",
        course_id="1234567",
    )
    course = Course(origin=origin, osm_id=1234567, bbox=(48.0, 11.0, 49.0, 12.0))
    hole = Hole(
        number=5,
        name="The Pond",
        par=4,
        handicap=7,
        distances={"red": 380, "white": 350},
        boundary=[(48.12350, 11.67900), (48.12400, 11.67920), (48.12410, 11.67800), (48.12360, 11.67780), (48.12340, 11.67840)],
        shapes=[
            Shape(type="GREEN", label="Green 5", vertices=[(48.12360, 11.67850), (48.12370, 11.67860), (48.12365, 11.67870)]),
            Shape(type="WATER_HAZARD", label="Pond", vertices=[(48.12380, 11.67890), (48.12390, 11.67900), (48.12385, 11.67910)]),
            Shape(type="TEE_BOX", label="Red tee", tee_color="red", vertices=[(48.12355, 11.67830)]),
        ],
    )
    course.holes = [hole]
    return course


def test_export_filename():
    from datetime import datetime
    c = make_course()
    fn = c.export_filename(datetime(2026, 9, 8))
    assert fn == "golfcart-red-course-1234567-20260908.zip", fn


def test_slug():
    assert Course(origin=CourseOrigin(course_name="Augusta National Golf Club")).slug() == "augusta-national-golf-club"


def test_shape_forbidden():
    assert Shape(type="GREEN").is_forbidden
    assert Shape(type="TEE_BOX").is_forbidden
    assert Shape(type="WATER_HAZARD").is_forbidden
    assert Shape(type="ROUGH").is_forbidden
    assert Shape(type="BUNKER").is_forbidden
    assert not Shape(type="FAIRWAY").is_forbidden
    assert Shape(type="FORBIDDEN_ZONE").is_forbidden


def test_hole_yaml_schema():
    """The exported hole YAML follows the new schema: hole props on the hole,
    features are pure geometry + type."""
    c = make_course()
    y = c.holes[0].to_yaml()
    assert y["schema_version"] == 1
    assert y["hole_number"] == 5
    assert y["par"] == 4
    assert y["handicap"] == 7
    assert y["distances"] == {"red": 380, "white": 350}
    assert y["boundary"] and len(y["boundary"]) >= 3
    assert all({"lat", "lon"} <= set(pt) for pt in y["boundary"])
    # Features must NOT carry hole-level props (par/distance/handicap/hole_number).
    for feat in y["features"]:
        assert "par" not in feat
        assert "distance_m" not in feat
        assert "handicap" not in feat
        assert "hole_number" not in feat
    # TEE_BOX carries tee_color.
    tee = next(f for f in y["features"] if f["type"] == "TEE_BOX")
    assert tee["tee_color"] == "red"


def test_course_yaml_schema():
    c = make_course()
    y = c.to_course_yaml()
    assert y["schema_version"] == 1
    assert y["course"]["name"] == "Red Course"
    assert y["course"]["origin"]["latitude_deg"] == 48.12345
    assert y["holes"] == ["holes/hole5.yaml"]


def test_geojson_export():
    c = make_course()
    import json, zipfile
    out = export_course(c, Path("/tmp/golfcart_test_export"))
    assert out.exists()
    with zipfile.ZipFile(out) as zf:
        names = zf.namelist()
        assert "course.yaml" in names
        assert "holes/hole5.yaml" in names
        assert "holes/geojson/hole5.geojson" in names
        gj = json.loads(zf.read("holes/geojson/hole5.geojson"))
        assert gj["type"] == "FeatureCollection"
        assert len(gj["features"]) >= 3  # boundary + green + water


def test_classify():
    assert _classify({"golf": "green"}) == "GREEN"
    assert _classify({"golf": "tee"}) == "TEE_BOX"
    assert _classify({"natural": "water"}) == "WATER_HAZARD"
    assert _classify({"golf": "bunker"}) == "BUNKER"
    assert _classify({"golf": "fairway"}) == "FAIRWAY"
    assert _classify({"golf": "rough"}) == "ROUGH"
    assert _classify({"highway": "path"}) == "PATH"
    assert _classify({"golf": "hole"}) == "HOLE"
    assert _classify({"leisure": "golf_course"}) is None
    assert _classify({"golf": "course"}) is None


def test_parse_course():
    el = {
        "type": "way",
        "id": 42,
        "tags": {"leisure": "golf_course", "name": "Test Course"},
        "center": {"lat": 48.1, "lon": 11.2},
        "bounds": {"minlat": 48.0, "minlon": 11.0, "maxlat": 48.2, "maxlon":  11.4},
    }
    r = _parse_course(el)
    assert r is not None
    assert r.name == "Test Course"
    assert r.osm_id == 42
    assert r.center == (48.1, 11.2)


def test_latlon_roundtrip():
    """lat/lon -> map-frame -> lat/lon round-trip via the origin convention."""
    from map_editor.geo import latlon_to_map, map_to_latlon
    lat, lon = 48.12345,  11.67890
    x, y = latlon_to_map(lat, lon, 48.12345,  11.67890, 0.0)
    assert abs(x) < 1e-6 and abs(y) < 1e-6
    lat2, lon2 = map_to_latlon(x, y, 48.12345,  11.67890, 0.0)
    assert abs(lat2 - lat) < 1e-9
    assert abs(lon2 - lon) < 1e-9


# ---------------------------------------------------------------------------
# DEM / slope costmap
# ---------------------------------------------------------------------------


def test_compute_slope_flat():
    """A flat DEM has zero slope everywhere."""
    import numpy as np
    from map_editor.dem import compute_slope
    z = np.full((5, 5), 100.0)
    slope, aspect = compute_slope(z, 30.0)
    assert np.allclose(slope, 0.0)
    assert np.allclose(aspect, 0.0)


def test_compute_slope_ramp():
    """A uniform ramp has a consistent non-zero slope."""
    import numpy as np
    from map_editor.dem import compute_slope
    # z increases by 1 m per 30 m cell in x -> ~1.9 deg slope.
    z = np.tile(np.arange(5, dtype=np.float64) * 1.0, (5, 1))
    slope, aspect = compute_slope(z, 30.0)
    expected = np.degrees(np.arctan(1.0 / 30.0))
    # Interior cells (not edges) should match.
    assert np.allclose(slope[2, 2], expected, atol=0.1)
    assert slope[2, 2] > 0.0


def test_slope_to_cost_values():
    """Slope below threshold is free; above threshold scales up."""
    import numpy as np
    from map_editor.dem import slope_to_cost_values
    slope = np.array([[0.0, 3.0], [5.0, 10.0]])
    cost = slope_to_cost_values(slope, threshold_deg=5.0)
    assert cost[0, 0] == 0
    assert cost[0, 1] == 0
    assert cost[1, 0] == 0  # exactly at threshold -> 0
    assert cost[1, 1] > 0
    assert cost.max() <= 254


def test_write_costmap_roundtrip():
    """Written PGM+YAML can be read back with matching dimensions."""
    import numpy as np
    from pathlib import Path
    import tempfile
    from map_editor.dem import Costmap, write_costmap
    data = np.zeros((4, 6), dtype=np.uint8)
    data[1, 2] = 200
    cm = Costmap(data=data, resolution=0.05, origin_x=1.0, origin_y=2.0)
    d = Path(tempfile.mkdtemp())
    pgm = d / "c.pgm"
    yml = d / "c.yaml"
    write_costmap(cm, pgm, yml)
    assert pgm.exists() and yml.exists()
    # PGM header: P5, width height, maxval.
    with open(pgm, "rb") as f:
        header = f.readline().strip()
        dims = f.readline().split()
        assert header == b"P5"
        assert [int(x) for x in dims] == [6, 4]
    import yaml
    meta = yaml.safe_load(yml.read_text())
    assert meta["resolution"] == 0.05
    assert meta["origin"] == [1.0, 2.0, 0.0]


def test_sample_elevation():
    """_sample_elevation parses GRAY_INDEX from a mocked GetFeatureInfo reply."""
    from map_editor.dem import _sample_elevation

    class FakeResp:
        def raise_for_status(self):
            pass

        def json(self):
            return {"features": [{"properties": {"GRAY_INDEX": 246.5}}]}

    class FakeSession:
        def get(self, url, params=None, timeout=None):
            assert params["request"] == "GetFeatureInfo"
            assert params["crs"] == "EPSG:4326"
            return FakeResp()

    z = _sample_elevation(48.775, 9.18, FakeSession())
    assert z == pytest.approx(246.5)


def test_sample_elevation_no_data():
    """_sample_elevation returns None when no features are returned."""
    from map_editor.dem import _sample_elevation

    class FakeResp:
        def raise_for_status(self):
            pass

        def json(self):
            return {"features": []}

    class FakeSession:
        def get(self, url, params=None, timeout=None):
            return FakeResp()

    assert _sample_elevation(48.0, 9.0, FakeSession()) is None


def test_fetch_getmap():
    """_fetch_getmap decodes a grayscale PNG into a 2D array."""
    import io
    import numpy as np
    from PIL import Image
    from map_editor.dem import _fetch_getmap

    # Build a small grayscale PNG.
    arr = np.zeros((4, 6), dtype=np.uint8)
    arr[1, 2] = 200
    buf = io.BytesIO()
    Image.fromarray(arr, mode="L").save(buf, format="PNG")

    class FakeResp:
        def raise_for_status(self):
            pass

        @property
        def content(self):
            return buf.getvalue()

    class FakeSession:
        def get(self, url, params=None, timeout=None):
            assert params["request"] == "GetMap"
            return FakeResp()

    gray = _fetch_getmap(48.0, 9.0, 48.1, 9.1, 6, 4, FakeSession())
    assert gray is not None
    assert gray.shape == (4, 6)
    assert gray[1, 2] == pytest.approx(200.0)


def test_calibrate_elevation():
    """_calibrate_elevation fits a linear gray->elevation mapping."""
    import numpy as np
    from map_editor.dem import _calibrate_elevation

    # gray = 100 -> 200 m, gray = 200 -> 400 m  => scale=2, offset=0.
    gray = np.array([[100.0, 200.0], [100.0, 200.0]])

    class FakeSession:
        def __init__(self):
            self.calls = 0

        def get(self, url, params=None, timeout=None):
            self.calls += 1
            # Return elevation = 2 * gray for the sampled point.
            # The sampled gray is read from the `gray` array by position.
            return None  # not used; _sample_elevation is patched below

    # Patch _sample_elevation to return elevation = 2 * gray at the sampled cell.
    import map_editor.dem as dem

    orig = dem._sample_elevation

    def fake_sample(lat, lon, session, timeout=15.0):
        # Map lat/lon back to a gray value via the same formula fetch_dem uses.
        rows, cols = gray.shape
        r = int(round((48.1 - lat) / (48.1 - 48.0) * (rows - 1)))
        c = int(round((lon - 9.0) / (9.1 - 9.0) * (cols - 1)))
        r = max(0, min(rows - 1, r))
        c = max(0, min(cols - 1, c))
        return 2.0 * gray[r, c]

    dem._sample_elevation = fake_sample
    try:
        scale, offset = _calibrate_elevation(FakeSession(), 48.0, 9.0, 48.1, 9.1, gray, n_points=2)
    finally:
        dem._sample_elevation = orig
    assert scale == pytest.approx(2.0, abs=0.5)
    assert offset == pytest.approx(0.0, abs=50.0)


def test_save_load_roundtrip():
    """A saved course Zip can be loaded back with shapes and boundary intact."""
    import tempfile
    from map_editor.exporter import export_course
    from map_editor.loader import load_course

    c = make_course()
    d = Path(tempfile.mkdtemp())
    out = export_course(c, d)
    loaded = load_course(out)

    assert loaded.course_name == "Red Course"
    assert len(loaded.holes) == 1
    hole = loaded.holes[0]
    assert hole.number == 5
    assert hole.par == 4
    assert hole.handicap == 7
    assert hole.distances == {"red": 380, "white": 350}
    assert len(hole.boundary) == 5
    # Shapes: green + water hazard + tee box (boundary is not a shape).
    assert len(hole.shapes) == 3
    types = {s.type for s in hole.shapes}
    assert "GREEN" in types
    assert "WATER_HAZARD" in types
    assert "TEE_BOX" in types
    # A polygon shape should have >=3 vertices.
    green = next(s for s in hole.shapes if s.type == "GREEN")
    assert len(green.vertices) >= 3
    # tee_color survives the roundtrip.
    tee = next(s for s in hole.shapes if s.type == "TEE_BOX")
    assert tee.tee_color == "red"


def test_validator_accepts_valid_course():
    from map_editor.validator import validate_course, validate_hole
    c = make_course()
    assert validate_course(c.to_course_yaml()) == []
    assert validate_hole(c.holes[0].to_yaml()) == []


def test_validator_rejects_bad_hole():
    from map_editor.validator import validate_hole
    # Missing boundary.
    assert validate_hole({"schema_version": 1, "hole_number": 5}) != []
    # Boundary too small.
    assert validate_hole({
        "schema_version": 1,
        "hole_number": 5,
        "boundary": [{"lat": 1, "lon": 1}, {"lat": 2, "lon": 2}],
    }) != []
    # Unknown feature type.
    assert validate_hole({
        "schema_version": 1,
        "hole_number": 5,
        "boundary": [{"lat": 1, "lon": 1}, {"lat": 2, "lon": 2}, {"lat": 3, "lon": 3}],
        "features": [{"type": "NOT_A_TYPE", "geometry": {"type": "Point", "coordinates": [1, 2]}}],
    }) != []
    # Invalid tee color.
    assert validate_hole({
        "schema_version": 1,
        "hole_number": 5,
        "boundary": [{"lat": 1, "lon": 1}, {"lat": 2, "lon": 2}, {"lat": 3, "lon": 3}],
        "distances": {"purple": 300},
    }) != []


def test_validator_rejects_bad_course():
    from map_editor.validator import validate_course
    # Missing course object.
    assert validate_course({"schema_version": 1, "holes": []}) != []
    # Origin out of range.
    assert validate_course({
        "schema_version": 1,
        "course": {"name": "X", "origin": {"latitude_deg": 999, "longitude_deg": 0, "rotation_rad": 0}},
        "holes": [],
    }) != []