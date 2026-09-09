"""Course data model for the golf course map editor.

Defines the on-disk course format (source of truth for export) and the in-memory
shape model. The per-hole YAML is a **superset** of what
`geofence_node.cpp::load_config()` reads (boundary, origin_latitude_deg,
origin_longitude_deg, hole_number) so it drops straight into
`golfcart_geofence/config/` and is picked up by `geofence.launch.py`.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field, asdict
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional

import yaml


# ---------------------------------------------------------------------------
# Shape types
# ---------------------------------------------------------------------------

#: Feature types that are treated as forbidden zones (greens, tees, water, rough, bunkers).
FORBIDDEN_TYPES = {"GREEN", "TEE_BOX", "WATER_HAZARD", "ROUGH", "BUNKER"}

#: All supported shape types.
SHAPE_TYPES = [
    "GREEN",
    "TEE_BOX",
    "HOLE",
    "FAIRWAY",
    "HAZARD",
    "WATER_HAZARD",
    "BUNKER",
    "ROUGH",
    "PATH",
    "FORBIDDEN_ZONE",
    "EXIT_POINT",
]


@dataclass
class Shape:
    """A single editable shape on the course (lat/lon coordinates)."""

    type: str = "FORBIDDEN_ZONE"
    label: str = ""
    hole_number: int = 0
    par: int = 0
    distance_m: int = 0
    handicap: int = 0
    #: lat/lon vertices (list of (lat, lon) tuples). For a point, a single vertex.
    #: For a polygon, >=3 vertices. For a line, >=2 vertices.
    vertices: List[tuple[float, float]] = field(default_factory=list)
    #: OSM element id (if imported from OSM).
    osm_id: Optional[int] = None
    osm_type: Optional[str] = None

    @property
    def is_forbidden(self) -> bool:
        return self.type in FORBIDDEN_TYPES or self.type == "FORBIDDEN_ZONE"

    @property
    def is_point(self) -> bool:
        return self.type in ("HOLE", "EXIT_POINT")

    def to_geojson(self) -> Dict[str, Any]:
        """Serialize to a GeoJSON Feature (Polygon/LineString/Point, [lon, lat])."""
        coords = [[lon, lat] for lat, lon in self.vertices]
        if self.is_point:
            geom_type = "Point"
            geometry_coords = coords[0] if coords else None
        elif len(coords) >= 2 and self.type == "PATH":
            geom_type = "LineString"
            geometry_coords = coords
        else:
            geom_type = "Polygon"
            geometry_coords = [coords]  # outer ring
        return {
            "type": "Feature",
            "properties": {
                "type": self.type,
                "label": self.label,
                "hole_number": self.hole_number,
                "par": self.par,
                "distance_m": self.distance_m,
                "handicap": self.handicap,
                "forbidden": self.is_forbidden,
            },
            "geometry": {"type": geom_type, "coordinates": geometry_coords},
        }


@dataclass
class Hole:
    """A single hole on the course."""

    number: int = 0
    par: int = 0
    distance_m: int = 0
    handicap: int = 0
    #: Outer playable boundary (lat, lon) polygon, >=3 vertices.

    boundary: List[tuple[float, float]] = field(default_factory=list)
    #: Inner typed zones (tee boxes, hole markers, etc.) as lat/lon points with radius.

    zones: List[Dict[str, Any]] = field(default_factory=list)
    #: All editable shapes for this hole.

    shapes: List[Shape] = field(default_factory=list)
    #: Associated slope costmap (optional). Path to .pgm + .yaml pair.

    costmap_pgm: Optional[str] = None
    costmap_yaml: Optional[str] = None

    def to_yaml(self, course_origin: "CourseOrigin") -> Dict[str, Any]:
        """Serialize to the geofence-compatible per-hole YAML dict."""
        return {
            "course_name": course_origin.course_name,
            "hole_number": self.number,
            "origin_latitude_deg": course_origin.latitude_deg,
            "origin_longitude_deg": course_origin.longitude_deg,
            "origin_rotation_rad": course_origin.rotation_rad,
            "boundary": [{"lat": lat, "lon": lon} for lat, lon in self.boundary],
            "zones": self.zones,
            "par": self.par,
            "distance_m": self.distance_m,
            "handicap": self.handicap,
            "costmap_pgm": self.costmap_pgm,
            "costmap_yaml": self.costmap_yaml,
            "shapes": [shape.to_geojson() for shape in self.shapes],
        }


@dataclass
class CourseOrigin:
    """Course map origin (datum for lat/lon -> map-frame conversion)."""

    latitude_deg: float = 0.0
    longitude_deg: float = 0.0
    rotation_rad: float = 0.0
    course_name: str = ""
    course_id: str = ""


@dataclass
class Course:
    """The whole course being edited."""

    origin: CourseOrigin = field(default_factory=CourseOrigin)
    osm_id: Optional[int] = None
    bbox: Optional[tuple[float, float, float, float]] = None  # (min_lat, min_lon, max_lat, max_lon)
    holes: List[Hole] = field(default_factory=list)

    @property
    def course_name(self) -> str:
        return self.origin.course_name

    def slug(self) -> str:
        """Sanitized course name for filenames."""
        s = re.sub(r"[^a-zA-Z0-9]+", "-", self.course_name.lower()).strip("-")
        return s or "course"

    def export_filename(self, when: Optional[datetime] = None) -> str:
        """`golfcart-{slug}-{osm_id}-{YYYYMMDD}.zip` (date-only)."""
        when = when or datetime.now()
        osm = self.osm_id or "unknown"
        return f"golfcart-{self.slug()}-{osm}-{when:%Y%m%d}.zip"

    def to_course_yaml(self) -> Dict[str, Any]:
        return {
            "course_name": self.course_name,
            "course_id": self.origin.course_id,
            "osm_id": self.osm_id,
            "origin_latitude_deg": self.origin.latitude_deg,
            "origin_longitude_deg": self.origin.longitude_deg,
            "origin_rotation_rad": self.origin.rotation_rad,
            "bbox": list(self.bbox) if self.bbox else None,
            "holes": [h.number for h in self.holes],
        }


def slugify(name: str) -> str:
    """Sanitize a name into a filename-safe slug."""
    return re.sub(r"[^a-zA-Z0-9]+", "-", name.lower()).strip("-") or "course"