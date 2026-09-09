# Golf Course Map Editor

A desktop (PySide6/Qt) editor for building golf course maps for the golf cart
project. It lets you search & select a golf course on OpenStreetMap, download
its OSM data, render it over a transparent satellite view, edit shapes
(greens, tees, water hazards, paths, rough, forbidden zones), associate a
slope-derived costmap per hole, and export the whole course as a Zip bundle
the trolley (Nav2/geofence) and the web app can consume.

## Features

- **Search & select** a golf course on OpenStreetMap (Nominatim geocode +
  Overpass `leisure=golf_course`).
- **Download** all golf elements within the course bbox (greens, tee boxes,
  water hazards, bunkers, fairways, paths, rough, holes) and parse them into
  typed, editable shapes.
- **Render** over an **Esri World Imagery** satellite base layer with an
  adjustable transparency slider (tiles cached locally).
- **Edit shapes**: select/move vertices, add/delete vertices, add/delete
  shapes, edit properties (type, label, hole, par, distance, handicap).
- **Click-to-draw**: draw new polygons/lines/points by clicking on the map
  (double-click to finish); draw the hole boundary polygon with the boundary
  tool. Vertices snap to nearby existing vertices.
- **Move shapes between holes** via the "Move selected to hole" dropdown.
- **Forbidden zones**: greens, tees, water hazards, rough, and bunkers are all
  treated as forbidden zones (exported to `forbidden_zones` + geofence zones).
- **Costmap association**: load a slope-derived costmap (`.pgm` + `.yaml` from
  Nav2 `map_saver_cli`) and associate it with a hole.
- **DEM → slope costmap**: generate a slope costmap from a digital elevation
  model (DEM) for the current hole, with a slope heatmap overlay in the editor.
  Fetches the whole elevation field in one GetMap request (fast).
- **Save / Load**: save the working course to a Zip and reload it to continue
  editing.
- **Export** the whole course as a Zip bundle.

## Requirements

- Python 3.10+
- PySide6, requests, PyYAML (see `requirements.txt`)

```bash
pip install -r requirements.txt
```

## Run

```bash
python -m map_editor
```

## Export format

The editor exports a Zip file named
`golfcart-{course-slug}-{osm-id}-{YYYYMMDD}.zip` with this structure:

```text
course.yaml
holes/holeN.yaml
holes/costmapN.pgm        (optional)
holes/costmapN.yaml       (optional)
holes/geojson/holeN.geojson
```

- **`course.yaml`** holds course metadata, the map origin datum under
  `course.origin` (`latitude_deg` / `longitude_deg` / `rotation_rad`), and the
  **tee taxonomy** under `course.tees` — a flexible dictionary of tee IDs to
  `{name, slope?, cr?}`. This matches the convention used by
  `georeference_node` and the geofence.
- **`holes/holeN.yaml`** holds the hole boundary + hole-level properties
  (`par`, `handicap`, per-tee `distances` keyed by tee ID) + typed `features`
  (geometry only). It drops straight into `golfcart_geofence/config/` and is
  picked up by `geofence.launch.py config_file:=holeN.yaml course_file:=course.yaml`.
- **`holes/geojson/holeN.geojson`** is a GeoJSON FeatureCollection for the
  Leaflet web app (`src/golfcart_teleop/web/`).

## Tee taxonomy

Tees are **not** fixed colors. `course.yaml` defines the taxonomy:

```yaml
course:
  tees:
    red:  {name: "Red",  slope: 2.0, cr: 72.0}
    blue: {name: "Blue", slope: 1.5, cr: 70.0}
    A:    {name: "A",    slope: 1.0, cr: 70.0}
```

Hole files reference these IDs for `distances` and `TEE_BOX` features
(`tee_id`). Any ID scheme works (colors, names, symbols, numbers).

## Schema & validation

The format is specified by JSON Schemas in `schema/`:

- `schema/course.schema.json` — top-level course (origin, identity, tees, hole refs).
- `schema/hole.schema.json` — per-hole (boundary, hole props, features).

`map_editor/validator.py` validates parsed YAML against these schemas
(`validate_course` / `validate_hole`). It uses `jsonschema` when available and
falls back to a lightweight structural check otherwise. Hole-level properties
(par, handicap, distances) live on the hole — **not** on individual features.

## Tests

```bash
python -m pytest tests/
```

## Notes / scope

- Satellite tiles come from Esri World Imagery (free, no API key) and are
  cached under `~/.cache/golfcart-map-editor/tiles`.
- **DEM source:** for Baden-Württemberg, LGL **DGM1 (1 m)** is the recommended
  free source (LiDAR-derived). Fallback for courses outside BW: EU-DEM (25 m)
  or BKG DGM25 (25 m). The DEM transport in `map_editor/dem.py` is currently a
  scaffold — wire it to the verified LGL endpoint (see
  `plans/plan-map-editor.prompt.md`, DEM section) before relying on real data.
- Slope is stored as **magnitude + aspect**; the cart resolves roll/pitch from
  its actual heading at runtime. The DEM is **upsampled/interpolated** to
  costmap resolution (more detail, not more accuracy than the source).
- ROS `CourseMap` publishing and Nav2 `static_layer` costmap wiring are
  deferred follow-ups (see `plans/plan-map-editor.prompt.md`).