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
    """A single editable feature on the course (lat/lon geometry).

    A feature is pure geometry + type. Hole-level properties (par, distances,
    handicap) live on the :class:`Hole`, not on individual features.
    """

    type: str = "FORBIDDEN_ZONE"
    label: str = ""
    #: For TEE_BOX features: which tee color this box serves.
    tee_color: Optional[str] = None
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
        props: Dict[str, Any] = {
            "type": self.type,
            "label": self.label,
            "forbidden": self.is_forbidden,
        }
        if self.tee_color:
            props["tee_color"] = self.tee_color
        if self.osm_id is not None:
            props["osm_id"] = self.osm_id
        if self.osm_type is not None:
            props["osm_type"] = self.osm_type
        return {
            "type": "Feature",
            "properties": props,
            "geometry": {"type": geom_type, "coordinates": geometry_coords},
        }

    def to_feature(self) -> Dict[str, Any]:
        """Serialize to the schema-native feature form (type on the object).

        This is what goes into the hole YAML `features` list. Unlike
        :meth:`to_geojson` (which wraps in a GeoJSON Feature), the type is the
        feature type directly, matching `hole.schema.json`.
        """
        geom = self.to_geojson()["geometry"]
        feat: Dict[str, Any] = {
            "type": self.type,
            "geometry": geom,
        }
        if self.label:
            feat["label"] = self.label
        if self.tee_color:
            feat["tee_color"] = self.tee_color
        if self.osm_id is not None:
            feat["osm_id"] = self.osm_id
        if self.osm_type is not None:
            feat["osm_type"] = self.osm_type
        return feat


@dataclass
class Hole:
    """A single hole on the course.

    Hole-level properties (par, handicap, per-tee-color distances) live here,
    NOT on individual features.
    """

    number: int = 0
    name: str = ""
    par: int = 0
    handicap: int = 0
    #: Distance (m) per tee color, e.g. {"red": 380, "white": 350}.
    distances: Dict[str, float] = field(default_factory=dict)
    #: Outer playable boundary (lat, lon) polygon, >=3 vertices.
    boundary: List[tuple[float, float]] = field(default_factory=list)
    #: Inner typed zones (tee boxes, hole markers, etc.) as lat/lon points with radius.
    zones: List[Dict[str, Any]] = field(default_factory=list)
    #: All editable features for this hole.
    shapes: List[Shape] = field(default_factory=list)
    #: Associated slope costmap (optional). Path to .pgm + .yaml pair.
    costmap_pgm: Optional[str] = None
    costmap_yaml: Optional[str] = None
    #: In-memory slope grid (degrees) for heatmap overlay, if generated.
    slope_deg: Optional[Any] = None
    #: In-memory aspect grid (degrees), if generated.
    aspect_deg: Optional[Any] = None

    def to_yaml(self) -> Dict[str, Any]:
        """Serialize to the per-hole YAML dict (new schema)."""
        doc: Dict[str, Any] = {
            "schema_version": 1,
            "hole_number": self.number,
            "boundary": [{"lat": lat, "lon": lon} for lat, lon in self.boundary],
        }
        if self.name:
            doc["name"] = self.name
        if self.par:
            doc["par"] = self.par
        if self.handicap:
            doc["handicap"] = self.handicap
        if self.distances:
            doc["distances"] = self.distances
        if self.shapes:
            doc["features"] = [shape.to_feature() for shape in self.shapes]
        if self.costmap_pgm or self.costmap_yaml:
            costmap: Dict[str, Any] = {}
            if self.costmap_pgm:
                costmap["pgm"] = self.costmap_pgm
            if self.costmap_yaml:
                costmap["yaml"] = self.costmap_yaml
            doc["costmap"] = costmap
        return doc


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
        course: Dict[str, Any] = {
            "name": self.course_name,
            "origin": {
                "latitude_deg": self.origin.latitude_deg,
                "longitude_deg": self.origin.longitude_deg,
                "rotation_rad": self.origin.rotation_rad,
            },
        }
        if self.origin.course_id:
            course["id"] = self.origin.course_id
        if self.osm_id is not None:
            course["osm_id"] = self.osm_id
        if self.bbox:
            course["bbox"] = list(self.bbox)
        return {
            "schema_version": 1,
            "course": course,
            "holes": [f"holes/hole{h.number}.yaml" for h in self.holes],
        }


def slugify(name: str) -> str:
    """Sanitize a name into a filename-safe slug."""
    return re.sub(r"[^a-zA-Z0-9]+", "-", name.lower()).strip("-") or "course"