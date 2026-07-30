"""
Write the 3D solver's Cartesian grid + solid mask out as a real OpenFOAM
case: a polyMesh (points/faces/owner/neighbour/boundary) plus per-timestep
field files (U, p), openable in ParaView via a `.foam` placeholder file.

Unlike the VTK ImageData export (which just carries a solid/fluid scalar
flag on every point), this carves the immersed obstacle out of the mesh
entirely — solid cells are not part of the mesh at all, and every face
between a fluid cell and a solid cell becomes a real boundary face on an
"object" patch, exactly like a body-fitted OpenFOAM case would have.

Mesh topology never changes step to step, so it's written once under
constant/polyMesh/; each timestep only writes new U/p field files.
"""
from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Dict, List, Tuple

import numpy as np

OUTSIDE, SOLID, FLUID = 0, 1, 2

_HEADER_TEMPLATE = """FoamFile
{{
    version     2.0;
    format      ascii;
    class       {cls};
    object      {obj};
}}
"""


def _write_foam_file(path: str, cls: str, obj: str, body: str):
    with open(path, "w") as f:
        f.write("FoamFile\n{\n")
        f.write("    version     2.0;\n")
        f.write("    format      ascii;\n")
        f.write(f"    class       {cls};\n")
        f.write(f"    object      {obj};\n")
        f.write("}\n\n")
        f.write(body)


def _clip_mesh_to_box(mesh, xmin, xmax, ymin, ymax, zmin, zmax, tol):
    """Clip `mesh`'s own triangles to an axis-aligned box via a sequence
    of half-space cuts (no capping faces added), rather than a solid
    boolean difference. This needs no watertightness/volume on `mesh`,
    so thin or open surfaces (e.g. a wing skin with no modeled wall
    thickness) are preserved exactly instead of silently failing.
    Returns a list of (3, 3) vertex arrays, one per resulting triangle,
    with any face flush against one of the box's own cutting planes
    excluded (should be rare since nothing here introduces capping
    faces, but kept as a defensive check)."""
    from trimesh.intersections import slice_mesh_plane

    planes = (
        ([1.0, 0.0, 0.0], [xmin, 0.0, 0.0]), ([-1.0, 0.0, 0.0], [xmax, 0.0, 0.0]),
        ([0.0, 1.0, 0.0], [0.0, ymin, 0.0]), ([0.0, -1.0, 0.0], [0.0, ymax, 0.0]),
        ([0.0, 0.0, 1.0], [0.0, 0.0, zmin]), ([0.0, 0.0, -1.0], [0.0, 0.0, zmax]),
    )
    clipped = mesh
    for normal, origin in planes:
        if clipped is None or len(clipped.faces) == 0:
            return []
        try:
            clipped = slice_mesh_plane(clipped, plane_normal=normal, plane_origin=origin, cap=False)
        except Exception:
            return []

    faces = []
    verts = clipped.vertices
    for face in clipped.faces:
        v = verts[face]
        on_box_plane = (
            np.all(np.abs(v[:, 0] - xmin) < tol) or np.all(np.abs(v[:, 0] - xmax) < tol)
            or np.all(np.abs(v[:, 1] - ymin) < tol) or np.all(np.abs(v[:, 1] - ymax) < tol)
            or np.all(np.abs(v[:, 2] - zmin) < tol) or np.all(np.abs(v[:, 2] - zmax) < tol)
        )
        if not on_box_plane:
            faces.append(v)
    return faces


@dataclass
class _Patch:
    name: str
    type: str
    start_face: int = 0
    n_faces: int = 0


