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


def prepare_internal_geometry(
    mesh: "trimesh.Trimesh",
    target_max_extent: float = 1.0,
    inflow_gap: float = 0.0,
    wake_gap: float = 0.0,
) -> PreparedGeometry:
    """Center, normalize, and place a pipe/duct mesh for INTERNAL flow.
    Unlike `prepare_geometry`'s open-tunnel domain, there is no lateral
    clearance here: the pipe wall itself defines the domain's lateral
    (y, z) extent, so the grid hugs the mesh's own bounding box exactly.
    `inflow_gap`/`wake_gap` still allow a small amount of straight
    entrance/exit length before the inlet/outlet planes if desired, but
    default to 0 since the BCs can be applied directly at the pipe's own
    end faces."""
    mesh = mesh.copy()
    original_extents = mesh.extents.copy()

    centroid = mesh.bounding_box.centroid
    mesh.apply_translation(-centroid)

    scale = target_max_extent / max(mesh.extents)
    mesh.apply_scale(scale)

    ex, ey, ez = mesh.extents
    L = max(mesh.extents)
    Lx = ex + (inflow_gap + wake_gap) * L
    Ly = ey
    Lz = ez

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


def _cap_open_boundaries(mesh: "trimesh.Trimesh") -> "trimesh.Trimesh":
    """Cap each open boundary loop of `mesh` with a fan triangulation
    from the loop's own first vertex, turning an open shell (e.g. a
    pipe/duct wall surface with no modeled thickness — a single open
    tube with two open end loops) into a closed solid bounding its
    interior volume. Assumes boundary loops are reasonably convex/planar,
    true for pipe end rims — `trimesh.repair.fill_holes` is the general-
    purpose alternative but proved unreliable in practice for this exact
    shape (many-sided circular loops), so a small custom capper is used
    instead. Winding is chosen to point away from the mesh's centroid
    (outward), matching the convention used elsewhere in this codebase
    (see `io/openfoam_writer.py`'s `_refine_object_patch`)."""
    import trimesh

    if mesh.is_watertight:
        return mesh
    try:
        outline = mesh.outline()
    except Exception:
        return mesh
    entities = getattr(outline, "entities", [])
    if len(entities) == 0:
        return mesh

    verts = mesh.vertices
    centroid = verts.mean(axis=0)
    extra_faces = []
    for entity in entities:
        loop = [int(p) for p in entity.points]
        if len(loop) > 1 and loop[0] == loop[-1]:
            loop = loop[:-1]
        if len(loop) < 3:
            continue
        anchor = loop[0]
        for i in range(1, len(loop) - 1):
            tri = [anchor, loop[i], loop[i + 1]]
            tv = verts[tri]
            normal = np.cross(tv[1] - tv[0], tv[2] - tv[0])
            if np.dot(normal, tv.mean(axis=0) - centroid) < 0:
                tri = [tri[0], tri[2], tri[1]]
            extra_faces.append(tri)

    if not extra_faces:
        return mesh
    combined_faces = np.vstack([mesh.faces, np.array(extra_faces)])
    return trimesh.Trimesh(vertices=mesh.vertices, faces=combined_faces, process=True)


def voxelize_internal_to_grid(mesh: "trimesh.Trimesh", nx: int, ny: int, nz: int, Lx: float, Ly: float, Lz: float) -> np.ndarray:
    """Voxelize a pipe/duct mesh for INTERNAL flow: the mesh's own hollow
    interior (its lumen) becomes the fluid region, and the wall material
    (plus anything genuinely outside the whole part) becomes solid — the
    inverse topology from `voxelize_to_grid`'s external-object convention.

    These two conventions have opposite mask polarity (`contains()` means
    "is wall material" for (a) vs. "is lumen" for (b)), so they can't both
    be handled by the same fixed inversion rule — instead, try the (a)
    interpretation first, and fall back to (b) if it finds nothing:

    1. Voxelize `inside_mask = mesh.contains(points)`. Run a connected-
       component flood fill on its complement. Any component that
       doesn't touch the domain's lateral (y, z) boundary is an enclosed
       cavity — under interpretation (a), that's the lumen.
    2. If that finds no enclosed cavity at all (there's no separate wall
       shell to flood-fill around — true whenever the mesh already
       directly bounds the lumen, whether it started that way or was
       just capped into that shape by `_cap_open_boundaries`), fall back
       to interpretation (b): `inside_mask` itself is the fluid region.

    Only the lateral (y, z) domain faces count as "true exterior" when
    touched in step 1 — the pipe is open at its two x-axis ends by
    design (that's the inlet/outlet), so an enclosed lumen legitimately
    reaches the x=0/x=Lx faces and must not be disqualified for it."""
    from scipy.ndimage import label

    if not mesh.is_watertight:
        mesh = _cap_open_boundaries(mesh)

    x = (np.arange(nx) + 0.5) * (Lx / nx)
    y = (np.arange(ny) + 0.5) * (Ly / ny)
    z = (np.arange(nz) + 0.5) * (Lz / nz)
    X, Y, Z = np.meshgrid(x, y, z, indexing="ij")
    points = np.stack([X.ravel(), Y.ravel(), Z.ravel()], axis=1)
    inside_mask = mesh.contains(points).reshape(nx, ny, nz)

    complement = ~inside_mask
    labeled, _ = label(complement)

    boundary_labels = set()
    boundary_labels.update(np.unique(labeled[:, 0, :]).tolist())
    boundary_labels.update(np.unique(labeled[:, -1, :]).tolist())
    boundary_labels.update(np.unique(labeled[:, :, 0]).tolist())
    boundary_labels.update(np.unique(labeled[:, :, -1]).tolist())
    boundary_labels.discard(0)  # label 0 is `inside_mask` itself, not a complement component

    fluid_mask = complement & ~np.isin(labeled, list(boundary_labels))
    if not fluid_mask.any():
        fluid_mask = inside_mask  # no enclosed wall-shell cavity: the mesh directly bounds the lumen

    if not fluid_mask.any():
        raise ValueError(
            "No enclosed interior volume (lumen) found in the uploaded geometry — "
            "check that the file represents a hollow duct/pipe."
        )
    return ~fluid_mask
