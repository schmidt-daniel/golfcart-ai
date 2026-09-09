"""Golf course map editor for the golf cart project.

A desktop (PySide6/Qt) editor that:
- searches & selects a golf course on OpenStreetMap,
- downloads its OSM data (greens, tees, water hazards, bunkers, fairways, paths, rough),
- renders it over a transparent Esri satellite view,
- lets you edit shapes and mark forbidden zones,
- associates a slope-derived costmap per hole,
- exports the whole course as a Zip bundle the trolley (Nav2/geofence) and the web app can consume.

Run with:  python -m map_editor
"""

__version__ = "0.1.0"