class OpenFoamCaseWriter:
    def __init__(
        self, case_dir: str, nx: int, ny: int, nz: int, dx: float, dy: float, dz: float, solid_mask: np.ndarray,
        surface_mesh=None,
    ):
        self.case_dir = case_dir
        self.nx, self.ny, self.nz = nx, ny, nz
        self.dx, self.dy, self.dz = dx, dy, dz
        self.solid_mask = solid_mask  # shape (nx, ny, nz), True = solid
        self.surface_mesh = surface_mesh  # optional trimesh.Trimesh, same coord frame as the grid
        self.n_cells_smoothed = 0
        self.n_cells_fallback = 0
        self._extra_points: List[np.ndarray] = []

        os.makedirs(case_dir, exist_ok=True)
        os.makedirs(os.path.join(case_dir, "constant", "polyMesh"), exist_ok=True)
        os.makedirs(os.path.join(case_dir, "system"), exist_ok=True)

        self._build_topology()
        self._write_points()
        self._write_faces_owner_neighbour_boundary()
        self._write_system_stubs()
        self._write_foam_placeholder()

    # ------------------------------------------------------------------
    def _point_id(self, i, j, k):
        return i + j * (self.nx + 1) + k * (self.nx + 1) * (self.ny + 1)

    def _build_topology(self):
        nx, ny, nz = self.nx, self.ny, self.nz

        code = np.where(self.solid_mask, SOLID, FLUID)  # (nx, ny, nz)
        cell_id = np.full((nx, ny, nz), -1, dtype=np.int64)
        fluid_positions = np.argwhere(code == FLUID)
        cell_id[code == FLUID] = np.arange(fluid_positions.shape[0])
        self.n_cells = int((code == FLUID).sum())
        self._cell_id = cell_id
        self._cell_ijk = fluid_positions  # cell_ijk[cell_id] -> (i, j, k)

        internal_faces: List[Tuple[int, int, Tuple[int, int, int, int]]] = []  # (owner, neighbour, points)
        boundary_faces: Dict[str, List[Tuple[int, Tuple[int, int, int, int]]]] = {
            "inlet": [], "outlet": [], "lowerWall": [], "upperWall": [],
            "frontWall": [], "backWall": [], "object": [],
        }

        def pad_code(axis):
            shape = list(code.shape)
            shape[axis] += 2
            padded = np.full(shape, OUTSIDE, dtype=code.dtype)
            slicer = [slice(None)] * 3
            slicer[axis] = slice(1, -1)
            padded[tuple(slicer)] = code
            return padded

        # ---- X-direction faces ----
        px = pad_code(0)  # shape (nx+2, ny, nz); real cell i -> px[i+1]
        for i in range(nx + 1):
            minus = px[i, :, :]
            plus = px[i + 1, :, :]
            for j in range(ny):
                for k in range(nz):
                    m, p = minus[j, k], plus[j, k]
                    pts = (
                        self._point_id(i, j, k), self._point_id(i, j + 1, k),
                        self._point_id(i, j + 1, k + 1), self._point_id(i, j, k + 1),
                    )  # verified (scratch_verify_normals.py): normal points in +x
                    if m == FLUID and p == FLUID:
                        internal_faces.append((cell_id[i - 1, j, k], cell_id[i, j, k], pts))
                    elif m == FLUID and p == OUTSIDE:
                        boundary_faces["outlet"].append((cell_id[i - 1, j, k], pts))
                    elif m == OUTSIDE and p == FLUID:
                        boundary_faces["inlet"].append((cell_id[i, j, k], tuple(reversed(pts))))
                    elif m == FLUID and p == SOLID:
                        boundary_faces["object"].append((cell_id[i - 1, j, k], pts))
                    elif m == SOLID and p == FLUID:
                        boundary_faces["object"].append((cell_id[i, j, k], tuple(reversed(pts))))

        # ---- Y-direction faces ----
        py = pad_code(1)  # shape (nx, ny+2, nz); real cell j -> py[:, j+1, :]
        for j in range(ny + 1):
            minus = py[:, j, :]
            plus = py[:, j + 1, :]
            for i in range(nx):
                for k in range(nz):
                    m, p = minus[i, k], plus[i, k]
                    pts = (
                        self._point_id(i, j, k), self._point_id(i, j, k + 1),
                        self._point_id(i + 1, j, k + 1), self._point_id(i + 1, j, k),
                    )  # verified (scratch_verify_normals.py): normal points in +y
                    if m == FLUID and p == FLUID:
                        internal_faces.append((cell_id[i, j - 1, k], cell_id[i, j, k], pts))
                    elif m == FLUID and p == OUTSIDE:
                        boundary_faces["upperWall"].append((cell_id[i, j - 1, k], pts))
                    elif m == OUTSIDE and p == FLUID:
                        boundary_faces["lowerWall"].append((cell_id[i, j, k], tuple(reversed(pts))))
                    elif m == FLUID and p == SOLID:
                        boundary_faces["object"].append((cell_id[i, j - 1, k], pts))
                    elif m == SOLID and p == FLUID:
                        boundary_faces["object"].append((cell_id[i, j, k], tuple(reversed(pts))))

        # ---- Z-direction faces ----
        pz = pad_code(2)  # shape (nx, ny, nz+2); real cell k -> pz[:, :, k+1]
        for k in range(nz + 1):
            minus = pz[:, :, k]
            plus = pz[:, :, k + 1]
            for i in range(nx):
                for j in range(ny):
                    m, p = minus[i, j], plus[i, j]
                    pts = (
                        self._point_id(i, j, k), self._point_id(i + 1, j, k),
                        self._point_id(i + 1, j + 1, k), self._point_id(i, j + 1, k),
                    )  # verified (scratch_verify_normals.py): normal points in +z
                    if m == FLUID and p == FLUID:
                        internal_faces.append((cell_id[i, j, k - 1], cell_id[i, j, k], pts))
                    elif m == FLUID and p == OUTSIDE:
                        boundary_faces["backWall"].append((cell_id[i, j, k - 1], pts))
                    elif m == OUTSIDE and p == FLUID:
                        boundary_faces["frontWall"].append((cell_id[i, j, k], tuple(reversed(pts))))
                    elif m == FLUID and p == SOLID:
                        boundary_faces["object"].append((cell_id[i, j, k - 1], pts))
                    elif m == SOLID and p == FLUID:
                        boundary_faces["object"].append((cell_id[i, j, k], tuple(reversed(pts))))

        # sort internal faces by (owner, neighbour) as OpenFOAM expects
        internal_faces.sort(key=lambda t: (t[0], t[1]))

        self._faces: List[Tuple[int, int]] = []  # (owner, neighbour_or_-1)
        self._face_points: List[Tuple[int, int, int, int]] = []

        for owner, neighbour, pts in internal_faces:
            self._faces.append((owner, neighbour))
            self._face_points.append(pts)
        self.n_internal_faces = len(self._faces)

        if self.surface_mesh is not None and boundary_faces["object"]:
            boundary_faces["object"] = self._refine_object_patch(boundary_faces["object"])

        self._patches: List[_Patch] = []
        patch_types = {
            "inlet": "patch", "outlet": "patch",
            "lowerWall": "wall", "upperWall": "wall",
            "frontWall": "wall", "backWall": "wall",
            "object": "wall",
        }
        for name, faces in boundary_faces.items():
            if not faces:
                continue
            start = len(self._faces)
            for owner, pts in faces:
                self._faces.append((owner, -1))
                self._face_points.append(pts)
            self._patches.append(_Patch(name=name, type=patch_types[name], start_face=start, n_faces=len(faces)))

    def _refine_object_patch(self, object_faces):
        """Replace the flat, staircased voxel faces on the object patch
        with the actual uploaded surface geometry: for each boundary
        cell, clip the real uploaded mesh's own triangles directly
        against that cell's box (see `_clip_mesh_to_box`) — no
        watertightness/volume requirement, so thin or open features
        (e.g. a wing skin) are preserved instead of silently degrading
        to a flat quad. Falls back to a per-cell solid boolean
        difference (only meaningful when the mesh is a proper volume —
        it's a guaranteed failure otherwise), then to the original flat
        quad as a last resort — geometry-only: cell classification,
        cell count, and field values are completely unaffected."""
        import trimesh

        by_owner: Dict[int, List[Tuple[int, int, int, int]]] = {}
        for owner, pts in object_faces:
            by_owner.setdefault(owner, []).append(pts)

        dx, dy, dz = self.dx, self.dy, self.dz
        tol = 1e-6 * min(dx, dy, dz)
        refined: List[Tuple[int, Tuple[int, ...]]] = []
        next_point_id = (self.nx + 1) * (self.ny + 1) * (self.nz + 1) + len(self._extra_points)
        mesh_is_volume = bool(self.surface_mesh.is_volume)

        for owner, quads in by_owner.items():
            i, j, k = self._cell_ijk[owner]
            cx, cy, cz = (i + 0.5) * dx, (j + 0.5) * dy, (k + 0.5) * dz
            xmin, ymin, zmin = cx - dx / 2, cy - dy / 2, cz - dz / 2
            xmax, ymax, zmax = cx + dx / 2, cy + dy / 2, cz + dz / 2

            surface_faces = _clip_mesh_to_box(self.surface_mesh, xmin, xmax, ymin, ymax, zmin, zmax, tol)

            if not surface_faces and mesh_is_volume:
                box = trimesh.creation.box(extents=[dx, dy, dz])
                box.apply_translation([cx, cy, cz])
                try:
                    clipped = trimesh.boolean.difference([box, self.surface_mesh], engine="manifold")
                except Exception:
                    clipped = None
                if clipped is not None and len(clipped.faces) > 0:
                    verts = clipped.vertices
                    for face in clipped.faces:
                        v = verts[face]
                        on_box_plane = (
                            np.all(np.abs(v[:, 0] - xmin) < tol) or np.all(np.abs(v[:, 0] - xmax) < tol)
                            or np.all(np.abs(v[:, 1] - ymin) < tol) or np.all(np.abs(v[:, 1] - ymax) < tol)
                            or np.all(np.abs(v[:, 2] - zmin) < tol) or np.all(np.abs(v[:, 2] - zmax) < tol)
                        )
                        if not on_box_plane:
                            surface_faces.append(v)

            if not surface_faces:
                self.n_cells_fallback += 1
                refined.extend((owner, pts) for pts in quads)
                continue

            self.n_cells_smoothed += 1
            center = np.array([cx, cy, cz])
            for tri in surface_faces:
                point_ids = list(range(next_point_id, next_point_id + 3))
                self._extra_points.extend(tri)
                next_point_id += 3
                normal = np.cross(tri[1] - tri[0], tri[2] - tri[0])
                if np.dot(normal, tri[0] - center) < 0:
                    point_ids = [point_ids[0], point_ids[2], point_ids[1]]
                refined.append((owner, tuple(point_ids)))

        return refined

    # ------------------------------------------------------------------
    def _write_points(self):
        nx, ny, nz = self.nx, self.ny, self.nz
        xs = np.arange(nx + 1) * self.dx
        ys = np.arange(ny + 1) * self.dy
        zs = np.arange(nz + 1) * self.dz
        n_points = (nx + 1) * (ny + 1) * (nz + 1)
        coords = np.zeros((n_points, 3))
        for k in range(nz + 1):
            for j in range(ny + 1):
                base = j * (nx + 1) + k * (nx + 1) * (ny + 1)
                idx = np.arange(nx + 1) + base
                coords[idx, 0] = xs
                coords[idx, 1] = ys[j]
                coords[idx, 2] = zs[k]

        all_coords = np.vstack([coords, np.array(self._extra_points)]) if self._extra_points else coords

        lines = [f"\n{len(all_coords)}", "("]
        lines.extend(f"({x:.8g} {y:.8g} {z:.8g})" for x, y, z in all_coords)
        lines.append(")\n")
        _write_foam_file(
            os.path.join(self.case_dir, "constant", "polyMesh", "points"),
            "vectorField", "points", "\n".join(lines),
        )

    def _write_faces_owner_neighbour_boundary(self):
        polymesh = os.path.join(self.case_dir, "constant", "polyMesh")

        face_lines = [f"\n{len(self._face_points)}", "("]
        face_lines.extend(f"{len(p)}({' '.join(str(x) for x in p)})" for p in self._face_points)
        face_lines.append(")\n")
        _write_foam_file(os.path.join(polymesh, "faces"), "faceList", "faces", "\n".join(face_lines))

        owner_lines = [f"\n{len(self._faces)}", "("]
        owner_lines.extend(str(o) for o, _ in self._faces)
        owner_lines.append(")\n")
        _write_foam_file(os.path.join(polymesh, "owner"), "labelList", "owner", "\n".join(owner_lines))

        neighbour_lines = [f"\n{self.n_internal_faces}", "("]
        neighbour_lines.extend(str(n) for o, n in self._faces[: self.n_internal_faces])
        neighbour_lines.append(")\n")
        _write_foam_file(os.path.join(polymesh, "neighbour"), "labelList", "neighbour", "\n".join(neighbour_lines))

        boundary_lines = [f"\n{len(self._patches)}", "("]
        for p in self._patches:
            boundary_lines.append(f"    {p.name}")
            boundary_lines.append("    {")
            boundary_lines.append(f"        type            {p.type};")
            boundary_lines.append(f"        nFaces          {p.n_faces};")
            boundary_lines.append(f"        startFace       {p.start_face};")
            boundary_lines.append("    }")
        boundary_lines.append(")\n")
        _write_foam_file(os.path.join(polymesh, "boundary"), "polyBoundaryMesh", "boundary", "\n".join(boundary_lines))

    def _write_system_stubs(self):
        system = os.path.join(self.case_dir, "system")
        _write_foam_file(
            os.path.join(system, "controlDict"), "dictionary", "controlDict",
            "application     cfdStudio;\nstartFrom       startTime;\nstartTime       0;\n"
            "stopAt          endTime;\nendTime         1;\ndeltaT          1;\nwriteControl    timeStep;\n"
            "writeInterval   1;\n",
        )
        _write_foam_file(os.path.join(system, "fvSchemes"), "dictionary", "fvSchemes", "// generated by CFD Studio\n")
        _write_foam_file(os.path.join(system, "fvSolution"), "dictionary", "fvSolution", "// generated by CFD Studio\n")

    def _write_foam_placeholder(self):
        case_name = os.path.basename(os.path.normpath(self.case_dir)) or "case"
        open(os.path.join(self.case_dir, f"{case_name}.foam"), "w").close()

    @property
    def foam_path(self) -> str:
        case_name = os.path.basename(os.path.normpath(self.case_dir)) or "case"
        return os.path.join(self.case_dir, f"{case_name}.foam")

    # ------------------------------------------------------------------
    def write_timestep(self, time: float, fields: Dict[str, np.ndarray]):
        time_name = "0" if time == 0 else f"{time:.6g}"
        time_dir = os.path.join(self.case_dir, time_name)
        os.makedirs(time_dir, exist_ok=True)

        fluid_mask = ~self.solid_mask
        u = fields["velocity_u"][fluid_mask]
        v = fields["velocity_v"][fluid_mask]
        w = fields["velocity_w"][fluid_mask]
        p = fields["pressure"][fluid_mask]
        # our fluid ordering (argwhere over (nx,ny,nz) in C-order) must match
        # self._cell_id's assignment order, both built the same way, so a
        # plain boolean-mask select on the dense arrays lines up correctly.

        u_lines = [f"\ndimensions      [0 1 -1 0 0 0 0];", "internalField   nonuniform List<vector>", f"{len(u)}", "("]
        u_lines.extend(f"({a:.6e} {b:.6e} {c:.6e})" for a, b, c in zip(u, v, w))
        u_lines.append(");\n")
        u_lines.append(self._boundary_field_vector())
        _write_foam_file(os.path.join(time_dir, "U"), "volVectorField", "U", "\n".join(u_lines))

        p_lines = [f"\ndimensions      [0 2 -2 0 0 0 0];", "internalField   nonuniform List<scalar>", f"{len(p)}", "("]
        p_lines.extend(f"{val:.6e}" for val in p)
        p_lines.append(");\n")
        p_lines.append(self._boundary_field_scalar())
        _write_foam_file(os.path.join(time_dir, "p"), "volScalarField", "p", "\n".join(p_lines))

    def _boundary_field_vector(self) -> str:
        lines = ["boundaryField", "{"]
        for p in self._patches:
            lines.append(f"    {p.name}")
            lines.append("    {")
            if p.name == "inlet":
                lines.append("        type            fixedValue;")
                lines.append("        value           uniform (1 0 0);")
            elif p.name == "outlet":
                lines.append("        type            zeroGradient;")
            elif p.name == "object":
                lines.append("        type            noSlip;")
            else:
                lines.append("        type            slip;")
            lines.append("    }")
        lines.append("}\n")
        return "\n".join(lines)

    def _boundary_field_scalar(self) -> str:
        lines = ["boundaryField", "{"]
        for p in self._patches:
            lines.append(f"    {p.name}")
            lines.append("    {")
            if p.name == "outlet":
                lines.append("        type            fixedValue;")
                lines.append("        value           uniform 0;")
            else:
                lines.append("        type            zeroGradient;")
            lines.append("    }")
        lines.append("}\n")
        return "\n".join(lines)
