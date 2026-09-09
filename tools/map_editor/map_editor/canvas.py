"""Interactive map canvas for the golf course map editor.

A QGraphicsView-based canvas that renders the satellite base layer and the
editable OSM shapes (polygons, lines, points). Supports pan/zoom,
select/move vertices, add/delete vertices, add/delete shapes, and snapping.
"""

from __future__ import annotations

from typing import List, Optional, Tuple

from PySide6.QtCore import QPointF, QRectF, Qt, Signal
from PySide6.QtGui import QColor, QPainter, QPen, QPolygonF
from PySide6.QtWidgets import QGraphicsView, QGraphicsScene

from map_editor.model import Hole, Shape
from map_editor.satellite import SatelliteLayer

MIN_ZOOM = 14
MAX_ZOOM = 20

TYPE_COLORS = {
    "GREEN": QColor(46, 204, 113, 140),
    "TEE_BOX": QColor(241, 196, 15, 140),
    "HOLE": QColor(0, 0, 0, 200),
    "FAIRWAY": QColor(52, 152, 219, 120),
    "HAZARD": QColor(231, 76, 60, 140),
    "WATER_HAZARD": QColor(41, 128, 185, 160),
    "BUNKER": QColor(243, 156, 18, 140),
    "ROUGH": QColor(133, 193, 95, 120),
    "PATH": QColor(149, 165, 166, 200),
    "FORBIDDEN_ZONE": QColor(231, 76, 60, 160),
    "EXIT_POINT": QColor(155, 89, 182, 200),
}
DEFAULT_COLOR = QColor(231, 76, 60, 140)


def shape_color(stype: str) -> QColor:
    return TYPE_COLORS.get(stype, DEFAULT_COLOR)


