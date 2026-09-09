"""OpenStreetMap client for the golf course map editor.

Uses the Overpass API to search for golf courses and download all golf
elements within a course bbox, and Nominatim to geocode search queries.
"""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

import requests

from map_editor.model import Course, CourseOrigin, Hole, Shape

OVERPASS_URL = "https://overpass-api.de/api/interpreter"
NOMINATIM_URL = "https://nominatim.openstreetmap.org/search"
USER_AGENT = "golfcart-map-editor/0.1 (golf cart project)"


@dataclass
class CourseSearchResult:
    """A golf course found by search."""

    name: str
    osm_id: int
    osm_type: str
    bbox: Tuple[float, float, float, float]  # (min_lat, min_lon, max_lat, max_lon)
    center: Tuple[float, float]  # (lat, lon)


def _headers() -> Dict[str, str]:
    return {"User-Agent": USER_AGENT}


def geocode(query: str) -> List[Dict[str, Any]]:
    """Geocode a free-text query via Nominatim. Returns raw results."""
    resp = requests.get(
        NOMINATIM_URL,
        params={"q": query, "format": "json", "limit": 20},
        headers=_headers(),
        timeout=30,
    )
    resp.raise_for_status()
    return resp.json()


def search_courses(query: str) -> List[CourseSearchResult]:
    """Search for golf courses by name/location.

    Uses Nominatim to geocode the query, then Overpass to find golf courses
    within the geocoded bbox. Falls back to a plain Overpass name search.
"""
    results: List[CourseSearchResult] = []
    try:
        geo = geocode(query)
        if geo:
            bbox = (
                float(geo[0]["boundingbox"][0]),
                float(geo[0]["boundingbox"][2]),
                float(geo[0]["boundingbox"][1]),
                float(geo[0]["boundingbox"][3]),
            )
            results.extend(_overpass_courses_in_bbox(bbox))
    except requests.RequestException:
        pass

    if not results:
        results.extend(_overpass_courses_by_name(query))
    return results


def _overpass_courses_in_bbox(bbox: Tuple[float, float, float, float]) -> List[CourseSearchResult]:
    """Find golf courses within a bbox via Overpass."""
    min_lat, min_lon, max_lat, max_lon = bbox
    query = f"""
    [out:json][timeout:30];
    (
      way["leisure"="golf_course"]({min_lat},{min_lon},{max_lat},{max_lon});
      way["golf"="course"]({min_lat},{min_lon},{max_lat},{max_lon});
      relation["leisure"="golf_course"]({min_lat},{min_lon},{max_lat},{max_lon});
    );
    out center;
    """
    data = _overpass(query)
    return [_parse_course(el) for el in data.get("elements", []) if _parse_course(el)]


def _overpass_courses_by_name(name: str) -> List[CourseSearchResult]:
    """Find golf courses whose name matches via Overpass."""
    query = f"""
    [out:json][timeout:30];
    (
      way["leisure"="golf_course"]["name"~"{name}",i];
      way["golf"="course"]["name"~"{name}",i];
      relation["leisure"="golf_course"]["name"~"{name}",i];
    );
    out center;
    """
    data = _overpass(query)
    return [_parse_course(el) for el in data.get("elements", []) if _parse_course(el)]


def _parse_course(el: Dict[str, Any]) -> Optional[CourseSearchResult]:
    name = el.get("tags", {}).get("name") or el.get("tags", {}).get("ref") or "Unnamed course"
    center = el.get("center") or el.get("bounds", {}).get("center") or {}
    if not center:
        return None
    lat = float(center["lat"])
    lon = float(center["lon"])
    b = el.get("bounds") or {}
    if b:
        bbox = (float(b["minlat"]), float(b["minlon"]), float(b["maxlat"]), float(b["maxlon"]))
    else:
        bbox = (lat - 0.005, lon - 0.005, lat + 0.005, lon + 0.005)
    return CourseSearchResult(
        name=name,
        osm_id=int(el["id"]),
        osm_type=el["type"],
        bbox=bbox,
        center=(lat, lon),
    )


