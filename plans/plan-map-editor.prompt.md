# Plan: Desktop Golf Course Map Editor

**TL;DR** — Build a Python desktop map editor (PySide6/Qt) that lets a user search & select a golf course on OpenStreetMap, download its OSM data, render it over a transparent satellite view, edit shapes (greens, tees, water hazards, paths, rough, forbidden zones), associate a slope-derived costmap per hole, and export the whole course as a Zip bundle the trolley (Nav2/geofence) and the HMI/WebApp can consume.

## Steps

### Phase 0 — Architecture & scaffolding
1. Add `tools/map_editor/` (Python package, PySide6). Add `requirements.txt` (PySide6, requests, pyproj, shapely, pyyaml, Pillow). Add `README.md`.
2. Define the on-disk course format (source of truth for export): `course.yaml` (name, CR, location, origin lat/lon/rotation) + per-hole `holeN.yaml` (number, par, distances, handicap, boundary, typed zones) + optional `costmapN.pgm/.yaml`. Mirror the existing `hole5.yaml` geofence structure.

### Phase 1 — Course search & selection (OSM)
3. `osm_client.py`: Overpass API query for `leisure=golf_course` (and `golf=course`) within a search bbox; return course list (name, id, bbox, center).
4. Search UI: text query → geocode (Nominatim) → list results → select → store course bbox/center.