class MapCanvas(QGraphicsView):
    """The main editable map view."""

    selection_changed = Signal(object)
    shapes_changed = Signal()

    #: Drawing modes.
    MODE_SELECT = "select"
    MODE_DRAW_SHAPE = "draw_shape"
    MODE_DRAW_BOUNDARY = "draw_boundary"

    def __init__(self, satellite: SatelliteLayer, parent=None) -> None:
        super().__init__(parent)
        self.satellite = satellite
        self._scene = QGraphicsScene(self)
        self.setScene(self._scene)
        self.setRenderHint(QPainter.Antialiasing)
        self.setDragMode(QGraphicsView.ScrollHandDrag)
        self.setTransformationAnchor(QGraphicsView.AnchorUnderMouse)
        self.setResizeAnchor(QGraphicsView.AnchorViewCenter)

        self.zoom = MIN_ZOOM
        self.center_lonlat: Tuple[float, float] = (0.0, 0.0)
        self.hole: Optional[Hole] = None
        self.selected_shape: Optional[Shape] = None
        self.selected_vertex: Optional[int] = None
        self._drag_vertex: Optional[int] = None
        self._snap_radius_px = 12.0

        #: Current interaction mode.
        self.mode = self.MODE_SELECT
        #: Shape type to create when in draw_shape mode.
        self.draw_type = "FORBIDDEN_ZONE"
        #: Vertices collected while drawing (lat, lon).
        self._draw_pts: List[Tuple[float, float]] = []

        self._scene.setSceneRect(QRectF(-180.0, -90.0, 360.0, 180.0))

    # ------------------------------------------------------------------
    # Mode control
    # ------------------------------------------------------------------

    def set_mode(self, mode: str, draw_type: Optional[str] = None) -> None:
        """Switch interaction mode (select / draw_shape / draw_boundary)."""
        self.mode = mode
        if draw_type is not None:
            self.draw_type = draw_type
        self._draw_pts = []
        self._drag_vertex = None
        self._scene.update()

    def cancel_draw(self) -> None:
        """Cancel any in-progress drawing."""
        self._draw_pts = []
        self._scene.update()

    def _lonlat_to_scene(self, lon: float, lat: float) -> QPointF:
        return QPointF(lon, -lat)

    def _scene_to_lonlat(self, p: QPointF) -> Tuple[float, float]:
        return p.x(), -p.y()

    def _scene_to_view(self, p: QPointF) -> QPointF:
        return self.mapFromScene(p)

    def _view_to_scene(self, p: QPointF) -> QPointF:
        return self.mapToScene(p.toPoint())

    def set_view(self, center_lonlat: Tuple[float, float], zoom: int) -> None:
        self.center_lonlat = center_lonlat
        self.zoom = max(MIN_ZOOM, min(MAX_ZOOM, zoom))
        self._recenter()

    def _recenter(self) -> None:
        lon, lat = self.center_lonlat
        self.centerOn(self._lonlat_to_scene(lon, lat))
        self._apply_zoom()

    def _apply_zoom(self) -> None:
        px_per_deg = 256.0 * (2.0 ** self.zoom) / 360.0
        self.resetTransform()
        self.scale(px_per_deg, px_per_deg)

    def wheelEvent(self, event) -> None:
        delta = event.angleDelta().y()
        if delta > 0:
            self.zoom = min(MAX_ZOOM, self.zoom + 1)
        else:
            self.zoom = max(MIN_ZOOM, self.zoom - 1)
        self._apply_zoom()
        self.centerOn(self.mapToScene(event.position().toPoint()))
        event.accept()

    def set_hole(self, hole: Optional[Hole]) -> None:
        self.hole = hole
        self.selected_shape = None
        self.selected_vertex = None
        self.selection_changed.emit(None)
        self._scene.update()

    def drawBackground(self, painter: QPainter, rect: QRectF) -> None:
        painter.fillRect(rect, QColor(30, 30, 30))
        viewport_lonlat = QRectF(
            rect.left(), -rect.top(), rect.width(), rect.height()
        )
        self.satellite.draw(painter, viewport_lonlat, self.zoom)
        if self.hole is not None and self.hole.slope_deg is not None:
            self._draw_slope_heatmap(painter)

    def _draw_slope_heatmap(self, painter: QPainter) -> None:
        """Overlay the hole's slope grid as a translucent heatmap."""
        import numpy as np

        slope = np.asarray(self.hole.slope_deg, dtype=np.float64)
        rows, cols = slope.shape
        if rows < 2 or cols < 2:
            return
        # Normalize slope to 0..1 for coloring (cap at 15 deg).
        norm = np.clip(slope / 15.0, 0.0, 1.0)
        painter.save()
        painter.setOpacity(0.45)
        # Draw each cell as a small rect in scene coords. The grid is assumed
        # to span the hole boundary bbox; approximate cell size from the
        # boundary extent.
        lats = [v[0] for v in self.hole.boundary]
        lons = [v[1] for v in self.hole.boundary]
        if not lats or not lons:
            painter.restore()
            return
        min_lat, max_lat = min(lats), max(lats)
        min_lon, max_lon = min(lons), max(lons)
        dlat = (max_lat - min_lat) / rows
        dlon = (max_lon - min_lon) / cols
        for r in range(rows):
            for c in range(cols):
                v = norm[r, c]
                if v <= 0.01:
                    continue
                # Green (low) -> yellow -> red (high).
                r8 = int(255 * v)
                g8 = int(255 * (1.0 - v))
                color = QColor(r8, g8, 0, 200)
                painter.setBrush(color)
                painter.setPen(QPen(color, 0))
                lat0 = max_lat - (r + 1) * dlat
                lon0 = min_lon + c * dlon
                p = self._lonlat_to_scene(lon0, lat0)
                painter.drawRect(QRectF(p.x(), p.y(), dlon, dlat))
        painter.restore()

    def drawForeground(self, painter: QPainter, rect: QRectF) -> None:
        if not self.hole:
            return
        for shape in self.hole.shapes:
            self._draw_shape(painter, shape)
        if self.selected_shape:
            self._draw_vertices(painter, self.selected_shape)
        if self._draw_pts:
            self._draw_in_progress(painter)

    def _draw_shape(self, painter: QPainter, shape: Shape) -> None:
        color = shape_color(shape.type)
        pen = QPen(color.darker(120), 2.0)
        painter.setPen(pen)
        painter.setBrush(color)
        pts = [self._lonlat_to_scene(lon, lat) for lat, lon in shape.vertices]
        if shape.is_point and pts:
            painter.setBrush(color.darker(150))
            painter.drawEllipse(pts[0], 6.0, 6.0)
        elif len(pts) >= 2 and shape.type == "PATH":
            painter.drawPolyline(QPolygonF(pts))
        elif len(pts) >= 3:
            painter.drawPolygon(QPolygonF(pts))

    def _draw_vertices(self, painter: QPainter, shape: Shape) -> None:
        if not shape.vertices:
            return
        painter.setPen(QPen(QColor(255, 255, 255), 1.5))
        painter.setBrush(QColor(255, 0, 0))
        for i, (lat, lon) in enumerate(shape.vertices):
            p = self._lonlat_to_scene(lon, lat)
            painter.drawEllipse(p, 4.0, 4.0)
            if i == self.selected_vertex:
                painter.setBrush(QColor(0, 255, 0))
                painter.drawEllipse(p, 6.0, 6.0)
                painter.setBrush(QColor(255, 0, 0))

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.LeftButton:
            scene_pos = self._view_to_scene(event.position().toPoint())
            lon, lat = self._scene_to_lonlat(scene_pos)
            if self.mode == self.MODE_DRAW_SHAPE:
                self._add_draw_point(lat, lon)
                event.accept()
                return
            if self.mode == self.MODE_DRAW_BOUNDARY:
                self._add_boundary_point(lat, lon)
                event.accept()
                return
            # Select mode.
            if self.selected_shape:
                vi = self._hit_vertex(self.selected_shape, scene_pos)
                if vi is not None:
                    self.selected_vertex = vi
                    self._drag_vertex = vi
                    self.selection_changed.emit(self.selected_shape)
                    self._scene.update()
                    event.accept()
                    return
            hit = self._hit_shape(scene_pos)
            self.selected_shape = hit
            self.selected_vertex = None
            self.selection_changed.emit(hit)
            self._scene.update()
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event) -> None:
        if self._drag_vertex is not None and self.selected_shape:
            scene_pos = self._view_to_scene(event.position().toPoint())
            lon, lat = self._scene_to_lonlat(scene_pos)
            self.selected_shape.vertices[self._drag_vertex] = (lat, lon)
            self.shapes_changed.emit()
            self._scene.update()
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event) -> None:
        self._drag_vertex = None
        super().mouseReleaseEvent(event)

    def mouseDoubleClickEvent(self, event) -> None:
        """Finish a polygon/line on double-click; a point on single click."""
        if self.mode in (self.MODE_DRAW_SHAPE, self.MODE_DRAW_BOUNDARY):
            self._finish_draw()
            event.accept()
            return
        super().mouseDoubleClickEvent(event)

    # ------------------------------------------------------------------
    # Drawing
    # ------------------------------------------------------------------

    def _add_draw_point(self, lat: float, lon: float) -> None:
        """Add a vertex while drawing a shape (snapped to nearby vertices)."""
        lat, lon = self._snap_point(lat, lon)
        self._draw_pts.append((lat, lon))
        self._scene.update()

    def _add_boundary_point(self, lat: float, lon: float) -> None:
        """Add a vertex while drawing the hole boundary (snapped)."""
        lat, lon = self._snap_point(lat, lon)
        self._draw_pts.append((lat, lon))
        self._scene.update()

    def _snap_point(self, lat: float, lon: float) -> Tuple[float, float]:
        """Snap a point to a nearby existing vertex (within snap radius)."""
        if not self.hole:
            return lat, lon
        p = self._lonlat_to_scene(lon, lat)
        vp = self._scene_to_view(p)
        best_d = self._snap_radius_px
        best = None
        for shape in self.hole.shapes:
            for slat, slon in shape.vertices:
                sp = self._scene_to_view(self._lonlat_to_scene(slon, slat))
                d = (sp - vp).manhattanLength()
                if d < best_d:
                    best_d = d
                    best = (slat, slon)
        return best if best is not None else (lat, lon)

    def _finish_draw(self) -> None:
        """Finalize the in-progress drawing into a shape or boundary."""
        if not self.hole:
            self._draw_pts = []
            self._scene.update()
            return
        pts = self._draw_pts
        self._draw_pts = []
        if self.mode == self.MODE_DRAW_BOUNDARY:
            if len(pts) >= 3:
                self.hole.boundary = list(pts)
                self.shapes_changed.emit()
            self._scene.update()
            return
        # Draw shape.
        if not pts:
            self._scene.update()
            return
        if self.draw_type in ("HOLE", "EXIT_POINT"):
            shape = Shape(type=self.draw_type, vertices=[pts[0]])
        elif self.draw_type == "PATH":
            shape = Shape(type=self.draw_type, vertices=pts)
        else:
            shape = Shape(type=self.draw_type, vertices=pts)
        self.add_shape(shape)

    def _draw_in_progress(self, painter: QPainter) -> None:
        """Draw the in-progress polygon/line being drawn."""
        if not self._draw_pts:
            return
        pts = [self._lonlat_to_scene(lon, lat) for lat, lon in self._draw_pts]
        color = QColor(0, 200, 255, 180)
        painter.setPen(QPen(color, 2.0))
        painter.setBrush(color)
        if len(pts) == 1:
            painter.drawEllipse(pts[0], 5.0, 5.0)
        elif len(pts) >= 2:
            painter.drawPolyline(QPolygonF(pts))

    def _hit_vertex(self, shape: Shape, scene_pos: QPointF) -> Optional[int]:
        best = None
        best_d = self._snap_radius_px
        for i, (lat, lon) in enumerate(shape.vertices):
            p = self._lonlat_to_scene(lon, lat)
            vp = self._scene_to_view(p)
            sp = self._scene_to_view(scene_pos)
            d = (vp - sp).manhattanLength()
            if d < best_d:
                best_d = d
                best = i
        return best

    def _hit_shape(self, scene_pos: QPointF) -> Optional[Shape]:
        if not self.hole:
            return None
        for shape in reversed(self.hole.shapes):
            if shape.is_point and shape.vertices:
                p = self._lonlat_to_scene(shape.vertices[0][1], shape.vertices[0][0])
                if (self._scene_to_view(p) - self._scene_to_view(scene_pos)).manhattanLength() < 12.0:
                    return shape
        for shape in reversed(self.hole.shapes):
            if len(shape.vertices) >= 3 and not shape.is_point:
                poly = QPolygonF([self._lonlat_to_scene(lon, lat) for lat, lon in shape.vertices])
                if poly.containsPoint(scene_pos, Qt.OddEvenFill):
                    return shape
        return None

    def add_shape(self, shape: Shape) -> None:
        if not self.hole:
            return
        self.hole.shapes.append(shape)
        self.selected_shape = shape
        self.selected_vertex = None
        self.selection_changed.emit(shape)
        self.shapes_changed.emit()
        self._scene.update()

    def delete_selected(self) -> None:
        if not self.hole or self.selected_shape is None:
            return
        self.hole.shapes.remove(self.selected_shape)
        self.selected_shape = None
        self.selected_vertex = None
        self.selection_changed.emit(None)
        self.shapes_changed.emit()
        self._scene.update()

    def add_vertex_to_selected(self) -> None:
        if not self.selected_shape or len(self.selected_shape.vertices) < 2:
            return
        v = self.selected_shape.vertices
        mid = ((v[-1][0] + v[0][0]) / 2.0, (v[-1][1] + v[0][1]) / 2.0)
        v.append(mid)
        self.shapes_changed.emit()
        self._scene.update()

    def delete_vertex_from_selected(self) -> None:
        if not self.selected_shape or len(self.selected_shape.vertices) <= 2:
            return
        if self.selected_vertex is not None:
            del self.selected_shape.vertices[self.selected_vertex]
            self.selected_vertex = None
        else:
            self.selected_shape.vertices.pop()
        self.shapes_changed.emit()
        self._scene.update()
