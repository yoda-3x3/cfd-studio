from __future__ import annotations

import datetime
import os

import numpy as np
from PySide6.QtCore import Qt, QThread, Slot
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
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
    QRadioButton,
    QScrollArea,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
from matplotlib.figure import Figure

from ..paraview_launcher import find_paraview_candidates, get_paraview_path, launch_paraview, set_paraview_path
from ..solver3d import geometry, kernels, materials, presets
from . import theme as theme_mod
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
        self._prefer_native = False
        self._theme = theme_mod.THEMES[theme_mod.DEFAULT_THEME_KEY]  # so anything built before set_theme() has a theme to read
        self._build_ui()

    # ------------------------------------------------------------------
    def _build_ui(self):
        root = QHBoxLayout(self)

        left_scroll = QScrollArea()
        left_scroll.setWidget(self._build_left_panel())
        left_scroll.setWidgetResizable(True)
        left_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        left_scroll.setFrameShape(QScrollArea.NoFrame)
        # the panel itself has a fixed content width; the scroll area just
        # needs to be a little wider to leave room for the scrollbar
        left_scroll.setFixedWidth(380 + 22)
        root.addWidget(left_scroll, 0)

        root.addWidget(self._build_right_panel(), 1)

    def _build_left_panel(self) -> QWidget:
        panel = QWidget()
        panel.setFixedWidth(380)
        layout = QVBoxLayout(panel)
        layout.setAlignment(Qt.AlignTop)

        geo_group = QGroupBox("3D Geometry")
        geo_form = QFormLayout(geo_group)
        self.geo_form = geo_form

        flow_mode_row = QHBoxLayout()
        self.flow_mode_external_radio = QRadioButton("External (object in tunnel)")
        self.flow_mode_external_radio.setChecked(True)
        self.flow_mode_external_radio.toggled.connect(self._on_flow_mode_toggled)
        self.flow_mode_internal_radio = QRadioButton("Internal (pipe/duct)")
        flow_mode_row.addWidget(self.flow_mode_external_radio)
        flow_mode_row.addWidget(self.flow_mode_internal_radio)
        flow_mode_widget = QWidget()
        flow_mode_widget.setLayout(flow_mode_row)
        geo_form.addRow("Flow type:", flow_mode_widget)

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
        self.inflow_gap_spin.setRange(0.0, 20.0)
        self.inflow_gap_spin.setValue(1.5)
        self.wake_gap_spin = QDoubleSpinBox()
        self.wake_gap_spin.setRange(0.0, 40.0)
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

        self.perf_preset_combo = QComboBox()
        for preset in presets.PRESETS.values():
            self.perf_preset_combo.addItem(preset.label, preset.key)
        default_idx = self.perf_preset_combo.findData(presets.DEFAULT_PRESET_KEY)
        if default_idx >= 0:
            self.perf_preset_combo.setCurrentIndex(default_idx)
        self.perf_preset_combo.currentIndexChanged.connect(self._on_performance_preset_changed)
        case_form.addRow("Performance preset:", self.perf_preset_combo)

        self.perf_preset_info_label = QLabel()
        self.perf_preset_info_label.setObjectName("description")
        self.perf_preset_info_label.setWordWrap(True)
        case_form.addRow(self.perf_preset_info_label)

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

        re_mode_row = QHBoxLayout()
        self.re_mode_direct_radio = QRadioButton("Directly")
        self.re_mode_direct_radio.setChecked(True)
        self.re_mode_direct_radio.toggled.connect(self._on_re_mode_toggled)
        self.re_mode_physical_radio = QRadioButton("From physical properties")
        re_mode_row.addWidget(self.re_mode_direct_radio)
        re_mode_row.addWidget(self.re_mode_physical_radio)
        re_mode_widget = QWidget()
        re_mode_widget.setLayout(re_mode_row)
        case_form.addRow("Set Reynolds number:", re_mode_widget)

        self.re_spin = QDoubleSpinBox()
        self.re_spin.setRange(1.0, 10000000.0)
        self.re_spin.setDecimals(0)
        self.re_spin.setValue(200.0)
        case_form.addRow("Reynolds number:", self.re_spin)

        self.material_panel = QWidget()
        material_form = QFormLayout(self.material_panel)
        material_form.setContentsMargins(0, 0, 0, 0)

        self.material_combo = QComboBox()
        for key, preset in materials.PRESETS.items():
            self.material_combo.addItem(preset.label, key)
        self.material_combo.addItem("Custom fluid...", materials.CUSTOM_KEY)
        self.material_combo.currentIndexChanged.connect(self._on_material_changed)
        material_form.addRow("Fluid:", self.material_combo)

        self.custom_rho_spin = QDoubleSpinBox()
        self.custom_rho_spin.setRange(0.001, 100000.0)
        self.custom_rho_spin.setDecimals(4)
        self.custom_rho_spin.setValue(1.0)
        self.custom_rho_spin.valueChanged.connect(self._recompute_physical_re)
        material_form.addRow("Density ρ (kg/m³):", self.custom_rho_spin)

        self.custom_nu_spin = QDoubleSpinBox()
        self.custom_nu_spin.setRange(1e-9, 1.0)
        self.custom_nu_spin.setDecimals(9)
        self.custom_nu_spin.setValue(1.0e-6)
        self.custom_nu_spin.valueChanged.connect(self._recompute_physical_re)
        material_form.addRow("Kinematic viscosity ν (m²/s):", self.custom_nu_spin)

        self.material_info_label = QLabel()
        self.material_info_label.setObjectName("description")
        self.material_info_label.setWordWrap(True)
        material_form.addRow(self.material_info_label)

        self.char_length_spin = QDoubleSpinBox()
        self.char_length_spin.setRange(1e-4, 1000.0)
        self.char_length_spin.setDecimals(4)
        self.char_length_spin.setValue(1.0)
        self.char_length_spin.valueChanged.connect(self._recompute_physical_re)
        material_form.addRow("Characteristic length (m):", self.char_length_spin)

        self.velocity_spin = QDoubleSpinBox()
        self.velocity_spin.setRange(1e-4, 1000.0)
        self.velocity_spin.setDecimals(4)
        self.velocity_spin.setValue(1.0)
        self.velocity_spin.valueChanged.connect(self._recompute_physical_re)
        material_form.addRow("Velocity (m/s):", self.velocity_spin)

        case_form.addRow(self.material_panel)
        self.material_panel.setVisible(False)
        self._on_material_changed()

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

        # applies the default preset's values now that every spinbox it
        # touches (grid/steps/output_every, across multiple groups above)
        # actually exists
        self._on_performance_preset_changed()
        return panel

    def _build_right_panel(self) -> QWidget:
        panel = QWidget()
        vbox = QVBoxLayout(panel)
        self.figure = Figure(figsize=(10, 8), constrained_layout=True)
        self.canvas = FigureCanvasQTAgg(self.figure)
        # 2x2 live view: two orthogonal field slices (so the preview isn't
        # blind to spanwise/vertical structure), a pressure slice, and the
        # convergence plot, instead of just one field slice + residual.
        self.ax_field = self.figure.add_subplot(2, 2, 1)
        self.ax_pressure = self.figure.add_subplot(2, 2, 2)
        self.ax_field_xz = self.figure.add_subplot(2, 2, 3)
        self.ax_residual = self.figure.add_subplot(2, 2, 4)
        self._colorbar = None
        self._colorbar_pressure = None
        self._colorbar_xz = None
        self._init_plots()
        vbox.addWidget(self.canvas)
        return panel

    def _init_plots(self):
        self.ax_field.set_title("XY slice (z = Lz/2): velocity magnitude")
        self.ax_field.set_xlabel("x")
        self.ax_field.set_ylabel("y")
        self.ax_pressure.set_title("XY slice (z = Lz/2): pressure")
        self.ax_pressure.set_xlabel("x")
        self.ax_pressure.set_ylabel("y")
        self.ax_field_xz.set_title("XZ slice (y = Ly/2): velocity magnitude")
        self.ax_field_xz.set_xlabel("x")
        self.ax_field_xz.set_ylabel("z")
        self.ax_residual.set_title("Convergence (bulk-flow divergence residual)")
        self.ax_residual.set_xlabel("Step")
        self.ax_residual.set_ylabel("Residual")
        self.ax_residual.set_yscale("log")
        theme_mod.apply_theme_to_figure(self.figure, self._theme)
        self.canvas.draw_idle()

    def set_theme(self, th: theme_mod.Theme):
        self._theme = th
        theme_mod.apply_theme_to_figure(self.figure, th)
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

    # ------------------------------------------------------------------
    def _on_performance_preset_changed(self):
        key = self.perf_preset_combo.currentData()
        preset = presets.PRESETS[key]
        self.nx_spin.setValue(preset.nx)
        self.ny_spin.setValue(preset.ny)
        self.nz_spin.setValue(preset.nz)
        self.steps_spin.setValue(preset.steps)
        self.output_every_spin.setValue(preset.output_every)
        self._prefer_native = preset.prefer_native

        if preset.prefer_native and kernels.native_available:
            backend_note = "C++ (single-threaded -- no per-call thread overhead, fastest at this grid size)"
        elif preset.prefer_native:
            backend_note = "numba (this preset prefers the C++ kernels, but they aren't built on this machine -- falling back)"
        else:
            backend_note = "numba (multi-threaded -- wins at this grid size once there's enough work per call)"
        self.perf_preset_info_label.setText(f"{preset.description} Backend: {backend_note}.")

    # ------------------------------------------------------------------
    def _on_flow_mode_toggled(self, external_checked: bool):
        internal = not external_checked
        # internal flow: the pipe/duct wall itself defines the lateral extent,
        # so there's no lateral clearance to configure; entrance/exit length
        # defaults to 0 since BCs can apply directly at the pipe's own ends.
        self.geo_form.setRowVisible(self.lateral_gap_spin, not internal)
        inflow_label = self.geo_form.labelForField(self.inflow_gap_spin)
        wake_label = self.geo_form.labelForField(self.wake_gap_spin)
        if internal:
            inflow_label.setText("Entrance length (x pipe size):")
            wake_label.setText("Exit length (x pipe size):")
            self.inflow_gap_spin.setValue(0.0)
            self.wake_gap_spin.setValue(0.0)
        else:
            inflow_label.setText("Inflow clearance (x object size):")
            wake_label.setText("Wake clearance (x object size):")
            self.inflow_gap_spin.setValue(1.5)
            self.wake_gap_spin.setValue(4.0)

    def _on_re_mode_toggled(self, direct_checked: bool):
        self.material_panel.setVisible(not direct_checked)
        self.re_spin.setReadOnly(not direct_checked)
        if not direct_checked:
            self._recompute_physical_re()

    def _on_material_changed(self):
        key = self.material_combo.currentData()
        is_custom = key == materials.CUSTOM_KEY
        self.custom_rho_spin.setEnabled(is_custom)
        self.custom_nu_spin.setEnabled(is_custom)
        if not is_custom:
            preset = materials.PRESETS[key]
            self.custom_rho_spin.blockSignals(True)
            self.custom_nu_spin.blockSignals(True)
            self.custom_rho_spin.setValue(preset.rho)
            self.custom_nu_spin.setValue(preset.nu)
            self.custom_rho_spin.blockSignals(False)
            self.custom_nu_spin.blockSignals(False)
            self.material_info_label.setText(f"ρ = {preset.rho:.4g} kg/m³, ν = {preset.nu:.4g} m²/s")
        else:
            self.material_info_label.setText("Enter custom fluid properties below.")
        self._recompute_physical_re()

    def _recompute_physical_re(self):
        nu = self.custom_nu_spin.value()
        L = self.char_length_spin.value()
        U = self.velocity_spin.value()
        re = materials.reynolds_number(U, L, nu)
        if self.re_mode_physical_radio.isChecked():
            self.re_spin.setValue(re)

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
            self.re_mode_direct_radio, self.re_mode_physical_radio, self.material_combo,
            self.custom_rho_spin, self.custom_nu_spin, self.char_length_spin, self.velocity_spin,
            self.flow_mode_external_radio, self.flow_mode_internal_radio,
            self.perf_preset_combo,
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

        # switch kernel backend to match the selected performance preset
        # right before it's actually used -- not on every preset-combo
        # change, so global solver state only moves at a well-defined point
        active_backend = kernels.use_native_backend(self._prefer_native)
        backend_label = "C++ (single-threaded)" if active_backend else "numba"

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
            domain_mode="internal" if self.flow_mode_internal_radio.isChecked() else "external",
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
        self.status_label.setText(f"Starting... (kernels: {backend_label})")
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
        self.ax_residual.plot(self._residual_xs, self._residual_ys, color=self._theme.accent)
        self.ax_residual.set_title("Convergence (bulk-flow divergence residual)")
        self.ax_residual.set_xlabel("Step")
        self.ax_residual.set_ylabel("Residual")
        self.ax_residual.set_yscale("log")
        theme_mod.apply_theme_to_axes(self.ax_residual, self._theme)
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
        self.ax_field.set_title("XY slice (z = Lz/2): velocity magnitude")
        self.ax_field.set_xlabel("x")
        self.ax_field.set_ylabel("y")
        self.ax_field.set_aspect("equal")
        theme_mod.apply_theme_to_axes(self.ax_field, self._theme)

        if "pressure" in slice_fields:
            p = slice_fields["pressure"].T
            self.ax_pressure.cla()
            pmax = max(float(np.max(np.abs(p))), 1e-12)
            pmesh = self.ax_pressure.pcolormesh(x, y, p, shading="auto", cmap="RdBu_r", vmin=-pmax, vmax=pmax)
            if self._colorbar_pressure is None:
                self._colorbar_pressure = self.figure.colorbar(pmesh, ax=self.ax_pressure, label="pressure")
            else:
                self._colorbar_pressure.update_normal(pmesh)
            if solid.any():
                self.ax_pressure.contourf(x, y, solid, levels=[0.5, 1.5], colors=["#444444"])
            self.ax_pressure.set_title("XY slice (z = Lz/2): pressure")
            self.ax_pressure.set_xlabel("x")
            self.ax_pressure.set_ylabel("y")
            self.ax_pressure.set_aspect("equal")
            theme_mod.apply_theme_to_axes(self.ax_pressure, self._theme)

        if "velocity_magnitude_xz" in slice_fields:
            z = slice_fields["z"]
            vel_mag_xz = slice_fields["velocity_magnitude_xz"].T
            solid_xz = slice_fields["obstacle_xz"].T
            self.ax_field_xz.cla()
            xzmesh = self.ax_field_xz.pcolormesh(x, z, vel_mag_xz, shading="auto", cmap="viridis")
            if self._colorbar_xz is None:
                self._colorbar_xz = self.figure.colorbar(xzmesh, ax=self.ax_field_xz, label="|velocity|")
            else:
                self._colorbar_xz.update_normal(xzmesh)
            if solid_xz.any():
                self.ax_field_xz.contourf(x, z, solid_xz, levels=[0.5, 1.5], colors=["#444444"])
            self.ax_field_xz.set_title("XZ slice (y = Ly/2): velocity magnitude")
            self.ax_field_xz.set_xlabel("x")
            self.ax_field_xz.set_ylabel("z")
            self.ax_field_xz.set_aspect("equal")
            theme_mod.apply_theme_to_axes(self.ax_field_xz, self._theme)

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
