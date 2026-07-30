from __future__ import annotations

import os

import numpy as np
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QCheckBox,
    QDialog,
    QDialogButtonBox,
    QHBoxLayout,
    QLabel,
    QRadioButton,
    QVBoxLayout,
    QWidget,
)

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
from matplotlib.figure import Figure
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

from ..solver3d import orientation as orient_mod


class OrientationDialog(QDialog):
    """Shows the mesh with the AI-suggested flow orientation and lets the
    user accept it, pick one of the other candidate axes, flip the flow
    direction, or reject and re-upload."""

    def __init__(self, mesh, mesh_name: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Confirm Flow Orientation")
        self.resize(880, 620)

        self.mesh = mesh
        self.candidates = orient_mod.analyze_orientation(mesh)
        self.selected_index = 0
        self.reversed_flow = False
        self.confirmed_mesh = None

        # decimate a copy for fast preview rendering only
        preview_mesh = mesh
        if len(mesh.faces) > 4000:
            try:
                preview_mesh = mesh.simplify_quadric_decimation(4000)
            except Exception:
                preview_mesh = mesh
        self._preview_mesh = preview_mesh

        self._build_ui(mesh_name)
        self._update_preview()

    # ------------------------------------------------------------------
    def _build_ui(self, mesh_name: str):
        root = QHBoxLayout(self)

        self.figure = Figure(figsize=(6, 6))
        self.canvas = FigureCanvasQTAgg(self.figure)
        self.ax = self.figure.add_subplot(111, projection="3d")
        root.addWidget(self.canvas, 1)

        side = QWidget()
        side.setFixedWidth(300)
        side_layout = QVBoxLayout(side)
        side_layout.setAlignment(Qt.AlignTop)

        title = QLabel(f"<b>{os.path.basename(mesh_name)}</b>")
        side_layout.addWidget(title)

        explain = QLabel(
            "The white arrow shows the proposed incoming flow direction. This is a "
            "geometric heuristic (not a trained model): it ranks the mesh's principal "
            "axes by projected frontal area, on the reasoning that streamlined bodies "
            "are shaped to present their smallest cross-section to the flow. Please "
            "confirm it looks right before running."
        )
        explain.setObjectName("description")
        explain.setWordWrap(True)
        side_layout.addWidget(explain)

        side_layout.addWidget(QLabel("<b>Candidate orientations:</b>"))
        self.radio_buttons = []
        for i, cand in enumerate(self.candidates):
            rb = QRadioButton(f"{cand.label} — projected area {cand.projected_area:.4g}")
            rb.setChecked(i == 0)
            rb.toggled.connect(lambda checked, idx=i: self._on_candidate_selected(idx, checked))
            side_layout.addWidget(rb)
            self.radio_buttons.append(rb)

        if len(self.candidates) >= 2:
            ratio = self.candidates[1].projected_area / max(self.candidates[0].projected_area, 1e-9)
            if ratio < 1.15:
                warn = QLabel(
                    "⚠ The top two candidates are close in projected area — this shape "
                    "is ambiguous from geometry alone, so please look carefully."
                )
                warn.setWordWrap(True)
                warn.setStyleSheet("color: #b45309; font-weight: 600;")
                side_layout.addWidget(warn)

        self.reverse_checkbox = QCheckBox("Reverse flow direction (flip front/back)")
        self.reverse_checkbox.toggled.connect(self._on_reverse_toggled)
        side_layout.addWidget(self.reverse_checkbox)

        side_layout.addStretch(1)

        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.button(QDialogButtonBox.Ok).setText("Use This Orientation")
        buttons.accepted.connect(self._on_accept)
        buttons.rejected.connect(self.reject)
        side_layout.addWidget(buttons)

        root.addWidget(side, 0)

    # ------------------------------------------------------------------
    def _on_candidate_selected(self, idx, checked):
        if checked:
            self.selected_index = idx
            self._update_preview()

    def _on_reverse_toggled(self, checked):
        self.reversed_flow = checked
        self._update_preview()

    def _current_candidate(self):
        cand = self.candidates[self.selected_index]
        if not self.reversed_flow:
            return cand
        flipped = orient_mod.OrientationCandidate(
            label=cand.label,
            flow_axis=-cand.flow_axis,
            up_axis=cand.up_axis,
            span_axis=-cand.span_axis,
            projected_area=cand.projected_area,
            rank=cand.rank,
        )
        return flipped

    def _update_preview(self):
        cand = self._current_candidate()
        oriented = orient_mod.apply_orientation(self._preview_mesh, cand)

        self.ax.cla()
        verts = oriented.vertices
        faces = oriented.faces
        mesh_polys = verts[faces]
        collection = Poly3DCollection(mesh_polys, alpha=0.7, facecolor="#60a5fa", edgecolor="#1e3a8a", linewidths=0.1)
        self.ax.add_collection3d(collection)

        extent = np.max(np.ptp(verts, axis=0)) * 0.6 + 1e-6
        center = verts.mean(axis=0)
        self.ax.set_xlim(center[0] - extent, center[0] + extent)
        self.ax.set_ylim(center[1] - extent, center[1] + extent)
        self.ax.set_zlim(center[2] - extent, center[2] + extent)

        arrow_len = extent * 1.3
        arrow_start = np.array([center[0] - extent * 1.1, center[1], center[2]])
        self.ax.quiver(
            arrow_start[0], arrow_start[1], arrow_start[2],
            arrow_len, 0, 0,
            color="white", edgecolor="black", linewidth=1.5, arrow_length_ratio=0.15,
        )
        self.ax.text(arrow_start[0] - 0.05 * arrow_len, arrow_start[1], arrow_start[2], "flow →", color="black")

        self.ax.set_xlabel("x (flow)")
        self.ax.set_ylabel("y (up)")
        self.ax.set_zlabel("z (span)")
        self.ax.set_title("Preview: proposed simulation orientation")
        self.canvas.draw_idle()

    # ------------------------------------------------------------------
    def _on_accept(self):
        cand = self._current_candidate()
        self.confirmed_mesh = orient_mod.apply_orientation(self.mesh, cand)
        self.accept()
