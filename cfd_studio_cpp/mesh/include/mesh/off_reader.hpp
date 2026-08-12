#pragma once

#include <string>

#include "mesh/mesh.hpp"

namespace cfd::mesh {

// Reads an OFF (Object File Format) file: `OFF` header, `nVertices nFaces
// nEdges` counts, then vertex list, then face list (`n i0 i1 ... i(n-1)`,
// fan-triangulated if n > 3). Throws std::runtime_error on a malformed
// header or an out-of-range face index.
[[nodiscard]] Mesh read_off(const std::string& path);

} // namespace cfd::mesh
