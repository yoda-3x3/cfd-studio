"""
Geometric heuristic for suggesting how an uploaded mesh should be oriented
relative to the wind-tunnel's flow direction (+x).

This is NOT a trained model — it's an explainable geometric analysis: the
flow direction on a streamlined body is almost always the axis with the
smallest projected (silhouette) frontal area, since minimizing frontal
area is the basic shaping principle behind why aerodynamic/hydrodynamic
bodies look the way they do (cars, wings analyzed chord-first, fish,
buildings analyzed through their narrow face, etc). We rank the mesh's
three principal inertia axes by that projected area and suggest the
smallest as the flow axis, while always surfacing the numbers so the user
can see why and override it.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import List

import numpy as np
import trimesh
from scipy.spatial import ConvexHull


@dataclass
class OrientationCandidate:
    label: str
    flow_axis: np.ndarray  # unit vector, becomes +x
    up_axis: np.ndarray  # unit vector, becomes +y
    span_axis: np.ndarray  # unit vector, becomes +z
    projected_area: float
    rank: int


def _projected_silhouette_area(vertices: np.ndarray, axis: np.ndarray) -> float:
    """Convex-hull area of the vertex cloud projected onto the plane
    perpendicular to `axis` — a fast, reasonable proxy for frontal area."""
    axis = axis / np.linalg.norm(axis)
    # any vector not parallel to axis, to build a perpendicular basis
    helper = np.array([1.0, 0.0, 0.0]) if abs(axis[0]) < 0.9 else np.array([0.0, 1.0, 0.0])
    u = np.cross(axis, helper)
    u /= np.linalg.norm(u)
    v = np.cross(axis, u)
    proj = np.stack([vertices @ u, vertices @ v], axis=1)
    try:
        hull = ConvexHull(proj)
        return float(hull.volume)  # 'volume' of a 2D hull is its area
    except Exception:
        # degenerate (near-planar/collinear) projection
        return float(np.ptp(proj[:, 0]) * np.ptp(proj[:, 1]))


def _principal_axes(mesh: "trimesh.Trimesh") -> np.ndarray:
    """Three orthonormal candidate axes for the mesh, from its principal
    moments of inertia when available, falling back to vertex PCA."""
    try:
        vectors = np.array(mesh.principal_inertia_vectors)
        if vectors.shape == (3, 3) and np.all(np.isfinite(vectors)):
            return vectors
    except Exception:
        pass
    centered = mesh.vertices - mesh.vertices.mean(axis=0)
    cov = np.cov(centered.T)
    _, eigvecs = np.linalg.eigh(cov)
    return eigvecs.T  # rows are axes


def candidate_from_flow_axis(
    mesh: "trimesh.Trimesh", flow_axis: np.ndarray, label: str, rank: int = 0,
    reference_axes: "np.ndarray | None" = None,
) -> OrientationCandidate:
    """Build a full OrientationCandidate (orthonormal up/span basis) from
    an arbitrary flow axis — not necessarily one of the mesh's principal
    axes. Picks an up-axis from `reference_axes` (the mesh's principal
    axes, by default) that isn't near-parallel to `flow_axis`, then
    Gram-Schmidt orthogonalizes; falls back to an arbitrary perpendicular
    basis if every reference axis is too close to parallel."""
    flow_axis = flow_axis / np.linalg.norm(flow_axis)
    axes = _principal_axes(mesh) if reference_axes is None else reference_axes

    remaining = [a for a in axes if not np.allclose(np.abs(a @ flow_axis), 1.0, atol=1e-6)]
    if len(remaining) < 2:
        # numerical fallback: build an arbitrary perpendicular basis
        helper = np.array([1.0, 0.0, 0.0]) if abs(flow_axis[0]) < 0.9 else np.array([0.0, 1.0, 0.0])
        up_axis = np.cross(flow_axis, helper)
        up_axis /= np.linalg.norm(up_axis)
    else:
        up_axis = remaining[0] / np.linalg.norm(remaining[0])
    span_axis = np.cross(flow_axis, up_axis)
    span_axis /= np.linalg.norm(span_axis)
    # re-orthogonalize up_axis to guarantee a clean right-handed basis
    up_axis = np.cross(span_axis, flow_axis)
    up_axis /= np.linalg.norm(up_axis)

    return OrientationCandidate(
        label=label,
        flow_axis=flow_axis,
        up_axis=up_axis,
        span_axis=span_axis,
        projected_area=_projected_silhouette_area(mesh.vertices, flow_axis),
        rank=rank,
    )


def analyze_orientation(mesh: "trimesh.Trimesh") -> List[OrientationCandidate]:
    """Return the 3 candidate flow-axis orientations, ranked smallest
    projected area first (index 0 = suggested)."""
    axes = _principal_axes(mesh)
    vertices = mesh.vertices

    scored = []
    for i in range(3):
        axis = axes[i] / np.linalg.norm(axes[i])
        area = _projected_silhouette_area(vertices, axis)
        scored.append((area, axis))
    scored.sort(key=lambda t: t[0])

    candidates = []
    for rank, (area, flow_axis) in enumerate(scored):
        label = f"Option {rank + 1}" + (" (suggested)" if rank == 0 else "")
        candidates.append(candidate_from_flow_axis(mesh, flow_axis, label, rank=rank, reference_axes=axes))
    return candidates


def rotation_matrix_for(candidate: OrientationCandidate) -> np.ndarray:
    """4x4 homogeneous transform mapping the mesh so candidate.flow_axis
    becomes +x, candidate.up_axis becomes +y, candidate.span_axis becomes +z."""
    basis = np.stack([candidate.flow_axis, candidate.up_axis, candidate.span_axis], axis=0)
    if np.linalg.det(basis) < 0:
        basis[2] = -basis[2]
    transform = np.eye(4)
    transform[:3, :3] = basis
    return transform


def apply_orientation(mesh: "trimesh.Trimesh", candidate: OrientationCandidate) -> "trimesh.Trimesh":
    oriented = mesh.copy()
    oriented.apply_translation(-oriented.vertices.mean(axis=0))
    oriented.apply_transform(rotation_matrix_for(candidate))
    return oriented
