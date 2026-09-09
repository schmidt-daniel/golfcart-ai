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

        self.par_spin = QSpinBox()
        self.par_spin.setRange(0, 10)
        rl.addWidget(QLabel("Par:"))
        rl.addWidget(self.par_spin)

        self.dist_spin = QSpinBox()
        self.dist_spin.setRange(0, 1000)
        self.dist_spin.setSuffix(" m")
        rl.addWidget(QLabel("Distance:"))
        rl.addWidget(self.dist_spin)

        self.handicap_spin = QSpinBox()
        self.handicap_spin.setRange(0, 36)
        rl.addWidget(QLabel("Handicap:"))
        rl.addWidget(self.handicap_spin)

        btn_row = QHBoxLayout()
        self.add_btn = QPushButton("Add shape")
        self.del_btn = QPushButton("Delete shape")
        self.add_vertex_btn = QPushButton("+ vertex")
        self.del_vertex_btn = QPushButton("- vertex")
        btn_row.addWidget(self.add_btn)
        btn_row.addWidget(self.del_btn)
        rl.addLayout(btn_row)
        btn_row2 = QHBoxLayout()
        btn_row2.addWidget(self.add_vertex_btn)
        btn_row2.addWidget(self.del_vertex_btn)
        rl.addLayout(btn_row2)

        self.costmap_btn = QPushButton("Associate costmap…")
        rl.addWidget(self.costmap_btn)

        rl.addStretch()

        self.add_btn.clicked.connect(self._add_shape)
        self.del_btn.clicked.connect(self._delete_shape)
        self.add_vertex_btn.clicked.connect(self._add_vertex)
        self.del_vertex_btn.clicked.connect(self._delete_vertex)
        self.costmap_btn.clicked.connect(self._associate_costmap)

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
        self.export_btn = QPushButton("Export Zip…")
        self.export_btn.setEnabled(False)
        tb.addWidget(self.open_btn)
        tb.addWidget(self.export_btn)
        self.open_btn.clicked.connect(self._open_course)
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
        self.hole_combo.clear()
        if not self.course.holes:
            # Default: one hole holding all shapes.

            self.course.holes = [Hole(number=1, shapes=self._all_shapes())]
        for h in self.course.holes:
            self.hole_combo.addItem(f"Hole {h.number}", h)
        self.hole_combo.blockSignals(False)
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

    # ------------------------------------------------------------------
    # Editing
    # ------------------------------------------------------------------

    def _on_selection_changed(self, shape: Optional[Shape]) -> None:
        if shape is None:
            self.label_edit.clear()
            self.par_spin.setValue(0)
            self.dist_spin.setValue(0)
            self.handicap_spin.setValue(0)
            return
        idx = self.type_combo.findText(shape.type)
        if idx >= 0:
            self.type_combo.setCurrentIndex(idx)
        self.label_edit.setText(shape.label)
        self.par_spin.setValue(shape.par)
        self.dist_spin.setValue(shape.distance_m)
        self.handicap_spin.setValue(shape.handicap)

    def _on_opacity(self, value: int) -> None:
        self.satellite.set_opacity(value / 100.0)
        self.canvas.viewport().update()

    def _mark_dirty(self) -> None:
        self.setWindowTitle("Golf Course Map Editor" + (" *" if self.course else ""))

    # ------------------------------------------------------------------
    # Actions
    # ------------------------------------------------------------------

    def _add_shape(self) -> None:
        if not self.current_hole:
            return
        shape = Shape(
            type=self.type_combo.currentText(),
            label=self.label_edit.text(),
            par=self.par_spin.value(),
            distance_m=self.dist_spin.value(),
            handicap=self.handicap_spin.value(),
            vertices=[(self.course.origin.latitude_deg, self.course.origin.longitude_deg)],
        )
        self.canvas.add_shape(shape)

    def _delete_shape(self) -> None:
        self.canvas.delete_selected()

    def _add_vertex(self) -> None:
        self.canvas.add_vertex_to_selected()

    def _delete_vertex(self) -> None:
        self.canvas.delete_vertex_from_selected()

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