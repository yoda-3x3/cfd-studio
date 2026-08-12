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
    QPushButton,
    QRadioButton,
    QVBoxLayout,
    QWidget,
)

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
from matplotlib.figure import Figure
from mpl_toolkits.mplot3d import proj3d
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

from ..solver3d import orientation as orient_mod

# mplot3d has no hardware acceleration -- every mouse-drag rotation frame
# re-projects every polygon/point in pure Python, so keeping these counts
# low is what keeps interactive rotation smooth, not draw-call cleverness.
_MAX_PREVIEW_FACES = 1500
_MAX_PICK_POINTS = 500


class OrientationDialog(QDialog):
    """Shows the mesh with the proposed flow orientation and lets the user
    either accept/pick one of the 3 auto-suggested candidate axes (flipping
    direction if needed), or click a point directly on the mesh to mark the
    leading edge — the point that should face into the incoming flow — and
    have the flow axis derived from that pick instead."""

    def __init__(self, mesh, mesh_name: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Confirm Flow Orientation")
        self.resize(880, 620)

        self.mesh = mesh
        self.candidates = orient_mod.analyze_orientation(mesh)
        self.selected_index = 0
        self.reversed_flow = False
        self.confirmed_mesh = None

        self.pick_mode = False
        self.picked_vertex_index: int | None = None
        self._scatter = None

        # decimate a copy for fast interactive rotation (mplot3d redraws
        # every polygon on every drag frame, so face count directly sets
        # how smooth rotating the preview feels)
        preview_mesh = mesh
        if len(mesh.faces) > _MAX_PREVIEW_FACES:
            try:
                # `face_count` must be passed by keyword: the first positional
                # arg is `percent` (a 0-1 reduction fraction) in current
                # trimesh, so a bare face-count int here silently fails and
                # falls through to the full-resolution mesh below.
                preview_mesh = mesh.simplify_quadric_decimation(face_count=_MAX_PREVIEW_FACES)
            except Exception:
                preview_mesh = mesh
        self._preview_mesh = preview_mesh
        self._preview_centroid = preview_mesh.vertices.mean(axis=0)

        # a further-subsampled set of vertices for the pick-mode scatter --
        # picking only needs "close enough to the true leading edge", not
        # every single decimated vertex, so this stays small independent of
        # how detailed the shaded preview mesh itself is
        n_verts = len(preview_mesh.vertices)
        if n_verts > _MAX_PICK_POINTS:
            self._pick_vertex_indices = np.linspace(0, n_verts - 1, _MAX_PICK_POINTS).astype(int)
        else:
            self._pick_vertex_indices = np.arange(n_verts)

        self._build_ui(mesh_name)
        self._update_preview()

    # ------------------------------------------------------------------
    def _build_ui(self, mesh_name: str):
        root = QHBoxLayout(self)

        self.figure = Figure(figsize=(6, 6))
        self.canvas = FigureCanvasQTAgg(self.figure)
        self.ax = self.figure.add_subplot(111, projection="3d")
        self.canvas.mpl_connect("pick_event", self._on_pick)
        root.addWidget(self.canvas, 1)

        side = QWidget()
        side.setFixedWidth(300)
        side_layout = QVBoxLayout(side)
        side_layout.setAlignment(Qt.AlignTop)

        title = QLabel(f"<b>{os.path.basename(mesh_name)}</b>")
        side_layout.addWidget(title)

        mode_row = QHBoxLayout()
        self.auto_mode_radio = QRadioButton("Auto-suggest")
        self.auto_mode_radio.setChecked(True)
        self.auto_mode_radio.toggled.connect(self._on_mode_toggled)
        self.pick_mode_radio = QRadioButton("Pick leading edge on mesh")
        mode_row.addWidget(self.auto_mode_radio)
        mode_row.addWidget(self.pick_mode_radio)
        mode_widget = QWidget()
        mode_widget.setLayout(mode_row)
        side_layout.addWidget(mode_widget)

        # ---- Auto-suggest panel ----
        self.auto_panel = QWidget()
        auto_layout = QVBoxLayout(self.auto_panel)
        auto_layout.setContentsMargins(0, 0, 0, 0)

        explain = QLabel(
            "The white arrow shows the proposed incoming flow direction. This is a "
            "geometric heuristic (not a trained model): it ranks the mesh's principal "
            "axes by projected frontal area, on the reasoning that streamlined bodies "
            "are shaped to present their smallest cross-section to the flow. Please "
            "confirm it looks right before running."
        )
        explain.setObjectName("description")
        explain.setWordWrap(True)
        auto_layout.addWidget(explain)

        auto_layout.addWidget(QLabel("<b>Candidate orientations:</b>"))
        self.radio_buttons = []
        for i, cand in enumerate(self.candidates):
            rb = QRadioButton(f"{cand.label} — projected area {cand.projected_area:.4g}")
            rb.setChecked(i == 0)
            rb.toggled.connect(lambda checked, idx=i: self._on_candidate_selected(idx, checked))
            auto_layout.addWidget(rb)
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
                auto_layout.addWidget(warn)

        side_layout.addWidget(self.auto_panel)

        # ---- Pick-leading-edge panel ----
        self.pick_panel = QWidget()
        pick_layout = QVBoxLayout(self.pick_panel)
        pick_layout.setContentsMargins(0, 0, 0, 0)

        pick_explain = QLabel(
            "Click a point on the mesh below to mark the leading edge — the point "
            "that should face into the incoming flow. The flow direction is then "
            "computed as pointing from that point back through the mesh's centroid."
        )
        pick_explain.setObjectName("description")
        pick_explain.setWordWrap(True)
        pick_layout.addWidget(pick_explain)

        self.pick_status_label = QLabel("No point picked yet.")
        self.pick_status_label.setWordWrap(True)
        pick_layout.addWidget(self.pick_status_label)

        self.clear_pick_button = QPushButton("Clear Pick")
        self.clear_pick_button.clicked.connect(self._on_clear_pick)
        pick_layout.addWidget(self.clear_pick_button)

        side_layout.addWidget(self.pick_panel)
        self.pick_panel.setVisible(False)

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
    def _on_mode_toggled(self, auto_checked: bool):
        self.pick_mode = not auto_checked
        self.auto_panel.setVisible(auto_checked)
        self.pick_panel.setVisible(not auto_checked)
        self._update_preview()

    def _on_candidate_selected(self, idx, checked):
        if checked:
            self.selected_index = idx
            self._update_preview()

    def _on_reverse_toggled(self, checked):
        self.reversed_flow = checked
        self._update_preview()

    def _on_clear_pick(self):
        self.picked_vertex_index = None
        self.pick_status_label.setText("No point picked yet.")
        self._update_preview()

    # ------------------------------------------------------------------
    def _on_pick(self, event):
        if not self.pick_mode or event.artist is not self._scatter:
            return
        # these are indices into the (subsampled) scatter's own data array,
        # not into self._preview_mesh.vertices -- mapped to a real vertex
        # index only after picking the winner, below
        candidate_local = list(event.ind)
        if not candidate_local:
            return

        if len(candidate_local) == 1:
            best_local = candidate_local[0]
        else:
            # disambiguate occlusion ties by depth: nearer the camera wins
            # (empirically confirmed: proj_transform's zs is smaller = nearer)
            offsets = self._scatter._offsets3d
            proj = self.ax.get_proj()
            depths = []
            for idx in candidate_local:
                vx, vy, vz = offsets[0][idx], offsets[1][idx], offsets[2][idx]
                _, _, zs = proj3d.proj_transform(vx, vy, vz, proj)
                depths.append(zs)
            best_local = candidate_local[int(np.argmin(depths))]

        best_idx = int(self._pick_vertex_indices[best_local])
        self.picked_vertex_index = best_idx
        x, y, z = self._preview_mesh.vertices[best_idx]
        self.pick_status_label.setText(f"Picked vertex at ({x:.4g}, {y:.4g}, {z:.4g}).")
        self._update_preview()

    # ------------------------------------------------------------------
    def _current_auto_candidate(self):
        cand = self.candidates[self.selected_index]
        return self._maybe_reverse(cand)

    def _current_pick_candidate(self):
        if self.picked_vertex_index is None:
            return None
        vertex = self._preview_mesh.vertices[self.picked_vertex_index]
        flow_axis = self._preview_centroid - vertex
        norm = np.linalg.norm(flow_axis)
        if norm < 1e-12:
            return None
        flow_axis = flow_axis / norm
        cand = orient_mod.candidate_from_flow_axis(self._preview_mesh, flow_axis, "Picked leading edge")
        return self._maybe_reverse(cand)

    def _maybe_reverse(self, cand):
        if not self.reversed_flow:
            return cand
        return orient_mod.OrientationCandidate(
            label=cand.label,
            flow_axis=-cand.flow_axis,
            up_axis=cand.up_axis,
            span_axis=-cand.span_axis,
            projected_area=cand.projected_area,
            rank=cand.rank,
        )

    def _current_candidate(self):
        if self.pick_mode:
            return self._current_pick_candidate()
        return self._current_auto_candidate()

    # ------------------------------------------------------------------
    def _update_preview(self):
        cand = self._current_candidate()

        self.ax.cla()
        if cand is not None:
            display_mesh = orient_mod.apply_orientation(self._preview_mesh, cand)
        else:
            # pick mode, nothing picked yet: show the raw, centered mesh
            display_mesh = self._preview_mesh.copy()
            display_mesh.apply_translation(-self._preview_centroid)

        verts = display_mesh.vertices
        faces = display_mesh.faces
        mesh_polys = verts[faces]
        collection = Poly3DCollection(mesh_polys, alpha=0.7, facecolor="#60a5fa", edgecolor="#1e3a8a", linewidths=0.1)
        self.ax.add_collection3d(collection)

        extent = np.max(np.ptp(verts, axis=0)) * 0.6 + 1e-6
        center = verts.mean(axis=0)
        self.ax.set_xlim(center[0] - extent, center[0] + extent)
        self.ax.set_ylim(center[1] - extent, center[1] + extent)
        self.ax.set_zlim(center[2] - extent, center[2] + extent)

        if self.pick_mode:
            pick_pts = verts[self._pick_vertex_indices]
            self._scatter = self.ax.scatter(
                pick_pts[:, 0], pick_pts[:, 1], pick_pts[:, 2],
                s=6, color="#facc15", edgecolor="none",
                depthshade=False, picker=True, pickradius=8,
            )
            if self.picked_vertex_index is not None:
                pv = verts[self.picked_vertex_index]
                self.ax.scatter([pv[0]], [pv[1]], [pv[2]], s=90, color="#ef4444", edgecolor="black", depthshade=False)
        else:
            self._scatter = None

        if cand is not None:
            arrow_len = extent * 1.3
            arrow_start = np.array([center[0] - extent * 1.1, center[1], center[2]])
            self.ax.quiver(
                arrow_start[0], arrow_start[1], arrow_start[2],
                arrow_len, 0, 0,
                color="white", edgecolor="black", linewidth=1.5, arrow_length_ratio=0.15,
            )
            self.ax.text(arrow_start[0] - 0.05 * arrow_len, arrow_start[1], arrow_start[2], "flow →", color="black")

        self.ax.set_xlabel("x (flow)" if cand is not None else "x")
        self.ax.set_ylabel("y (up)" if cand is not None else "y")
        self.ax.set_zlabel("z (span)" if cand is not None else "z")
        self.ax.set_title(
            "Preview: proposed simulation orientation" if cand is not None
            else "Click a point on the mesh to mark the leading edge"
        )
        self.canvas.draw_idle()

    # ------------------------------------------------------------------
    def _on_accept(self):
        cand = self._current_candidate()
        if cand is None:
            cand = self.candidates[0]  # pick mode with nothing picked: fall back to the top auto-suggestion
        self.confirmed_mesh = orient_mod.apply_orientation(self.mesh, cand)
        self.accept()
