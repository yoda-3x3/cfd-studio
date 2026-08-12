#pragma once

#include <cstdint>
#include <vector>

#include "mesh/mesh.hpp"

namespace cfd::mesh {

// A closed loop of boundary-edge vertices, in the winding order the
// adjacent triangle established (last vertex implicitly connects back to
// the first) -- port of trimesh's mesh.outline() entities.
struct BoundaryLoop {
    std::vector<std::uint32_t> vertices;
};

// True if every edge is shared by exactly two triangles with opposite
// winding (a closed, consistently-oriented 2-manifold) -- a simplified but
// standard closed-mesh test, port of trimesh's mesh.is_watertight for the
// cases this app actually needs it for (deciding whether cap_open_boundaries
// / the boolean-diff fallback in the OpenFOAM writer apply).
[[nodiscard]] bool is_watertight(const Mesh& mesh);

// Boundary loops: edges used by exactly one triangle, chained into closed
// loops via the winding direction that triangle established. Port of
// trimesh's mesh.outline().entities.
[[nodiscard]] std::vector<BoundaryLoop> boundary_loops(const Mesh& mesh);

// Caps each open boundary loop with a fan triangulation from the loop's own
// first vertex, turning an open shell (e.g. a pipe/duct wall with no
// modeled thickness -- a tube with two open end loops) into a closed solid
// bounding its interior volume. Winding is chosen to point away from the
// mesh's centroid (outward), matching the convention
// io/openfoam_writer.py's _refine_object_patch already established. A
// no-op (returns `mesh` unchanged) if the mesh is already watertight.
// Port of solver3d/geometry.py's _cap_open_boundaries -- trimesh.repair.
// fill_holes proved unreliable in practice for many-sided circular loops
// (real pipe end rims), hence this small custom capper instead.
[[nodiscard]] Mesh cap_open_boundaries(const Mesh& mesh);

} // namespace cfd::mesh