### Phase 2 — OSM data download & render
5. `osm_client.py`: Overpass query for all golf elements within the course bbox (greens `leisure=golf_course`/`landuse=grass`+`golf=green`, tee boxes `golf=tee`, water hazards `natural=water`/`golf=water_hazard`, bunkers `golf=bunker`, fairways `golf=fairway`, paths `highway=path`/`golf=path`, rough `golf=rough`, etc.). Parse into typed shapes (shapely.

6. Render view: PySide6 QGraphicsView canvas. Base layer = satellite imagery (Esri World Imagery tiles, cached locally)with adjustable transparency slider. Overlay = OSM shapes as editable polygons/polylines/points.



### Phase 3 — Shape editing
7. Editing tools: select/move vertices, add/delete vertices, add new shape (polygon/line/point), delete shape, edit properties (type, label, hole_number, par, distances). Snap-to-vertex.

8. Forbidden zones: greens, tees, water hazards, rough, bunkers all treated as forbidden zones(exported to `forbidden_zones` + geofence zones). Dedicated"forbidden zone" tool.



### Phase 4 — Costmap association
9. Load a slope-derived costmap (`.pgm` + `.yaml` from Nav2 map_saver_cli, or a GeoJSON/GeoTIFF)and associate it with a hole. Store as `costmapN.pgm/.yaml` in the hole folder. (Slope costmap generation itself is out of scope — editor only loads/associates.)

### Phase 4b — DEM → slope costmap generation (IMPLEMENTED 2026-09-09)
9b. `map_editor/dem.py`: `compute_slope()` (Horn's method, magnitude + aspect),
    `slope_to_cost_values()`, `write_costmap()` (PGM+YAML, Nav2 format),
    `fetch_dem()` (GetMap-based, fast). "Generate slope costmap from DEM…"
    button in the main window; slope heatmap overlay in the canvas. Hole model
    gained `slope_deg`/`aspect_deg` in-memory fields.
    - **DEM source:** LGL WMS DGM025 (verified 2026-09-09) — serves real
      elevation via GetFeatureInfo (`GRAY_INDEX` = meters). `fetch_dem()` uses
      ONE GetMap request (whole grayscale field) + a few GetFeatureInfo points
      to calibrate gray->elevation (O(1) HTTP requests). Live-tested: 0.01x0.01
      deg @10 m -> 111x73 grid, elev 248.5-269.8 m, max slope 31.08 deg.
    - **Slope representation:** magnitude + aspect (cart resolves roll/pitch at
      runtime). **Resolution:** DEM upsampled/interpolated to costmap res.
    - **New deps:** numpy (requirements.txt + Dockerfile). rasterio removed
      (GetMap uses PIL, not rasterio).

### Phase 3b — Drawing tools, boundary, save/load, hole assignment (IMPLEMENTED 2026-09-09)
7b. Canvas gained click-to-draw (polygon/line/point, double-click to finish),
    boundary-drawing tool, and snap-to-vertex. Main window gained Draw
    shape / Draw boundary / Select tool buttons, "Move selected to hole"
    dropdown, and Save… / Load… (via new `map_editor/loader.py`). Removed the
    unused rasterio dep. 17 tests pass in container.



### Phase 5 — Export
10. Export whole course → Zip: `course.yaml` + `holes/holeN.yaml` + `holes/costmapN.pgm/.yaml` + `holes/geojson/` (per-hole GeoJSON for web). Emit the same origin convention (`origin_latitude_deg`/`origin_longitude_deg`/`origin_rotation_rad`) used by `georeference_node`/geofence.
11. **IMPLEMENTED 2026-09-09** — `course_loader_node` (golfcart_navigation) reads an exported course Zip and publishes `CourseMap` on `/course/map` (transient-local). `map_server` added to `navigation.launch.py` to serve the static costmap to `global_costmap`'s `static_layer`. `scripts/deploy_course.sh` copies the Zip to the cart, unpacks `holes/holeN.yaml` into `golfcart_geofence/config/`, and installs a `golfcart-course-loader.service` systemd unit. Verified: node loads a sample course (2 holes, 2 forbidden zones); full workspace build + 66 tests pass.

### Phase 5b — Schema-validated course format (IMPLEMENTED 2026-09-09)
12. **Course format v2** — JSON Schemas in `tools/map_editor/schema/`
    (`course.schema.json` + `hole.schema.json`), validated by
    `map_editor/validator.py`. Hole-level properties (par, handicap,
    per-tee `distances`) moved onto the hole; features are pure geometry
    + type (TEE_BOX carries `tee_id`). Origin moved to `course.origin` in
    course.yaml. Consumers updated: `geofence_node` (reads origin from
    `course_file` param, legacy fallback), `course_loader_node` (new
    `course.origin` + `features`, legacy `shapes` fallback). GUI hole-level
    property editors. 21 editor tests + 66 workspace tests pass.

### Phase 5c — Flexible tee taxonomy (IMPLEMENTED 2026-09-09)
13. **Tees are not fixed colors.** `course.yaml` gains `course.tees`: a
    dictionary of tee IDs → `{name, slope?, cr?}` (any ID scheme: colors,
    names, symbols, numbers). Hole `distances` keys and `TEE_BOX` features
    reference these IDs via `tee_id`. `Shape.tee_color` → `Shape.tee_id`;
    `Tee` dataclass added. GUI distance editor uses a tee selector combo
    populated from `course.tees`. 21 editor tests + 66 workspace tests pass.



### Phase 6 — Verification
12. Unit tests: OSM parsing, shape editing ops, export Zip structure, lat/lon↔map-frame round-trip. Manual: search a real course, edit, export, load into geofence/web.



## Relevant files
- `tools/hole_polygon_drawer.html` — existing Leaflet polygon tool; reference for GeoJSON/lat-lon export.

- `src/golfcart_msgs/msg/CourseMap.msg`, `CourseFeature.msg` — target semantic model (types TEE_BOX/HOLE/EXIT_POINT/GREEN/FAIRWAY/HAZARD).
- `src/golfcart_geofence/config/hole5.yaml` — existing on-disk course format to mirror.

- `src/golfcart_navigation/src/georeference_node.cpp` — origin convention + latlon_to_map().

- `src/golfcart_navigation/config/global_costmap.yaml` — static_layer costmap integration point.

- `src/golfcart_teleop/web/index.html`, `summon.html` — Leaflet web app; export GeoJSON renders here.



## Verification
1. `python -m pytest tools/map_editor/tests` — unit tests pass.

2. Launch editor, search "Augusta National", select, confirm OSM shapes render over satellite.
3. Edit a green's vertices, add a water hazard, delete a path; save.
4. Load a slope costmap `.pgm/.yaml` and associate with hole 5.
5. Export Zip; unzip; validate `course.yaml` + `holes/hole5.yaml` structure; copy `holes/hole5.yaml` into `golfcart_geofence/config/` and run `geofence.launch.py config_file:=hole5.yaml` — confirm the node loads it (boundary ≥3 pts, origin, hole_number)and reports "Loaded geofence"; render `holes/geojson/hole5.geojson` in the web app.



## Decisions
- **Stack:** Python + PySide6 (Qt) desktop app. Confirmed by user ("Python is fine", "frontend with a Python UI if rendering libs available" — Qt provides QGraphicsView rendering).

- **Export:** Zip bundlewith course metadata + per-hole folders (number, par, distances, handicap) + costmaps + GeoJSON. Confirmed by user.

- **OSM scope:** everything within the course bbox. Confirmed by user.

- **Costmap:** load & associate slope-derived costmap per hole (generation out of scope). Confirmed by user.



- **Included:** search/select, OSM download, satellite render w/ transparency, shape edit(add/delete/edit, forbidden zones, costmap association, Zip export.



- **Excluded (deferred):** ROS `CourseMap` publishing, Nav2 `static_layer` map_server wiring, HMI map rendering, slope costmap generation, camera semantic labeling.



## Confirmed decisions (2026-09-08)
- **Satellite tiles:** Esri World Imagery(free, no key) + local cache. CONFIRMED.
- **Web consumption:** export per-hole GeoJSON for the Leaflet web app. CONFIRMED.
- **HMI course display:** future feature, out of scope. CONFIRMED.
- **CourseMap publishing:** deferred follow-up(per recommendation). CONFIRMED.
- **Export integration depth:** deferred(editor stays self-contained; no ROS publishing / Nav2 static_layer wiring now). CONFIRMED.
- **Export filename:** `golfcart-{course-slug}-{osm-id}-{YYYYMMDD}.zip`(date-only; OS handles same-day collisions). Unique key = OSM id. CONFIRMED.