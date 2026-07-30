from __future__ import annotations

import datetime
import os

os.environ.setdefault("QT_API", "pyside6")

import numpy as np
from PySide6.QtCore import Qt, QThread, Slot
from PySide6.QtGui import QAction
from PySide6.QtWidgets import (
    QComboBox,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QProgressBar,
    QPushButton,
    QSpinBox,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
from matplotlib.figure import Figure

from ..paraview_launcher import (
    find_paraview_candidates,
    get_paraview_path,
    launch_paraview,
    set_paraview_path,
)
from ..solver.scenarios import PRESETS
from .tab_3d import ThreeDPanel
from .worker import SimulationWorker

APP_TITLE = "CFD Studio — 2D Incompressible Flow with ParaView"

STYLESHEET = """
QMainWindow { background: #f3f4f6; }
QGroupBox {
    font-weight: 600;
    border: 1px solid #d0d3d8;
    border-radius: 8px;
    margin-top: 14px;
    background: #ffffff;
    padding-top: 6px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: #2b2f38;
}
QLabel#description { color: #5a5f6a; font-style: italic; }
QPushButton {
    background: #2563eb;
    color: white;
    border-radius: 6px;
    padding: 7px 14px;
    font-weight: 600;
}
QPushButton:hover { background: #1d4ed8; }
QPushButton:disabled { background: #a9b4c4; }
QPushButton#stopButton { background: #dc2626; }
QPushButton#stopButton:hover { background: #b91c1c; }
QPushButton#paraviewButton { background: #059669; }
QPushButton#paraviewButton:hover { background: #047857; }
QProgressBar {
    border: 1px solid #d0d3d8;
    border-radius: 6px;
    text-align: center;
    background: #eef0f3;
    height: 18px;
}
QProgressBar::chunk { background-color: #2563eb; border-radius: 6px; }
QStatusBar { background: #e5e7eb; }
"""


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(APP_TITLE)
        self.resize(1280, 820)
        self.setStyleSheet(STYLESHEET)

        self._thread: QThread | None = None
        self._worker: SimulationWorker | None = None
        self._last_pvd_path: str | None = None

        self._build_menu()
        self._build_ui()
        self._on_scenario_changed()

    # ------------------------------------------------------------------
    def _build_menu(self):
        settings_menu = self.menuBar().addMenu("&Settings")
        locate_action = QAction("Locate ParaView...", self)
        locate_action.triggered.connect(self._locate_paraview)
        settings_menu.addAction(locate_action)

        help_menu = self.menuBar().addMenu("&Help")
        about_action = QAction("About", self)
        about_action.triggered.connect(self._show_about)
        help_menu.addAction(about_action)

    # ------------------------------------------------------------------
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)

        self.tabs = QTabWidget()
        scenarios_tab = QWidget()
        scenarios_layout = QHBoxLayout(scenarios_tab)
        scenarios_layout.addWidget(self._build_left_panel(), 0)
        scenarios_layout.addWidget(self._build_right_panel(), 1)
        self.tabs.addTab(scenarios_tab, "2D Flow Scenarios")

        self.three_d_panel = ThreeDPanel()
        self.tabs.addTab(self.three_d_panel, "3D Custom Geometry")

        root.addWidget(self.tabs)

        self.statusBar().showMessage("Ready.")

    # ------------------------------------------------------------------
    def _build_left_panel(self) -> QWidget:
        panel = QWidget()
        panel.setFixedWidth(360)
        layout = QVBoxLayout(panel)
        layout.setAlignment(Qt.AlignTop)

        case_group = QGroupBox("Case Setup")
        form = QFormLayout(case_group)

        self.scenario_combo = QComboBox()
        for preset in PRESETS.values():
            self.scenario_combo.addItem(preset.label, preset.key)
        self.scenario_combo.currentIndexChanged.connect(self._on_scenario_changed)
        form.addRow("Scenario:", self.scenario_combo)

        self.description_label = QLabel()
        self.description_label.setObjectName("description")
        self.description_label.setWordWrap(True)
        form.addRow(self.description_label)

        self.nx_spin = QSpinBox()
        self.nx_spin.setRange(11, 400)
        self.ny_spin = QSpinBox()
        self.ny_spin.setRange(11, 400)
        grid_row = QHBoxLayout()
        grid_row.addWidget(self.nx_spin)
        grid_row.addWidget(QLabel("x"))
        grid_row.addWidget(self.ny_spin)
        grid_widget = QWidget()
        grid_widget.setLayout(grid_row)
        form.addRow("Grid points (nx x ny):", grid_widget)

        self.re_spin = QDoubleSpinBox()
        self.re_spin.setRange(1.0, 100000.0)
        self.re_spin.setDecimals(0)
        self.re_spin.setSingleStep(50)
        form.addRow("Reynolds number:", self.re_spin)

        self.u_spin = QDoubleSpinBox()
        self.u_spin.setRange(0.01, 50.0)
        self.u_spin.setDecimals(2)
        form.addRow("Lid / inflow speed:", self.u_spin)

        self.obstacle_group = QGroupBox("Obstacle Geometry")
        obs_form = QFormLayout(self.obstacle_group)
        self.obstacle_x0 = QDoubleSpinBox()
        self.obstacle_x0.setRange(0.0, 100.0)
        self.obstacle_x0.setDecimals(2)
        self.obstacle_width = QDoubleSpinBox()
        self.obstacle_width.setRange(0.01, 100.0)
        self.obstacle_width.setDecimals(2)
        self.obstacle_height = QDoubleSpinBox()
        self.obstacle_height.setRange(0.01, 100.0)
        self.obstacle_height.setDecimals(2)
        obs_form.addRow("Position (x0):", self.obstacle_x0)
        obs_form.addRow("Width:", self.obstacle_width)
        obs_form.addRow("Height:", self.obstacle_height)

        layout.addWidget(case_group)
        layout.addWidget(self.obstacle_group)

        run_group = QGroupBox("Run")
        run_form = QFormLayout(run_group)

        self.steps_spin = QSpinBox()
        self.steps_spin.setRange(50, 500000)
        self.steps_spin.setValue(3000)
        self.steps_spin.setSingleStep(500)
        run_form.addRow("Number of steps:", self.steps_spin)

        self.output_every_spin = QSpinBox()
        self.output_every_spin.setRange(1, 5000)
        self.output_every_spin.setValue(20)
        run_form.addRow("Write output every N steps:", self.output_every_spin)

        out_row = QHBoxLayout()
        self.output_dir_edit = QLineEdit()
        browse_btn = QPushButton("Browse...")
        browse_btn.clicked.connect(self._browse_output_dir)
        out_row.addWidget(self.output_dir_edit)
        out_row.addWidget(browse_btn)
        out_widget = QWidget()
        out_widget.setLayout(out_row)
        run_form.addRow("Output folder:", out_widget)

        btn_row = QHBoxLayout()
        self.run_button = QPushButton("Run Simulation")
        self.run_button.clicked.connect(self._start_simulation)
        self.stop_button = QPushButton("Stop")
        self.stop_button.setObjectName("stopButton")
        self.stop_button.clicked.connect(self._stop_simulation)
        self.stop_button.setEnabled(False)
        btn_row.addWidget(self.run_button)
        btn_row.addWidget(self.stop_button)
        run_form.addRow(btn_row)

        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        run_form.addRow(self.progress_bar)

        self.status_label = QLabel("Idle.")
        run_form.addRow(self.status_label)

        self.paraview_button = QPushButton("Open Latest Result in ParaView")
        self.paraview_button.setObjectName("paraviewButton")
        self.paraview_button.clicked.connect(self._open_in_paraview)
        self.paraview_button.setEnabled(False)
        run_form.addRow(self.paraview_button)

        layout.addWidget(run_group)
        layout.addStretch(1)
        return panel

    # ------------------------------------------------------------------
    def _build_right_panel(self) -> QWidget:
        panel = QWidget()
        layout = QVBoxLayout(panel)

        self.figure = Figure(figsize=(7, 8), constrained_layout=True)
        self.canvas = FigureCanvasQTAgg(self.figure)
        self.ax_field = self.figure.add_subplot(2, 1, 1)
        self.ax_residual = self.figure.add_subplot(2, 1, 2)
        self._init_plots()

        layout.addWidget(self.canvas)
        return panel

    def _init_plots(self):
        self.ax_field.set_title("Velocity magnitude (live preview)")
        self.ax_field.set_xlabel("x")
        self.ax_field.set_ylabel("y")
        self.ax_residual.set_title("Convergence (streamfunction residual)")
        self.ax_residual.set_xlabel("Step")
        self.ax_residual.set_ylabel("Residual")
        self.ax_residual.set_yscale("log")
        self._residual_xs: list[int] = []
        self._residual_ys: list[float] = []
        self.canvas.draw_idle()

    # ------------------------------------------------------------------
    @Slot()
    def _on_scenario_changed(self):
        key = self.scenario_combo.currentData()
        preset = PRESETS[key]
        self.description_label.setText(preset.description)
        self.nx_spin.setValue(preset.default_nx)
        self.ny_spin.setValue(preset.default_ny)
        self.re_spin.setValue(preset.default_Re)
        self.u_spin.setValue(preset.default_U)
        self.obstacle_group.setVisible(preset.has_obstacle)
        if preset.has_obstacle:
            self.obstacle_x0.setValue(preset.Lx * 0.35)
            self.obstacle_width.setValue(preset.Lx * 0.15)
            self.obstacle_height.setValue(preset.Ly * 0.3)
        self._suggest_output_dir()

    def _suggest_output_dir(self):
        key = self.scenario_combo.currentData()
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        base = os.path.join(os.path.dirname(os.path.dirname(__file__)), "runs")
        self.output_dir_edit.setText(os.path.join(base, f"{key}_{timestamp}"))

    def _browse_output_dir(self):
        directory = QFileDialog.getExistingDirectory(self, "Choose Output Folder", self.output_dir_edit.text())
        if directory:
            self.output_dir_edit.setText(directory)

    # ------------------------------------------------------------------
    def _set_controls_enabled(self, enabled: bool):
        for w in (
            self.scenario_combo, self.nx_spin, self.ny_spin, self.re_spin, self.u_spin,
            self.obstacle_x0, self.obstacle_width, self.obstacle_height,
            self.steps_spin, self.output_every_spin, self.output_dir_edit, self.run_button,
        ):
            w.setEnabled(enabled)
        self.stop_button.setEnabled(not enabled)

    def _start_simulation(self):
        key = self.scenario_combo.currentData()
        output_dir = self.output_dir_edit.text().strip()
        if not output_dir:
            QMessageBox.warning(self, "Missing output folder", "Please choose an output folder.")
            return

        preset = PRESETS[key]
        obstacle_kwargs = {}
        if preset.has_obstacle:
            obstacle_kwargs = {
                "obstacle_x0": self.obstacle_x0.value(),
                "obstacle_width": self.obstacle_width.value(),
                "obstacle_height": self.obstacle_height.value(),
            }

        self._residual_xs, self._residual_ys = [], []
        self.ax_residual.cla()
        self.ax_residual.set_title("Convergence (streamfunction residual)")
        self.ax_residual.set_xlabel("Step")
        self.ax_residual.set_ylabel("Residual")
        self.ax_residual.set_yscale("log")

        self._worker = SimulationWorker(
            scenario_key=key,
            Re=self.re_spin.value(),
            U=self.u_spin.value(),
            nx=self.nx_spin.value(),
            ny=self.ny_spin.value(),
            n_steps=self.steps_spin.value(),
            output_every=self.output_every_spin.value(),
            output_dir=output_dir,
            case_name=key,
            **obstacle_kwargs,
        )
        self._thread = QThread(self)
        self._worker.moveToThread(self._thread)
        self._thread.started.connect(self._worker.run)
        self._worker.progress.connect(self._on_progress)
        self._worker.preview.connect(self._on_preview)
        self._worker.finished.connect(self._on_finished)
        self._worker.error.connect(self._on_error)
        self._worker.stopped.connect(self._on_stopped)
        self._worker.finished.connect(self._thread.quit)
        self._worker.error.connect(self._thread.quit)
        self._worker.stopped.connect(self._thread.quit)

        self._set_controls_enabled(False)
        self.paraview_button.setEnabled(False)
        self.progress_bar.setValue(0)
        self.status_label.setText("Running...")
        self._thread.start()

    def _stop_simulation(self):
        if self._worker is not None:
            self._worker.request_stop()
            self.status_label.setText("Stopping...")

    # ------------------------------------------------------------------
    @Slot(int, int, float, float)
    def _on_progress(self, step, total_steps, residual, elapsed):
        pct = int(100 * step / max(1, total_steps))
        self.progress_bar.setValue(pct)
        self.status_label.setText(
            f"Step {step}/{total_steps}  |  residual={residual:.3e}  |  elapsed={elapsed:.1f}s"
        )
        self._residual_xs.append(step)
        self._residual_ys.append(max(residual, 1e-16))
        self.ax_residual.cla()
        self.ax_residual.plot(self._residual_xs, self._residual_ys, color="#2563eb")
        self.ax_residual.set_title("Convergence (streamfunction residual)")
        self.ax_residual.set_xlabel("Step")
        self.ax_residual.set_ylabel("Residual")
        self.ax_residual.set_yscale("log")
        self.canvas.draw_idle()

    @Slot(object)
    def _on_preview(self, fields):
        x, y = fields["x"], fields["y"]
        vel_mag = fields["velocity_magnitude"]
        u, v = fields["velocity_u"], fields["velocity_v"]
        solid = fields["obstacle"]

        self.ax_field.cla()
        mesh = self.ax_field.pcolormesh(x, y, vel_mag, shading="auto", cmap="viridis")
        if not hasattr(self, "_colorbar") or self._colorbar is None:
            self._colorbar = self.figure.colorbar(mesh, ax=self.ax_field, label="|velocity|")
        else:
            self._colorbar.update_normal(mesh)

        try:
            self.ax_field.streamplot(x, y, u, v, color="white", density=1.0, linewidth=0.6)
        except Exception:
            pass

        if solid.any():
            self.ax_field.contourf(x, y, solid, levels=[0.5, 1.5], colors=["#444444"])

        self.ax_field.set_title("Velocity magnitude (live preview)")
        self.ax_field.set_xlabel("x")
        self.ax_field.set_ylabel("y")
        self.ax_field.set_aspect("equal")
        self.canvas.draw_idle()

    @Slot(str)
    def _on_finished(self, pvd_path):
        self._last_pvd_path = pvd_path
        self._set_controls_enabled(True)
        self.paraview_button.setEnabled(True)
        self.progress_bar.setValue(100)
        self.status_label.setText(f"Finished. Output: {pvd_path}")
        self.statusBar().showMessage(f"Simulation complete: {pvd_path}", 8000)

    @Slot()
    def _on_stopped(self):
        self._set_controls_enabled(True)
        self.status_label.setText("Stopped by user.")

    @Slot(str)
    def _on_error(self, message):
        self._set_controls_enabled(True)
        self.status_label.setText("Error.")
        QMessageBox.critical(self, "Simulation Error", message)

    # ------------------------------------------------------------------
    def _open_in_paraview(self):
        if not self._last_pvd_path:
            return
        try:
            launch_paraview(self._last_pvd_path)
        except FileNotFoundError as exc:
            QMessageBox.warning(self, "ParaView not found", str(exc))
            self._locate_paraview()

    def _locate_paraview(self):
        candidates = find_paraview_candidates()
        current = get_paraview_path() or (candidates[0] if candidates else "")
        path, _ = QFileDialog.getOpenFileName(
            self, "Locate paraview.exe", os.path.dirname(current) if current else "C:/Program Files",
            "ParaView Executable (paraview.exe)",
        )
        if path:
            set_paraview_path(path)
            QMessageBox.information(self, "ParaView Located", f"Using ParaView at:\n{path}")

    def _show_about(self):
        QMessageBox.information(
            self,
            "About CFD Studio",
            "CFD Studio\n\n"
            "A 2D incompressible Navier-Stokes solver (vorticity-streamfunction "
            "formulation) with a live preview and direct ParaView export.\n\n"
            "Fields exported: velocity, velocity magnitude, vorticity, "
            "streamfunction, obstacle mask.",
        )

    # ------------------------------------------------------------------
    def closeEvent(self, event):
        if self._worker is not None:
            self._worker.request_stop()
        if self._thread is not None and self._thread.isRunning():
            self._thread.quit()
            self._thread.wait(3000)
        self.three_d_panel.shutdown()
        event.accept()
