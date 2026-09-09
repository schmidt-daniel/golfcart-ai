"""Export the course to a Zip bundle the trolley and web app can consume.

Structure:
    course.yaml
    holes/holeN.yaml          (geofence-compatible superset)
    holes/costmapN.pgm        (optional, copied)
    holes/costmapN.yaml
    holes/geojson/holeN.geojson  (for the Leaflet web app)
"""

from __future__ import annotations

import json
import shutil
import zipfile
from pathlib import Path
from typing import Optional

import yaml

from map_editor.model import Course, Hole, Shape


def _hole_yaml(hole: Hole, course: Course) -> dict:
    return hole.to_yaml(course.origin)


def _hole_geojson(hole: Hole) -> dict:
    """Build a GeoJSON FeatureCollection for a hole (for the web app)."""
    features = [s.to_geojson() for s in hole.shapes]
    # The boundary polygon as a feature too.
    if len(hole.boundary) >= 3:
        features.insert(0, {
            "type": "Feature",
            "properties": {"type": "BOUNDARY", "label": f"Hole {hole.number} boundary", "forbidden": False},
            "geometry": {
                "type": "Polygon",
                "coordinates": [[[lon, lat] for lat, lon in hole.boundary]],
            },
        })
    return {"type": "FeatureCollection", "features": features}


def export_course(course: Course, out_dir: Path, filename: Optional[str] = None) -> Path:
    """Export the course to a Zip file. Returns the output path."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    filename = filename or course.export_filename()
    out_path = out_dir / filename

    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as zf:
        # course.yaml
        zf.writestr("course.yaml", yaml.safe_dump(course.to_course_yaml(), sort_keys=False))

        for hole in course.holes:
            if not hole.boundary:
                continue
            # holes/holeN.yaml
            hole_yaml = _hole_yaml(hole, course)
            zf.writestr(f"holes/hole{hole.number}.yaml", yaml.safe_dump(hole_yaml, sort_keys=False))

            # holes/geojson/holeN.geojson
            zf.writestr(f"holes/geojson/hole{hole.number}.geojson", json.dumps(_hole_geojson(hole), indent=2))

            # holes/costmapN.pgm + .yaml (optional)
            if hole.costmap_pgm and Path(hole.costmap_pgm).exists():
                zf.write(hole.costmap_pgm, f"holes/costmap{hole.number}.pgm")
            if hole.costmap_yaml and Path(hole.costmap_yaml).exists():
                zf.write(hole.costmap_yaml, f"holes/costmap{hole.number}.yaml")

    return out_path