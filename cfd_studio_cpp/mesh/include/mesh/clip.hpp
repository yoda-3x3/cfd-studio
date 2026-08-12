#pragma once

#include <array>
#include <vector>

#include "mesh/mesh.hpp"

namespace cfd::mesh {

// A raw triangle as 3 vertex positions (not indexed) -- the natural
// representation for clipping results, where a clipped triangle no longer
// corresponds to any single original triangle's vertex indices.
using RawTriangle = std::array<Vec3, 3>;

// Clips `mesh`'s own triangles to an axis-aligned box via 6 sequential
// half-space cuts (no capping faces added), rather than a solid boolean
// difference -- this needs no watertightness/volume on `mesh`, so thin or
// open surfaces (e.g. a wing skin with no modeled wall thickness) are
// preserved exactly instead of silently failing. Port of
// io/openfoam_writer.py's _clip_mesh_to_box. Any resulting triangle flush
// against one of the box's own cutting planes is excluded (should be rare
// since nothing here introduces capping faces, but kept as a defensive
// check, matching the Python source).
[[nodiscard]] std::vector<RawTriangle> clip_mesh_to_box(
    const Mesh& mesh, double xmin, double xmax, double ymin, double ymax, double zmin, double zmax, double tol);

} // namespace cfd::mesh
