#pragma once

// Placeholder for Phase 1 (mesh I/O: STL/OBJ/PLY/OFF readers, MeshBVH via
// Embree, convex_hull2d via Qhull). Intentionally minimal for now so the
// project configures and builds end-to-end starting from Phase 0/2 --
// filled in during Phase 1/3 per the implementation plan.

namespace cfd::mesh {

struct Vertex {
    double x = 0.0, y = 0.0, z = 0.0;
};

struct Triangle {
    int v0 = 0, v1 = 0, v2 = 0;
};

} // namespace cfd::mesh
