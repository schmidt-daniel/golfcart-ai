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

- **`holes/holeN.yaml`** is a **superset** of what
  `golfcart_geofence/src/geofence_node.cpp::load_config()` reads
  (`boundary`, `origin_latitude_deg`, `origin_longitude_deg`, `hole_number`),
  so it drops straight into `golfcart_geofence/config/` and is picked up by
  `geofence.launch.py config_file:=holeN.yaml`. Editor-only fields (par,
  distances, handicap, zones, costmap ref) are ignored by the node.
- **`holes/geojson/holeN.geojson`** is a GeoJSON FeatureCollection for the
  Leaflet web app (`src/golfcart_teleop/web/`).
- **`course.yaml`** holds course metadata and the origin datum
  (`origin_latitude_deg` / `origin_longitude_deg` / `origin_rotation_rad`),
  matching the convention used by `georeference_node` and the geofence.

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