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
        par=4,
        distance_m=380,
        handicap=7,
        boundary=[(48.12350, 11.67900), (48.12400, 11.67920), (48.12410, 11.67800), (48.12360, 11.67780), (48.12340, 11.67840)],
        shapes=[
            Shape(type="GREEN", label="Green 5", hole_number=5, vertices=[(48.12360, 11.67850), (48.12370, 11.67860), (48.12365, 11.67870)]),
            Shape(type="WATER_HAZARD", label="Pond", hole_number=5, vertices=[(48.12380, 11.67890), (48.12390, 11.67900), (48.12385, 11.67910)]),
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


def test_hole_yaml_geofence_compatible():
    """The exported hole YAML must contain the fields geofence_node reads."""
    c = make_course()
    y = c.holes[0].to_yaml(c.origin)
    assert y["boundary"] and len(y["boundary"]) >= 3
    assert "origin_latitude_deg" in y
    assert "origin_longitude_deg" in y
    assert y["hole_number"] == 5
    assert all({"lat", "lon"} <= set(pt) for pt in y["boundary"])


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