"""Georeferencing helpers (lat/lon <-> map-frame).

Matches the convention used by `georeference_node.cpp` and `geofence_math.hpp`:
equirectangular projection with R=6371000 m, x=east, y=north, then rotated
by `origin_rotation_rad`.
"""

from __future__ import annotations

import math

R_EARTH = 6371000.0


def latlon_to_map(
    lat: float, lon: float, origin_lat: float, origin_lon: float, rotation_rad: float = 0.0
) -> tuple[float, float]:
    """Convert lat/lon to map-frame (x=east, y=north) meters."""
    x = math.radians(lon - origin_lon) * R_EARTH * math.cos(math.radians(origin_lat))
    y = math.radians(lat - origin_lat) * R_EARTH
    if rotation_rad:
        cos = math.cos(rotation_rad)
        sin = math.sin(rotation_rad)
        x, y = x * cos - y * sin, x * sin + y * cos
    return x, y


def map_to_latlon(
    x: float, y: float, origin_lat: float, origin_lon: float, rotation_rad: float = 0.0
) -> tuple[float, float]:
    """Convert map-frame (x=east, y=north) meters back to lat/lon."""
    if rotation_rad:
        cos = math.cos(rotation_rad)
        sin = math.sin(rotation_rad)
        x, y = x * cos + y * sin, -x * sin + y * cos
    lat = origin_lat + math.degrees(y / R_EARTH)
    lon = origin_lon + math.degrees(x / (R_EARTH * math.cos(math.radians(origin_lat))))
    return lat, lon