def _overpass(query: str) -> Dict[str, Any]:
    resp = requests.post(OVERPASS_URL, data={"data": query}, headers=_headers(), timeout=60)
    resp.raise_for_status()
    return resp.json()


# ---------------------------------------------------------------------------
# Course data download
# ---------------------------------------------------------------------------

#: OSM tag -> shape type mapping.
OSM_TO_TYPE = {
    "golf=green": "GREEN",
    "golf=tee": "TEE_BOX",
    "golf=water_hazard": "WATER_HAZARD",
    "natural=water": "WATER_HAZARD",
    "golf=bunker": "BUNKER",
    "golf=fairway": "FAIRWAY",
    "golf=rough": "ROUGH",
    "golf=path": "PATH",
    "highway=path": "PATH",
    "highway=footway": "PATH",
    "golf=hole": "HOLE",
    "golf=pin": "HOLE",
}


def _classify(tags: Dict[str, str]) -> Optional[str]:
    """Classify an OSM element into a shape type, or None if not golf-relevant."""
    for key, val in tags.items():
        tag = f"{key}={val}"
        if tag in OSM_TO_TYPE:
            return OSM_TO_TYPE[tag]
    # A golf course area itself is not a playable shape.

    if tags.get("leisure") == "golf_course" or tags.get("golf") == "course":
        return None
    return None


def download_course(result: CourseSearchResult) -> Course:
    """Download all golf elements within the course bbox and build a Course."""
    min_lat, min_lon, max_lat, max_lon = result.bbox
    query = f"""
    [out:json][timeout:60];
    (
      way["golf"]({min_lat},{min_lon},{max_lat},{max_lon});
      way["natural"="water"]({min_lat},{min_lon},{max_lat},{max_lon});
      way["highway"="path"]({min_lat},{min_lon},{max_lat},{max_lon});
      way["highway"="footway"]({min_lat},{min_lon},{max_lat},{max_lon});
      node["golf"]({min_lat},{min_lon},{max_lat},{max_lon});
      relation["golf"]({min_lat},{min_lon},{max_lat},{max_lon});
    );
    out body;
    >;
    out skel qt;
    """
    data = _overpass(query)
    elements = data.get("elements", [])
    nodes = {el["id"]: el for el in elements if el["type"] == "node"}
    ways = {el["id"]: el for el in elements if el["type"] == "way"}

    shapes: List[Shape] = []
    for el in elements:
        if el["type"] not in ("way", "node"):
            continue
        tags = el.get("tags", {})
        stype = _classify(tags)
        if stype is None:
            continue
        if el["type"] == "way":
            verts = []
            for ref in el.get("nodes", []):
                node = nodes.get(ref)
                if node:
                    verts.append((float(node["lat"]), float(node["lon"])))
            if len(verts) < 2:
                continue
        else:  # node
            verts = [(float(el["lat"]), float(el["lon"]))]
        shapes.append(Shape(
            type=stype,
            label=tags.get("name", ""),
            vertices=verts,
            osm_id=int(el["id"]),
            osm_type=el["type"],
        ))

    origin = CourseOrigin(
        latitude_deg=result.center[0],
        longitude_deg=result.center[1],
        rotation_rad=0.0,
        course_name=result.name,
        course_id=str(result.osm_id),
    )
    return Course(origin=origin, osm_id=result.osm_id, bbox=result.bbox, holes=[])


def assign_holes(course: Course, num_holes: int = 18) -> None:
    """Assign shapes to holes by proximity to the course center.

    Simple heuristic: split shapes into `num_holes` clusters along the course
    extent. For now, assign all shapes to hole 1 unless the user edits.
"""
    if not course.holes:
        course.holes = [Hole(number=i + 1) for i in range(num_holes)]
    # Default: put all shapes on hole 1 (user reassigns in the editor).
    course.holes[0].shapes = course.holes[0].shapes + [s for s in _all_shapes(course)]
    return None


def _all_shapes(course: Course) -> List[Shape]:
    out = []
    for h in course.holes:
        out.extend(h.shapes)
    return out