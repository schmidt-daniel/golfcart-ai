"""Validate course/hole YAML files against the JSON Schemas.

The schemas live in `schema/` next to this package:
    schema/course.schema.json
    schema/hole.schema.json

This module loads them and validates parsed YAML dicts. It uses `jsonschema`
if available; otherwise it falls back to a lightweight structural check so the
editor still works on hosts without the dependency.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, List, Optional

import yaml

_SCHEMA_DIR = Path(__file__).resolve().parent.parent / "schema"

try:
    import jsonschema

    _HAS_JSONSCHEMA = True
except Exception:  # pragma: no cover - depends on environment
    _HAS_JSONSCHEMA = False


def _load_schema(name: str) -> Dict[str, Any]:
    with open(_SCHEMA_DIR / name) as f:
        return json.load(f)


def validate_course(data: Dict[str, Any]) -> List[str]:
    """Validate a parsed course.yaml dict. Returns a list of error strings (empty = valid)."""
    return _validate(data, "course.schema.json")


def validate_hole(data: Dict[str, Any]) -> List[str]:
    """Validate a parsed holeN.yaml dict. Returns a list of error strings (empty = valid)."""
    return _validate(data, "hole.schema.json")


def _validate(data: Dict[str, Any], schema_name: str) -> List[str]:
    if _HAS_JSONSCHEMA:
        schema = _load_schema(schema_name)
        validator = jsonschema.Draft7Validator(schema)
        errors = sorted(validator.iter_errors(data), key=lambda e: list(e.path))
        return [f"{'/'.join(str(p) for p in e.path)}: {e.message}" for e in errors]
    return _fallback_validate(data, schema_name)


def _fallback_validate(data: Dict[str, Any], schema_name: str) -> List[str]:
    """Minimal structural check when jsonschema is unavailable.

    This mirrors the most important constraints of the JSON Schemas so the
    editor still catches obvious errors without the dependency. It is not a
    full replacement — install `jsonschema` for complete validation.
    """
    errors: List[str] = []
    if not isinstance(data, dict):
        return ["document must be a mapping"]

    if schema_name == "course.schema.json":
        if data.get("schema_version") != 1:
            errors.append("schema_version: must be 1")
        course = data.get("course")
        if not isinstance(course, dict):
            errors.append("course: missing 'course' object")
        else:
            origin = course.get("origin")
            if not isinstance(origin, dict):
                errors.append("course.origin: required")
            else:
                lat = origin.get("latitude_deg")
                lon = origin.get("longitude_deg")
                if not isinstance(lat, (int, float)) or not (-90 <= lat <= 90):
                    errors.append("course.origin.latitude_deg: must be in [-90, 90]")
                if not isinstance(lon, (int, float)) or not (-180 <= lon <= 180):
                    errors.append("course.origin.longitude_deg: must be in [-180, 180]")
        if not isinstance(data.get("holes"), list):
            errors.append("holes: missing 'holes' list")
    else:
        if data.get("schema_version") != 1:
            errors.append("schema_version: must be 1")
        if not isinstance(data.get("hole_number"), int) or data.get("hole_number", 0) < 1:
            errors.append("hole_number: required integer >= 1")
        boundary = data.get("boundary")
        if not isinstance(boundary, list) or len(boundary) < 3:
            errors.append("boundary: required sequence of >=3 points")
        else:
            for i, pt in enumerate(boundary):
                if not isinstance(pt, dict) or "lat" not in pt or "lon" not in pt:
                    errors.append(f"boundary[{i}]: must have lat and lon")
        for feat in data.get("features", []) or []:
            if not isinstance(feat, dict) or "type" not in feat or "geometry" not in feat:
                errors.append("features: each feature needs type and geometry")
            elif feat.get("type") not in _FEATURE_TYPES:
                errors.append(f"features: unknown type '{feat.get('type')}'")
        for color in (data.get("distances", {}) or {}):
            if color not in _TEE_COLORS:
                errors.append(f"distances: unknown tee color '{color}'")
    return errors


_TEE_COLORS = {
    "red", "blue", "white", "yellow", "green", "black", "gold", "silver", "bronze",
}

_FEATURE_TYPES = {
    "GREEN", "TEE_BOX", "HOLE", "FAIRWAY", "HAZARD",
    "WATER_HAZARD", "BUNKER", "ROUGH", "PATH", "FORBIDDEN_ZONE", "EXIT_POINT",
}


def validate_course_file(path: Path) -> List[str]:
    """Validate a course.yaml file on disk."""
    with open(path) as f:
        data = yaml.safe_load(f)
    return validate_course(data)


def validate_hole_file(path: Path) -> List[str]:
    """Validate a holeN.yaml file on disk."""
    with open(path) as f:
        data = yaml.safe_load(f)
    return validate_hole(data)
