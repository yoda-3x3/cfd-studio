from __future__ import annotations

import datetime
import os

import numpy as np
from PySide6.QtCore import Qt, QThread, Slot
from PySide6.QtWidgets import (
    QCheckBox,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QProgressBar,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
from matplotlib.figure import Figure

from ..paraview_launcher import find_paraview_candidates, get_paraview_path, launch_paraview, set_paraview_path
from ..solver3d import geometry, kernels
from .orientation_dialog import OrientationDialog
from .worker3d import Simulation3DWorker


class ThreeDPanel(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._thread: QThread | None = None
        self._worker: Simulation3DWorker | None = None
        self._last_foam_path: str | None = None
        self._mesh_path: str | None = None
        self._raw_mesh = None
        self._oriented_mesh = None
        self._residual_xs: list[int] = []
        self._residual_ys: list[float] = []
        self._build_ui()

    # ------------------------------------------------------------------
    def _build_ui(self):
        root = QHBoxLayout(self)
        root.addWidget(self._build_left_panel(), 0)
        root.addWidget(self._build_right_panel(), 1)

    def _build_left_panel(self) -> QWidget:
        panel = QWidget()
        panel.setFixedWidth(380)
        layout = QVBoxLayout(panel)
        layout.setAlignment(Qt.AlignTop)

        geo_group = QGroupBox("3D Geometry")
        geo_form = QFormLayout(geo_group)

        upload_row = QHBoxLayout()
        self.upload_button = QPushButton("Upload 3D File...")
        self.upload_button.clicked.connect(self._browse_mesh_file)
        upload_row.addWidget(self.upload_button)
        self.reorient_button = QPushButton("Re-check Orientation...")
        self.reorient_button.clicked.connect(self._reopen_orientation_dialog)
        self.reorient_button.setEnabled(False)
        upload_row.addWidget(self.reorient_button)
        upload_widget = QWidget()
        upload_widget.setLayout(upload_row)
        geo_form.addRow(upload_widget)

        self.mesh_info_label = QLabel("No file selected. Supported: STL, OBJ, PLY, OFF.")
        self.mesh_info_label.setObjectName("description")
        self.mesh_info_label.setWordWrap(True)
        geo_form.addRow(self.mesh_info_label)

        self.inflow_gap_spin = QDoubleSpinBox()
        self.inflow_gap_spin.setRange(0.2, 20.0)
        self.inflow_gap_spin.setValue(1.5)
        self.wake_gap_spin = QDoubleSpinBox()
        self.wake_gap_spin.setRange(0.5, 40.0)
        self.wake_gap_spin.setValue(4.0)
        self.lateral_gap_spin = QDoubleSpinBox()
        self.lateral_gap_spin.setRange(0.2, 20.0)
        self.lateral_gap_spin.setValue(1.5)
        geo_form.addRow("Inflow clearance (x object size):", self.inflow_gap_spin)
        geo_form.addRow("Wake clearance (x object size):", self.wake_gap_spin)
        geo_form.addRow("Lateral clearance (x object size):", self.lateral_gap_spin)

        layout.addWidget(geo_group)

        case_group = QGroupBox("Case Setup")
        case_form = QFormLayout(case_group)

        grid_row = QHBoxLayout()
        self.nx_spin = QSpinBox()
        self.nx_spin.setRange(8, 300)
        self.nx_spin.setValue(100)
        self.ny_spin = QSpinBox()
        self.ny_spin.setRange(8, 300)
        self.ny_spin.setValue(60)
        self.nz_spin = QSpinBox()
        self.nz_spin.setRange(8, 300)
        self.nz_spin.setValue(60)
        for w in (self.nx_spin, self.ny_spin, self.nz_spin):
            grid_row.addWidget(w)
        grid_widget = QWidget()
        grid_widget.setLayout(grid_row)
        case_form.addRow("Grid (nx, ny, nz):", grid_widget)

        self.re_spin = QDoubleSpinBox()
        self.re_spin.setRange(1.0, 100000.0)
        self.re_spin.setDecimals(0)
        self.re_spin.setValue(200.0)
        case_form.addRow("Reynolds number:", self.re_spin)

        self.u_spin = QDoubleSpinBox()
        self.u_spin.setRange(0.01, 50.0)
        self.u_spin.setValue(1.0)
        case_form.addRow("Inflow speed:", self.u_spin)

        max_threads = kernels.max_thread_count()
        self.threads_spin = QSpinBox()
        self.threads_spin.setRange(1, max_threads)
        self.threads_spin.setValue(min(8, max_threads))
        case_form.addRow(f"CPU threads (max {max_threads}):", self.threads_spin)
        threads_note = QLabel(
            "The pressure solve is parallelized across cores, but this kind of "
            "stencil computation is memory-bandwidth-bound: expect a real but "
            "modest speedup (often best around 4-8 threads) rather than linear "
            "scaling to every core."
        )
        threads_note.setObjectName("description")
        threads_note.setWordWrap(True)
        case_form.addRow(threads_note)

        layout.addWidget(case_group)

        run_group = QGroupBox("Run")
        run_form = QFormLayout(run_group)

        self.steps_spin = QSpinBox()
        self.steps_spin.setRange(50, 500000)
        self.steps_spin.setValue(1500)
        self.steps_spin.setSingleStep(100)
        run_form.addRow("Number of steps:", self.steps_spin)

        self.output_every_spin = QSpinBox()
        self.output_every_spin.setRange(1, 5000)
        self.output_every_spin.setValue(25)
        run_form.addRow("Write output every N steps:", self.output_every_spin)

        out_row = QHBoxLayout()
        self.output_dir_edit = QLineEdit()
        browse_out_btn = QPushButton("Browse...")
        browse_out_btn.clicked.connect(self._browse_output_dir)
        out_row.addWidget(self.output_dir_edit)
        out_row.addWidget(browse_out_btn)
        out_widget = QWidget()
        out_widget.setLayout(out_row)
        run_form.addRow("Output folder:", out_widget)
        self._suggest_output_dir()

        self.force_rerun_checkbox = QCheckBox("Force re-run (ignore cached results)")
        run_form.addRow(self.force_rerun_checkbox)

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

        self.status_label = QLabel("Upload a 3D file to begin.")
        self.status_label.setWordWrap(True)
        run_form.addRow(self.status_label)

        self.paraview_button = QPushButton("Open Latest Result in ParaView")
        self.paraview_button.setObjectName("paraviewButton")
        self.paraview_button.clicked.connect(self._open_in_paraview)
        self.paraview_button.setEnabled(False)
        run_form.addRow(self.paraview_button)

        layout.addWidget(run_group)
        layout.addStretch(1)
        return panel

    def _build_right_panel(self) -> QWidget:
        panel = QWidget()
        vbox = QVBoxLayout(panel)
        self.figure = Figure(figsize=(7, 8), constrained_layout=True)
        self.canvas = FigureCanvasQTAgg(self.figure)
        self.ax_field = self.figure.add_subplot(2, 1, 1)
        self.ax_residual = self.figure.add_subplot(2, 1, 2)
        self._colorbar = None
        self._init_plots()
        vbox.addWidget(self.canvas)
        return panel

    def _init_plots(self):
        self.ax_field.set_title("Mid-plane slice (z = Lz/2): velocity magnitude")
        self.ax_field.set_xlabel("x")
        self.ax_field.set_ylabel("y")
        self.ax_residual.set_title("Convergence (bulk-flow divergence residual)")
        self.ax_residual.set_xlabel("Step")
        self.ax_residual.set_ylabel("Residual")
        self.ax_residual.set_yscale("log")
        self.canvas.draw_idle()

    # ------------------------------------------------------------------
    def _browse_mesh_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Upload 3D Geometry",
            "", "3D Mesh Files (*.stl *.obj *.ply *.off);;All Files (*.*)",
        )
        if not path:
            return
        try:
            mesh = geometry.load_mesh(path)
        except Exception as exc:  # noqa: BLE001
            QMessageBox.critical(self, "Could not load file", str(exc))
            return
        self._mesh_path = path
        self._raw_mesh = mesh
        self.reorient_button.setEnabled(True)
        self._run_orientation_dialog(mesh, path)
        self._suggest_output_dir()

    def _run_orientation_dialog(self, mesh, path):
        dlg = OrientationDialog(mesh, path, parent=self)
        if dlg.exec() == OrientationDialog.Accepted and dlg.confirmed_mesh is not None:
            self._oriented_mesh = dlg.confirmed_mesh
        else:
            self._oriented_mesh = mesh  # fall back to the un-rotated mesh if the user cancels

        ex, ey, ez = self._oriented_mesh.extents
        watertight = "yes" if mesh.is_watertight else "no (containment test may be less reliable)"
        self.mesh_info_label.setText(
            f"{os.path.basename(path)}: {len(mesh.faces)} triangles, "
            f"oriented extents ({ex:.3g} x {ey:.3g} x {ez:.3g}), watertight: {watertight}. "
            f"The largest dimension is normalized to 1.0 as the characteristic length for Re."
        )

    def _reopen_orientation_dialog(self):
        if self._raw_mesh is None:
            return
        self._run_orientation_dialog(self._raw_mesh, self._mesh_path)

    def _suggest_output_dir(self):
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        base = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "runs")
        self.output_dir_edit.setText(os.path.join(base, f"custom3d_{timestamp}"))

    def _browse_output_dir(self):
        directory = QFileDialog.getExistingDirectory(self, "Choose Output Folder", self.output_dir_edit.text())
        if directory:
            self.output_dir_edit.setText(directory)

    # ------------------------------------------------------------------
    def _set_controls_enabled(self, enabled: bool):
        for w in (
            self.upload_button, self.reorient_button, self.inflow_gap_spin, self.wake_gap_spin, self.lateral_gap_spin,
            self.nx_spin, self.ny_spin, self.nz_spin, self.re_spin, self.u_spin, self.threads_spin,
            self.steps_spin, self.output_every_spin, self.output_dir_edit, self.run_button,
            self.force_rerun_checkbox,
        ):
            w.setEnabled(enabled)
        self.stop_button.setEnabled(not enabled)

    def _start_simulation(self):
        if self._oriented_mesh is None:
            QMessageBox.warning(self, "No file uploaded", "Please upload a 3D geometry file first.")
            return
        output_dir = self.output_dir_edit.text().strip()
        if not output_dir:
            QMessageBox.warning(self, "Missing output folder", "Please choose an output folder.")
            return

        self._residual_xs, self._residual_ys = [], []
        self.ax_residual.cla()
        self._init_plots()

        self._worker = Simulation3DWorker(
            mesh=self._oriented_mesh,
            mesh_name=os.path.basename(self._mesh_path) if self._mesh_path else "mesh",
            nx=self.nx_spin.value(), ny=self.ny_spin.value(), nz=self.nz_spin.value(),
            Re=self.re_spin.value(), U_in=self.u_spin.value(),
            n_steps=self.steps_spin.value(), output_every=self.output_every_spin.value(),
            output_dir=output_dir,
            num_threads=self.threads_spin.value(),
            inflow_gap=self.inflow_gap_spin.value(),
            wake_gap=self.wake_gap_spin.value(),
            lateral_gap=self.lateral_gap_spin.value(),
            force_rerun=self.force_rerun_checkbox.isChecked(),
        )
        self._thread = QThread(self)
        self._worker.moveToThread(self._thread)
        self._thread.started.connect(self._worker.run)
        self._worker.status.connect(self._on_status)
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
        self.status_label.setText("Starting...")
        self._thread.start()

    def _stop_simulation(self):
        if self._worker is not None:
            self._worker.request_stop()
            self.status_label.setText("Stopping...")

    # ------------------------------------------------------------------
    @Slot(str)
    def _on_status(self, message):
        self.status_label.setText(message)

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
        self.ax_residual.set_title("Convergence (bulk-flow divergence residual)")
        self.ax_residual.set_xlabel("Step")
        self.ax_residual.set_ylabel("Residual")
        self.ax_residual.set_yscale("log")
        self.canvas.draw_idle()

    @Slot(object)
    def _on_preview(self, slice_fields):
        x, y = slice_fields["x"], slice_fields["y"]
        vel_mag = slice_fields["velocity_magnitude"].T
        u = slice_fields["velocity_u"].T
        v = slice_fields["velocity_v"].T
        solid = slice_fields["obstacle"].T

        self.ax_field.cla()
        mesh = self.ax_field.pcolormesh(x, y, vel_mag, shading="auto", cmap="viridis")
        if self._colorbar is None:
            self._colorbar = self.figure.colorbar(mesh, ax=self.ax_field, label="|velocity|")
        else:
            self._colorbar.update_normal(mesh)
        try:
            self.ax_field.streamplot(x, y, u, v, color="white", density=1.0, linewidth=0.6)
        except Exception:
            pass
        if solid.any():
            self.ax_field.contourf(x, y, solid, levels=[0.5, 1.5], colors=["#444444"])
        self.ax_field.set_title("Mid-plane slice (z = Lz/2): velocity magnitude")
        self.ax_field.set_xlabel("x")
        self.ax_field.set_ylabel("y")
        self.ax_field.set_aspect("equal")
        self.canvas.draw_idle()

    @Slot(str, bool)
    def _on_finished(self, foam_path, was_cached):
        self._last_foam_path = foam_path
        self._set_controls_enabled(True)
        self.paraview_button.setEnabled(True)
        self.progress_bar.setValue(100)
        if was_cached:
            self.status_label.setText(f"Reused cached result (identical file + settings already run). OpenFOAM case: {foam_path}")
        else:
            self.status_label.setText(f"Finished. OpenFOAM case: {foam_path}")

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
        if not self._last_foam_path:
            return
        try:
            launch_paraview(self._last_foam_path)
        except FileNotFoundError as exc:
            QMessageBox.warning(self, "ParaView not found", str(exc))

    def shutdown(self):
        if self._worker is not None:
            self._worker.request_stop()
        if self._thread is not None and self._thread.isRunning():
            self._thread.quit()
            self._thread.wait(3000)
