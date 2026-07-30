"""
Load an uploaded 3D mesh file (STL/OBJ/PLY/...) and prepare it for the 3D
solver: normalize its size, position it inside a virtual wind-tunnel
domain (inflow gap upstream, wake gap downstream, lateral clearance on
the sides), and voxelize it onto the solver's Cartesian grid as a solid
mask via point-in-mesh containment queries.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import trimesh


@dataclass
class PreparedGeometry:
    mesh: "trimesh.Trimesh"
    Lx: float
    Ly: float
    Lz: float
    is_watertight: bool
    original_extents: np.ndarray


def load_mesh(path: str) -> "trimesh.Trimesh":
    loaded = trimesh.load(path, force="mesh")
    if isinstance(loaded, trimesh.Scene):
        loaded = trimesh.util.concatenate(list(loaded.geometry.values()))
    if not isinstance(loaded, trimesh.Trimesh):
        raise ValueError(f"Could not interpret file as a triangle mesh: {path}")
    if loaded.vertices.shape[0] == 0 or loaded.faces.shape[0] == 0:
        raise ValueError(f"Mesh file has no geometry: {path}")
    return loaded


def prepare_geometry(
    mesh: "trimesh.Trimesh",
    target_max_extent: float = 1.0,
    inflow_gap: float = 1.5,
    wake_gap: float = 4.0,
    lateral_gap: float = 1.5,
) -> PreparedGeometry:
    """Center, normalize, and place the mesh inside a wind-tunnel-style
    domain. The Reynolds number for the resulting simulation is defined
    using `target_max_extent` as the characteristic length, since an
    uploaded file's original units are otherwise meaningless."""
    mesh = mesh.copy()
    original_extents = mesh.extents.copy()

    centroid = mesh.bounding_box.centroid
    mesh.apply_translation(-centroid)

    scale = target_max_extent / max(mesh.extents)
    mesh.apply_scale(scale)

    L = max(mesh.extents)
    ex, ey, ez = mesh.extents
    Lx = inflow_gap * L + ex + wake_gap * L
    Ly = ey + 2 * lateral_gap * L
    Lz = ez + 2 * lateral_gap * L

    bounds = mesh.bounds
    shift = np.array(
        [
            inflow_gap * L - bounds[0][0],
            Ly / 2 - (bounds[0][1] + bounds[1][1]) / 2,
            Lz / 2 - (bounds[0][2] + bounds[1][2]) / 2,
        ]
    )
    mesh.apply_translation(shift)

    return PreparedGeometry(
        mesh=mesh,
        Lx=Lx,
        Ly=Ly,
        Lz=Lz,
        is_watertight=bool(mesh.is_watertight),
        original_extents=original_extents,
    )


def voxelize_to_grid(mesh: "trimesh.Trimesh", nx: int, ny: int, nz: int, Lx: float, Ly: float, Lz: float) -> np.ndarray:
    x = (np.arange(nx) + 0.5) * (Lx / nx)
    y = (np.arange(ny) + 0.5) * (Ly / ny)
    z = (np.arange(nz) + 0.5) * (Lz / nz)
    X, Y, Z = np.meshgrid(x, y, z, indexing="ij")
    points = np.stack([X.ravel(), Y.ravel(), Z.ravel()], axis=1)
    inside = mesh.contains(points)
    return inside.reshape(nx, ny, nz)
