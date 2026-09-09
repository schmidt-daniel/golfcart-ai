"""Load a saved course Zip back into an editable Course.

The exporter writes `course.yaml` + `holes/holeN.yaml` + `holes/geojson/...`.
This loader reads those back into the in-memory model so the editor can
continue editing a previously saved course.
"""

from __future__ import annotations

import json
import zipfile
from pathlib import Path
from typing import Dict, List

import yaml

from map_editor.model import Course, CourseOrigin, Hole, Shape, Tee


def _shape_from_feature(feature: dict) -> Shape:
    """Rebuild a Shape from a schema-native feature (type on the object).

    Handles both the schema-native form (type directly on the object, as in the
    hole YAML `features` list) and the legacy GeoJSON Feature wrapper (type in
    `properties`).
    """
    if "properties" in feature and "geometry" in feature and feature.get("type") == "Feature":
        # Legacy GeoJSON Feature wrapper.
        props = feature.get("properties", {})
        ftype = props.get("type", "FORBIDDEN_ZONE")
        label = props.get("label", "")
        tee_id = props.get("tee_id") or props.get("tee_color")
        osm_id = props.get("osm_id")
        osm_type = props.get("osm_type")
        geom = feature.get("geometry", {})
    else:
        # Schema-native form.
        ftype = feature.get("type", "FORBIDDEN_ZONE")
        label = feature.get("label", "")
        tee_id = feature.get("tee_id") or feature.get("tee_color")
        osm_id = feature.get("osm_id")
        osm_type = feature.get("osm_type")
        geom = feature.get("geometry", {})

    gtype = geom.get("type")
    coords = geom.get("coordinates")

    if gtype == "Point":
        lon, lat = coords
        vertices = [(lat, lon)]
    elif gtype == "LineString":
        vertices = [(lat, lon) for lon, lat in coords]
    else:  # Polygon
        ring = coords[0] if coords else []
        vertices = [(lat, lon) for lon, lat in ring]

    return Shape(
        type=ftype,
        label=label,
        tee_id=tee_id,
        osm_id=osm_id,
        osm_type=osm_type,
        vertices=vertices,
    )


def load_course(path: Path) -> Course:
    """Load a course from a saved Zip file."""
    path = Path(path)
    with zipfile.ZipFile(path) as zf:
        course_yaml = yaml.safe_load(zf.read("course.yaml"))

        # New format: {schema_version, course: {name, origin, ...}, holes: [...]}
        course_doc = course_yaml.get("course", course_yaml)
        origin_doc = course_doc.get("origin", {})
        origin = CourseOrigin(
            latitude_deg=origin_doc.get("latitude_deg", 0.0),
            longitude_deg=origin_doc.get("longitude_deg", 0.0),
            rotation_rad=origin_doc.get("rotation_rad", 0.0),
            course_name=course_doc.get("name", ""),
            course_id=course_doc.get("id", ""),
        )
        # Load the tee taxonomy: {tee_id: {name, slope?, cr?}}.
        tees: Dict[str, Tee] = {}
        for tid, tdoc in (course_doc.get("tees", {}) or {}).items():
            tees[tid] = Tee(
                name=tdoc.get("name", tid),
                slope=tdoc.get("slope"),
                cr=tdoc.get("cr"),
            )
        course = Course(
            origin=origin,
            osm_id=course_doc.get("osm_id"),
            bbox=tuple(course_doc["bbox"]) if course_doc.get("bbox") else None,
            tees=tees,
        )

        # Load each hole.
        holes: List[Hole] = []
        for name in zf.namelist():
            if not name.startswith("holes/hole") or not name.endswith(".yaml"):
                continue
            hole_yaml = yaml.safe_load(zf.read(name))
            number = int(hole_yaml.get("hole_number", 0))
            costmap = hole_yaml.get("costmap", {}) or {}
            hole = Hole(
                number=number,
                name=hole_yaml.get("name", ""),
                par=hole_yaml.get("par", 0),
                handicap=hole_yaml.get("handicap", 0),
                distances=hole_yaml.get("distances", {}),
                boundary=[
                    (pt["lat"], pt["lon"]) for pt in hole_yaml.get("boundary", [])
                ],
                zones=hole_yaml.get("zones", []),
                costmap_pgm=costmap.get("pgm"),
                costmap_yaml=costmap.get("yaml"),
            )
            # Load features from the hole yaml (new format) or the matching geojson.
            features = hole_yaml.get("features")
            if features is None:
                gj_name = f"holes/geojson/hole{number}.geojson"
                if gj_name in zf.namelist():
                    gj = json.loads(zf.read(gj_name))
                    features = [
                        f for f in gj.get("features", [])
                        if f.get("properties", {}).get("type") != "BOUNDARY"
                    ]
            for feature in features or []:
                hole.shapes.append(_shape_from_feature(feature))
            holes.append(hole)

        course.holes = holes
        return course