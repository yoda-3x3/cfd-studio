#pragma once

#include <cstdint>
#include <vector>

#include "mesh/mesh.hpp"

namespace cfd::solvers {

// Port of solver3d/geometry.py's PreparedGeometry.
struct PreparedGeometry {
    cfd::mesh::Mesh mesh;
    double Lx = 0.0, Ly = 0.0, Lz = 0.0;
    bool is_watertight = false;
    cfd::mesh::Vec3 original_extents;
};

// Centers, normalizes, and places `mesh` inside a wind-tunnel-style domain
// (inflow gap upstream, wake gap downstream, lateral clearance on the
// sides) -- port of solver3d/geometry.py's prepare_geometry. The Reynolds
// number for the resulting simulation is defined using `target_max_extent`
// as the characteristic length, since an uploaded file's original units
// are otherwise meaningless.
//
// `ground_effect`/`altitude_gap` are a newer, opt-in addition (no Python
// equivalent): when `ground_effect` is true, the object is placed
// asymmetrically in Y -- `altitude_gap * L` of clearance beneath it (down
// to the y=0 "ground" plane) instead of being vertically centered, with
// the top clearance still governed by `lateral_gap` as before. When false
// (the default), behavior is byte-identical to the original symmetric
// centering -- `altitude_gap` is ignored.
[[nodiscard]] PreparedGeometry prepare_geometry(
    const cfd::mesh::Mesh& mesh,
    double target_max_extent = 1.0, double inflow_gap = 1.5, double wake_gap = 4.0, double lateral_gap = 1.5,
    bool ground_effect = false, double altitude_gap = 1.5);

// Centers, normalizes, and places a pipe/duct mesh for INTERNAL flow. Unlike
// prepare_geometry's open-tunnel domain, there is no lateral clearance: the
// pipe wall itself defines the domain's lateral (y,z) extent, so the grid
// hugs the mesh's own bounding box exactly. inflow_gap/wake_gap default to
// 0 since BCs can apply directly at the pipe's own end faces. Port of
// solver3d/geometry.py's prepare_internal_geometry.
[[nodiscard]] PreparedGeometry prepare_internal_geometry(
    const cfd::mesh::Mesh& mesh,
    double target_max_extent = 1.0, double inflow_gap = 0.0, double wake_gap = 0.0);

// Voxelizes `mesh` onto an (nx,ny,nz) grid via point-in-mesh containment --
// returns a solid mask (row-major C-order, i slowest/k fastest, true=solid),
// port of solver3d/geometry.py's voxelize_to_grid (external/object-in-
// tunnel convention: mesh interior = solid).
[[nodiscard]] std::vector<std::uint8_t> voxelize_to_grid(
    const cfd::mesh::Mesh& mesh, int nx, int ny, int nz, double Lx, double Ly, double Lz);

// Voxelizes a pipe/duct mesh for INTERNAL flow: the mesh's own hollow
// interior (its lumen) becomes the fluid region, and the wall material
// (plus anything genuinely outside the whole part) becomes solid -- the
// inverse topology from voxelize_to_grid's external-object convention.
//
// These two conventions have opposite mask polarity (mesh.contains() means
// "is wall material" for interpretation (a) vs. "is lumen" for (b)), so
// they can't share one fixed inversion rule -- instead, try (a) first and
// fall back to (b) if it finds nothing:
//   1. Voxelize inside_mask = mesh.contains(points). Flood-fill its
//      complement (6-connectivity). Any component that doesn't touch the
//      domain's LATERAL (y,z) boundary is an enclosed cavity -- under (a),
//      that's the lumen. Only y/z faces count as "true exterior": the pipe
//      is open at its two x-axis ends by design (inlet/outlet), so an
//      enclosed lumen legitimately reaches x=0/x=Lx and must not be
//      disqualified for it.
//   2. If that finds no enclosed cavity (no separate wall shell to
//      flood-fill around -- true whenever the mesh already directly bounds
//      the lumen, whether it started that way or was capped into that
//      shape by cap_open_boundaries), fall back to (b): inside_mask itself
//      is the fluid region.
// Throws std::runtime_error if neither interpretation finds any fluid
// region at all. Port of solver3d/geometry.py's voxelize_internal_to_grid.
[[nodiscard]] std::vector<std::uint8_t> voxelize_internal_to_grid(
    const cfd::mesh::Mesh& mesh, int nx, int ny, int nz, double Lx, double Ly, double Lz);

} // namespace cfd::solvers
