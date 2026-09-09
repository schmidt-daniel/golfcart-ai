"""Esri World Imagery satellite tile layer with local disk caching.

Tiles are fetched from Esri's free (no API key) World Imagery service and
cached under a local directory so repeated views don't re-download.

"""

from __future__ import annotations

import math
import os
from pathlib import Path
from typing import Optional

import requests
from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QImage, QPainter, QPixmap

ESRI_TILE_URL = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"

#: Web Mercator (EPSG:3857) constants.
R_EARTH = 6378137.0
ORIGIN_SHIFT = 2.0 * math.pi * R_EARTH / 2.0


def lonlat_to_tile(lon: float, lat: float, zoom: int) -> tuple[int, int]:
    """Convert lon/lat to a slippy-map tile (x, y) at the given zoom."""
    lat_rad = math.radians(lat)
    n = 2.0 ** zoom
    x = int((lon + 180.0) / 360.0 * n)
    y = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    return x, y


def tile_to_lonlat(x: int, y: int, zoom: int) -> tuple[float, float]:
    """Top-left corner of a tile in lon/lat."""
    n = 2.0 ** zoom
    lon = x / n * 360.0 - 180.0
    lat_rad = math.atan(math.sinh(math.pi * (1.0 - 2.0 * y / n)))
    return lon, math.degrees(lat_rad)


class TileCache:
    """Disk-backed cache for satellite tiles."""

    def __init__(self, cache_dir: Optional[Path] = None) -> None:
        self.cache_dir = cache_dir or Path.home() / ".cache" / "golfcart-map-editor" / "tiles"
        self.cache_dir.mkdir(parents=True, exist_ok=True)

    def _path(self, z: int, x: int, y: int) -> Path:
        return self.cache_dir / str(z) / str(x) / f"{y}.jpg"

    def get(self, z: int, x: int, y: int) -> Optional[QImage]:
        """Return a cached tile image, or None if not cached."""
        p = self._path(z, x, y)
        if p.exists():
            img = QImage(str(p))
            return img if not img.isNull() else None
        return None

    def put(self, z: int, x: int, y: int, img: QImage) -> None:
        p = self._path(z, x, y)
        p.parent.mkdir(parents=True, exist_ok=True)
        img.save(str(p), "JPG")


class SatelliteLayer:
    """Renders Esri satellite tiles for a lon/lat viewport."""

    def __init__(self, cache: Optional[TileCache] = None) -> None:
        self.cache = cache or TileCache()
        self.opacity: float = 1.0
        self._session = requests.Session()
        self._session.headers["User-Agent"] = "golfcart-map-editor/0.1"

    def set_opacity(self, opacity: float) -> None:
        self.opacity = max(0.0, min(1.0, opacity))

    def draw(self, painter: QPainter, viewport: QRectF, zoom: int) -> None:
        """Draw the satellite imagery covering `viewport` (in lon/lat)."""
        if self.opacity <= 0.0:
            return
        min_lon = viewport.left()
        max_lon = viewport.right()
        min_lat = viewport.bottom()
        max_lat = viewport.top()

        x0, y0 = lonlat_to_tile(min_lon, max_lat, zoom)
        x1, y1 = lonlat_to_tile(max_lon, min_lat, zoom)
        x0, x1 = sorted((x0, x1))
        y0, y1 = sorted((y0, y1))

        painter.save()
        painter.setOpacity(self.opacity)


        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                img = self.cache.get(zoom, x, y)
                if img is None:
                    img = self._fetch(zoom, x, y)
                    if img is None:
                        continue
                    self.cache.put(zoom, x, y, img)
                # Tile top-left in lon/lat.

                lon0, lat1 = tile_to_lonlat(x, y, zoom)
                lon1, lat0 = tile_to_lonlat(x + 1, y + 1, zoom)
                target = QRectF(lon0, lat0, lon1 - lon0, lat1 - lat0)
                painter.drawImage(target, img)
        painter.restore()

    def _fetch(self, z: int, x: int, y: int) -> Optional[QImage]:
        url = ESRI_TILE_URL.format(z=z, x=x, y=y)
        try:
            resp = self._session.get(url, timeout=15)
            resp.raise_for_status()
            img = QImage()
            if img.loadFromData(resp.content):
                return img
        except requests.RequestException:
            pass
        return None