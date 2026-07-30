"""
Minimal-dependency writer for VTK XML ImageData (.vti) time steps plus a
.pvd time-series collection file, so results can be opened directly in
ParaView without needing the (heavy) `vtk` Python package.
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from typing import Dict, List, Tuple

import numpy as np


def _format_array(arr: np.ndarray) -> str:
    return " ".join(f"{v:.6e}" for v in arr.ravel())


def _write_vti(
    filepath: str,
    nx: int,
    ny: int,
    dx: float,
    dy: float,
    scalars: Dict[str, np.ndarray],
    vector_name: str,
    u: np.ndarray,
    v: np.ndarray,
):
    """Write one ImageData (.vti) file. Arrays are shape (ny, nx)."""
    n_points = nx * ny
    velocity3 = np.zeros((n_points, 3), dtype=np.float64)
    velocity3[:, 0] = u.ravel()
    velocity3[:, 1] = v.ravel()

    lines = []
    lines.append('<?xml version="1.0"?>')
    lines.append('<VTKFile type="ImageData" version="0.1" byte_order="LittleEndian">')
    lines.append(
        f'  <ImageData WholeExtent="0 {nx - 1} 0 {ny - 1} 0 0" '
        f'Origin="0 0 0" Spacing="{dx:.8e} {dy:.8e} 1.0">'
    )
    lines.append(f'    <Piece Extent="0 {nx - 1} 0 {ny - 1} 0 0">')
    first_scalar = next(iter(scalars.keys())) if scalars else ""
    lines.append(f'      <PointData Scalars="{first_scalar}" Vectors="{vector_name}">')
    for name, arr in scalars.items():
        lines.append(f'        <DataArray type="Float32" Name="{name}" format="ascii">')
        lines.append("          " + _format_array(arr))
        lines.append("        </DataArray>")
    lines.append(
        f'        <DataArray type="Float32" Name="{vector_name}" '
        f'NumberOfComponents="3" format="ascii">'
    )
    lines.append("          " + _format_array(velocity3))
    lines.append("        </DataArray>")
    lines.append("      </PointData>")
    lines.append("      <CellData/>")
    lines.append("    </Piece>")
    lines.append("  </ImageData>")
    lines.append("</VTKFile>")

    with open(filepath, "w") as f:
        f.write("\n".join(lines))


@dataclass
class VTKSeriesWriter:
    output_dir: str
    case_name: str = "case"
    _entries: List[Tuple[float, str]] = field(default_factory=list)

    def __post_init__(self):
        os.makedirs(self.output_dir, exist_ok=True)

    @property
    def pvd_path(self) -> str:
        return os.path.join(self.output_dir, f"{self.case_name}.pvd")

    def write_step(self, step_index: int, time: float, nx: int, ny: int, dx: float, dy: float, fields: Dict[str, np.ndarray]):
        fname = f"{self.case_name}_{step_index:05d}.vti"
        fpath = os.path.join(self.output_dir, fname)
        scalars = {
            "velocity_magnitude": fields["velocity_magnitude"].astype(np.float32),
            "vorticity": fields["vorticity"].astype(np.float32),
            "streamfunction": fields["streamfunction"].astype(np.float32),
            "obstacle": fields["obstacle"].astype(np.float32),
        }
        _write_vti(
            fpath, nx, ny, dx, dy, scalars,
            vector_name="velocity",
            u=fields["velocity_u"].astype(np.float32),
            v=fields["velocity_v"].astype(np.float32),
        )
        self._entries.append((time, fname))
        self._write_pvd()
        return fpath

    def _write_pvd(self):
        lines = ['<?xml version="1.0"?>', '<VTKFile type="Collection" version="0.1">', "  <Collection>"]
        for t, fname in self._entries:
            lines.append(f'    <DataSet timestep="{t:.8e}" group="" part="0" file="{fname}"/>')
        lines.append("  </Collection>")
        lines.append("</VTKFile>")
        with open(self.pvd_path, "w") as f:
            f.write("\n".join(lines))

    def clear(self):
        for _, fname in self._entries:
            fpath = os.path.join(self.output_dir, fname)
            if os.path.exists(fpath):
                os.remove(fpath)
        if os.path.exists(self.pvd_path):
            os.remove(self.pvd_path)
        self._entries = []
