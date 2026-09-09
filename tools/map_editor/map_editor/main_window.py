"""Main window for the golf course map editor."""

from __future__ import annotations

from pathlib import Path
from typing import List, Optional

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSlider,
    QSpinBox,
    QSplitter,
    QToolBar,
    QVBoxLayout,
    QWidget,
)

from map_editor.canvas import MapCanvas
from map_editor.exporter import export_course
from map_editor.model import Course, Hole, Shape, SHAPE_TYPES
from map_editor.osm_client import CourseSearchResult, download_course, search_courses
from map_editor.satellite import SatelliteLayer


class SearchDialog(QDialog):
    """Search for a golf course and pick one."""

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Search golf course")
        self.resize(500, 400)
        self.result: Optional[CourseSearchResult] = None

        layout = QVBoxLayout(self)
        row = QHBoxLayout()
        self.query_edit = QLineEdit()
        self.query_edit.setPlaceholderText("Course name or location (e.g. Augusta National)")
        self.search_btn = QPushButton("Search")
        row.addWidget(self.query_edit)
        row.addWidget(self.search_btn)
        layout.addLayout(row)

        self.list_widget = QListWidget()
        layout.addWidget(self.list_widget)

        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.button(QDialogButtonBox.Ok).setEnabled(False)
        layout.addWidget(buttons)

        self.search_btn.clicked.connect(self._do_search)
        self.query_edit.returnPressed.connect(self._do_search)
        buttons.accepted.connect(self._accept)
        buttons.rejected.connect(self.reject)
        self.list_widget.itemDoubleClicked.connect(lambda _: self._accept())

    def _do_search(self) -> None:
        query = self.query_edit.text().strip()
        if not query:
            return
        self.list_widget.clear()
        self.search_btn.setEnabled(False)
        self.search_btn.setText("Searching…")
        try:
            results = search_courses(query)
            for r in results:
                item = QListWidgetItem(f"{r.name}  ({r.osm_type} {r.osm_id})")
                item.setData(Qt.UserRole, r)
                self.list_widget.addItem(item)
        except Exception as e:
            QMessageBox.warning(self, "Search failed", str(e))
        finally:
            self.search_btn.setEnabled(True)
            self.search_btn.setText("Search")

    def _accept(self) -> None:
        item = self.list_widget.currentItem()
        if item:
            self.result = item.data(Qt.UserRole)
            self.accept()


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Golf Course Map Editor")
        self.resize(1200, 800)

        self.satellite = SatelliteLayer()
        self.course: Optional[Course] = None
        self.current_hole: Optional[Hole] = None

        self.canvas = MapCanvas(self.satellite)
        self.canvas.selection_changed.connect(self._on_selection_changed)
        self.canvas.shapes_changed.connect(self._mark_dirty)

        # --- Right panel ---
        right = QWidget()
        rl = QVBoxLayout(right)

        self.hole_combo = QComboBox()
        self.hole_combo.currentIndexChanged.connect(self._on_hole_changed)
        rl.addWidget(QLabel("Hole:"))
        rl.addWidget(self.hole_combo)

        self.type_combo = QComboBox()
        self.type_combo.addItems(SHAPE_TYPES)
        rl.addWidget(QLabel("Shape type:"))
        rl.addWidget(self.type_combo)

        self.label_edit = QLineEdit()
        rl.addWidget(QLabel("Label:"))
        rl.addWidget(self.label_edit)

        # --- Hole-level properties (par, handicap, distance) ---
        rl.addWidget(QLabel("Hole properties:"))
        self.par_spin = QSpinBox()
        self.par_spin.setRange(0, 10)
        self.par_spin.valueChanged.connect(self._on_hole_par_changed)
        rl.addWidget(QLabel("Par:"))
        rl.addWidget(self.par_spin)

        self.handicap_spin = QSpinBox()
        self.handicap_spin.setRange(0, 36)
        self.handicap_spin.valueChanged.connect(self._on_hole_handicap_changed)
        rl.addWidget(QLabel("Handicap:"))
        rl.addWidget(self.handicap_spin)

        self.tee_combo = QComboBox()
        self.tee_combo.currentIndexChanged.connect(self._on_tee_changed)
        rl.addWidget(QLabel("Tee:"))
        rl.addWidget(self.tee_combo)

        self.dist_spin = QSpinBox()
        self.dist_spin.setRange(0, 1000)
        self.dist_spin.setSuffix(" m")
        self.dist_spin.valueChanged.connect(self._on_hole_distance_changed)
        rl.addWidget(QLabel("Distance:"))
        rl.addWidget(self.dist_spin)

        # --- Drawing tools ---
        rl.addWidget(QLabel("Tools:"))
        self.draw_btn = QPushButton("Draw shape")
        self.draw_btn.setCheckable(True)
        self.boundary_btn = QPushButton("Draw boundary")
        self.boundary_btn.setCheckable(True)
        self.select_btn = QPushButton("Select")
        self.select_btn.setCheckable(True)
        self.select_btn.setChecked(True)
        draw_row = QHBoxLayout()
        draw_row.addWidget(self.draw_btn)
        draw_row.addWidget(self.boundary_btn)
        draw_row.addWidget(self.select_btn)
        rl.addLayout(draw_row)

        btn_row = QHBoxLayout()
        self.del_btn = QPushButton("Delete shape")
        self.add_vertex_btn = QPushButton("+ vertex")
        self.del_vertex_btn = QPushButton("- vertex")
        btn_row.addWidget(self.del_btn)
        btn_row.addWidget(self.add_vertex_btn)
        btn_row.addWidget(self.del_vertex_btn)
        rl.addLayout(btn_row)

        # --- Move selected shape to another hole ---
        self.move_hole_combo = QComboBox()
        self.move_hole_combo.setEnabled(False)
        rl.addWidget(QLabel("Move selected to hole:"))
        rl.addWidget(self.move_hole_combo)

        self.costmap_btn = QPushButton("Associate costmap…")
        rl.addWidget(self.costmap_btn)

        self.dem_btn = QPushButton("Generate slope costmap from DEM…")
        rl.addWidget(self.dem_btn)

        rl.addStretch()

        self.draw_btn.clicked.connect(lambda: self._set_tool("draw"))
        self.boundary_btn.clicked.connect(lambda: self._set_tool("boundary"))
        self.select_btn.clicked.connect(lambda: self._set_tool("select"))
        self.del_btn.clicked.connect(self._delete_shape)
        self.add_vertex_btn.clicked.connect(self._add_vertex)
        self.del_vertex_btn.clicked.connect(self._delete_vertex)
        self.move_hole_combo.currentIndexChanged.connect(self._move_selected_to_hole)
        self.costmap_btn.clicked.connect(self._associate_costmap)
        self.dem_btn.clicked.connect(self._generate_slope_costmap)

        # --- Opacity slider ---
        rl.addWidget(QLabel("Satellite opacity:"))
        self.opacity_slider = QSlider(Qt.Horizontal)
        self.opacity_slider.setRange(0,  100)
        self.opacity_slider.setValue(100)
        self.opacity_slider.valueChanged.connect(self._on_opacity)
        rl.addWidget(self.opacity_slider)

        splitter = QSplitter(Qt.Horizontal)
        splitter.addWidget(self.canvas)
        splitter.addWidget(right)
        splitter.setStretchFactor(0, 1)
        self.setCentralWidget(splitter)

        self._build_toolbar()
        self._mark_dirty()

    def _build_toolbar(self) -> None:
        tb = QToolBar("Main")
        self.addToolBar(tb)
        self.open_btn = QPushButton("Open course…")
        self.save_btn = QPushButton("Save…")
        self.load_btn = QPushButton("Load…")
        self.export_btn = QPushButton("Export Zip…")
        self.export_btn.setEnabled(False)
        self.save_btn.setEnabled(False)
        tb.addWidget(self.open_btn)
        tb.addWidget(self.save_btn)
        tb.addWidget(self.load_btn)
        tb.addWidget(self.export_btn)
        self.open_btn.clicked.connect(self._open_course)
        self.save_btn.clicked.connect(self._save)
        self.load_btn.clicked.connect(self._load)
        self.export_btn.clicked.connect(self._export)

    # ------------------------------------------------------------------
    # Course / hole management
    # ------------------------------------------------------------------

    def _open_course(self) -> None:
        dlg = SearchDialog(self)
        if dlg.exec() != QDialog.Accepted or dlg.result is None:
            return
        try:
            self.course = download_course(dlg.result)
            self._populate_holes()
            self.canvas.set_view(self.course.origin.longitude_deg, self.course.origin.latitude_deg, 16)
            self._mark_dirty()
        except Exception as e:
            QMessageBox.critical(self, "Download failed", str(e))

    def _populate_holes(self) -> None:
        self.hole_combo.blockSignals(True)
        self.move_hole_combo.blockSignals(True)
        self.hole_combo.clear()
        self.move_hole_combo.clear()
        if not self.course.holes:
            # Default: one hole holding all shapes.

            self.course.holes = [Hole(number=1, shapes=self._all_shapes())]
        for h in self.course.holes:
            self.hole_combo.addItem(f"Hole {h.number}", h)
            self.move_hole_combo.addItem(f"Hole {h.number}", h)
        self.hole_combo.blockSignals(False)
        self.move_hole_combo.blockSignals(False)
        if self.course.holes:
            self._on_hole_changed(0)

    def _all_shapes(self) -> List[Shape]:
        out = []
        for h in self.course.holes if self.course else []:
            out.extend(h.shapes)
        return out

    def _on_hole_changed(self, _index: int) -> None:
        if not self.course or self.hole_combo.count() == 0:
            self.current_hole = None
            self.canvas.set_hole(None)
            return
        self.current_hole = self.hole_combo.currentData()
        self.canvas.set_hole(self.current_hole)
        self.export_btn.setEnabled(True)
        self.save_btn.setEnabled(True)
        self._refresh_hole_props()

    def _refresh_hole_props(self) -> None:
        """Populate the hole-level property editors from the current hole."""
        if self.current_hole is None:
            return
        # Populate the tee selector from the course tee taxonomy.
        self.tee_combo.blockSignals(True)
        self.tee_combo.clear()
        tees = self.course.tees if self.course else {}
        for tid, tee in tees.items():
            self.tee_combo.addItem(tee.name or tid, tid)
        self.tee_combo.blockSignals(False)

        self.par_spin.blockSignals(True)
        self.handicap_spin.blockSignals(True)
        self.dist_spin.blockSignals(True)
        self.par_spin.setValue(self.current_hole.par)
        self.handicap_spin.setValue(self.current_hole.handicap)
        self._refresh_distance_spin()
        self.par_spin.blockSignals(False)
        self.handicap_spin.blockSignals(False)
        self.dist_spin.blockSignals(False)

    def _current_tee_id(self) -> Optional[str]:
        """The tee ID currently selected in the tee combo (or None)."""
        if self.tee_combo.count() == 0:
            return None
        return self.tee_combo.currentData()

    def _refresh_distance_spin(self) -> None:
        """Set the distance spin from the current hole + selected tee."""
        if self.current_hole is None:
            return
        tid = self._current_tee_id()
        self.dist_spin.setValue(int(self.current_hole.distances.get(tid, 0)) if tid else 0)

    def _on_tee_changed(self, _index: int) -> None:
        self._refresh_distance_spin()

    def _on_hole_par_changed(self, value: int) -> None:
        if self.current_hole is not None:
            self.current_hole.par = value

    def _on_hole_handicap_changed(self, value: int) -> None:
        if self.current_hole is not None:
            self.current_hole.handicap = value

    def _on_hole_distance_changed(self, value: int) -> None:
        if self.current_hole is None:
            return
        tid = self._current_tee_id()
        if tid:
            self.current_hole.distances[tid] = float(value)

    # ------------------------------------------------------------------
    # Editing
    # ------------------------------------------------------------------

    def _on_selection_changed(self, shape: Optional[Shape]) -> None:
        if shape is None:
            self.label_edit.clear()
            return
        idx = self.type_combo.findText(shape.type)
        if idx >= 0:
            self.type_combo.setCurrentIndex(idx)
        self.label_edit.setText(shape.label)

    def _on_opacity(self, value: int) -> None:
        self.satellite.set_opacity(value / 100.0)
        self.canvas.viewport().update()

    def _mark_dirty(self) -> None:
        self.setWindowTitle("Golf Course Map Editor" + (" *" if self.course else ""))

    # ------------------------------------------------------------------
    # Actions
    # ------------------------------------------------------------------

    def _set_tool(self, tool: str) -> None:
        """Switch the canvas interaction tool."""
        self.draw_btn.setChecked(tool == "draw")
        self.boundary_btn.setChecked(tool == "boundary")
        self.select_btn.setChecked(tool == "select")
        if tool == "draw":
            self.canvas.set_mode(self.canvas.MODE_DRAW_SHAPE, self.type_combo.currentText())
        elif tool == "boundary":
            self.canvas.set_mode(self.canvas.MODE_DRAW_BOUNDARY)
        else:
            self.canvas.set_mode(self.canvas.MODE_SELECT)

    def _delete_shape(self) -> None:
        self.canvas.delete_selected()

    def _add_vertex(self) -> None:
        self.canvas.add_vertex_to_selected()

    def _delete_vertex(self) -> None:
        self.canvas.delete_vertex_from_selected()

    def _move_selected_to_hole(self, _index: int) -> None:
        """Move the selected shape to the chosen hole."""
        if not self.course or not self.canvas.selected_shape:
            return
        target = self.move_hole_combo.currentData()
        if target is None or target is self.current_hole:
            return
        shape = self.canvas.selected_shape
        # Remove from current hole.
        if self.current_hole and shape in self.current_hole.shapes:
            self.current_hole.shapes.remove(shape)
        # Add to target hole.
        target.shapes.append(shape)
        self.canvas.set_hole(self.current_hole)
        self._mark_dirty()

    def _save(self) -> None:
        """Save the working course to a Zip file (reloadable)."""
        if not self.course:
            return
        default = self.course.export_filename().replace(".zip", "_work.zip")
        path, _ = QFileDialog.getSaveFileName(self, "Save working course", default, "Zip (*.zip)")
        if not path:
            return
        try:
            from map_editor.exporter import export_course
            out = export_course(self.course, Path(path).parent, Path(path).name)
            QMessageBox.information(self, "Saved", f"Working course saved to:\n{out}")
        except Exception as e:
            QMessageBox.critical(self, "Save failed", str(e))

    def _load(self) -> None:
        """Load a previously saved working course Zip."""
        path, _ = QFileDialog.getOpenFileName(self, "Load working course", "", "Zip (*.zip)")
        if not path:
            return
        try:
            from map_editor.loader import load_course
            self.course = load_course(Path(path))
            self._populate_holes()
            self.canvas.set_view(
                self.course.origin.longitude_deg, self.course.origin.latitude_deg, 16
            )
            self._mark_dirty()
        except Exception as e:
            QMessageBox.critical(self, "Load failed", str(e))

    def _associate_costmap(self) -> None:
        if not self.current_hole:
            return
        pgm, _ = QFileDialog.getOpenFileName(self, "Select costmap .pgm", "", "PGM (*.pgm)")
        if not pgm:
            return
        yaml_path = str(Path(pgm).with_suffix(".yaml"))
        if not Path(yaml_path).exists():
            yaml_path, _ = QFileDialog.getOpenFileName(self, "Select costmap .yaml", "", "YAML (*.yaml)")
        self.current_hole.costmap_pgm = pgm
        self.current_hole.costmap_yaml = yaml_path
        self._mark_dirty()

    def _generate_slope_costmap(self) -> None:
        """Generate a slope costmap from DEM for the current hole."""
        if not self.current_hole or not self.course:
            return
        hole = self.current_hole
        if not hole.boundary:
            QMessageBox.warning(self, "No boundary", "Set the hole boundary first.")
            return
        try:
            from map_editor.dem import (
                compute_slope,
                fetch_dem,
                slope_to_cost_values,
                write_costmap,
                Costmap,
            )

            import numpy as np

            lats = [v[0] for v in hole.boundary]
            lons = [v[1] for v in hole.boundary]
            min_lat, max_lat = min(lats), max(lats)
            min_lon, max_lon = min(lons), max(lons)

            # Fetch DEM from the LGL WMS DGM025 (point-by-point GetFeatureInfo).
            spacing_m = 5.0
            z = fetch_dem(min_lat, min_lon, max_lat, max_lon, spacing_m=spacing_m)
            if np.isnan(z).all():
                QMessageBox.warning(
                    self, "No DEM data",
                    "No elevation data returned for this hole's bbox. "
                    "Check that the hole is within Baden-Wuerttemberg.",
                )
                return
            # Fill NaN cells with the grid mean so slope math is stable.
            z_filled = np.where(np.isnan(z), np.nanmean(z), z)
            slope_deg, aspect_deg = compute_slope(z_filled, spacing_m)

            # Store in-memory for heatmap overlay.
            hole.slope_deg = slope_deg
            hole.aspect_deg = aspect_deg

            # Write costmap files next to the course.
            out_dir = Path.home() / ".cache" / "golfcart-map-editor" / "costmaps"
            out_dir.mkdir(parents=True, exist_ok=True)
            pgm = out_dir / f"hole{hole.number}_slope.pgm"
            yml = out_dir / f"hole{hole.number}_slope.yaml"
            cost = slope_to_cost_values(slope_deg)
            cm = Costmap(data=cost, resolution=spacing_m, origin_x=0.0, origin_y=0.0)
            write_costmap(cm, pgm, yml)
            hole.costmap_pgm = str(pgm)
            hole.costmap_yaml = str(yml)

            self.canvas.viewport().update()
            self._mark_dirty()
            QMessageBox.information(
                self,
                "Slope costmap generated",
                f"Slope costmap written for hole {hole.number}.\n"
                f"Grid: {z.shape[0]}x{z.shape[1]} @ {spacing_m} m\n"
                f"Max slope: {float(np.nanmax(slope_deg)):.1f} deg\n"
                f"Files:\n{pgm}\n{yml}",
            )
        except Exception as e:
            QMessageBox.critical(self, "DEM generation failed", str(e))

    def _export(self) -> None:
        if not self.course:
            return
        default = self.course.export_filename()
        path, _ = QFileDialog.getSaveFileName(self, "Export course", default, "Zip (*.zip)")
        if not path:
            return
        try:
            out = export_course(self.course, Path(path).parent, Path(path).name)
            QMessageBox.information(self, "Export complete", f"Exported to:\n{out}")
        except Exception as e:
            QMessageBox.critical(self, "Export failed", str(e))