"""Load a saved course Zip back into an editable Course.

The exporter writes `course.yaml` + `holes/holeN.yaml` + `holes/geojson/...`.
This loader reads those back into the in-memory model so the editor can
continue editing a previously saved course.
"""

from __future__ import annotations

import json
import zipfile
from pathlib import Path
from typing import List

import yaml

from map_editor.model import Course, CourseOrigin, Hole, Shape


def _shape_from_geojson(feature: dict) -> Shape:
    """Rebuild a Shape from a GeoJSON feature written by the exporter."""
    props = feature.get("properties", {})
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
        type=props.get("type", "FORBIDDEN_ZONE"),
        label=props.get("label", ""),
        hole_number=props.get("hole_number", 0),
        par=props.get("par", 0),
        distance_m=props.get("distance_m", 0),
        handicap=props.get("handicap", 0),
        vertices=vertices,
    )


def load_course(path: Path) -> Course:
    """Load a course from a saved Zip file."""
    path = Path(path)
    with zipfile.ZipFile(path) as zf:
        course_yaml = yaml.safe_load(zf.read("course.yaml"))

        origin = CourseOrigin(
            latitude_deg=course_yaml.get("origin_latitude_deg", 0.0),
            longitude_deg=course_yaml.get("origin_longitude_deg", 0.0),
            rotation_rad=course_yaml.get("origin_rotation_rad", 0.0),
            course_name=course_yaml.get("course_name", ""),
            course_id=course_yaml.get("course_id", ""),
        )
        course = Course(
            origin=origin,
            osm_id=course_yaml.get("osm_id"),
            bbox=tuple(course_yaml["bbox"]) if course_yaml.get("bbox") else None,
        )

        # Load each hole.
        holes: List[Hole] = []
        for name in zf.namelist():
            if not name.startswith("holes/hole") or not name.endswith(".yaml"):
                continue
            hole_yaml = yaml.safe_load(zf.read(name))
            number = int(hole_yaml.get("hole_number", 0))
            hole = Hole(
                number=number,
                par=hole_yaml.get("par", 0),
                distance_m=hole_yaml.get("distance_m", 0),
                handicap=hole_yaml.get("handicap", 0),
                boundary=[
                    (pt["lat"], pt["lon"]) for pt in hole_yaml.get("boundary", [])
                ],
                zones=hole_yaml.get("zones", []),
                costmap_pgm=hole_yaml.get("costmap_pgm"),
                costmap_yaml=hole_yaml.get("costmap_yaml"),
            )
            # Load shapes from the matching geojson.
            gj_name = f"holes/geojson/hole{number}.geojson"
            if gj_name in zf.namelist():
                gj = json.loads(zf.read(gj_name))
                for feature in gj.get("features", []):
                    props = feature.get("properties", {})
                    if props.get("type") == "BOUNDARY":
                        continue  # boundary already loaded from yaml
                    hole.shapes.append(_shape_from_geojson(feature))
            holes.append(hole)

        course.holes = holes
        return course