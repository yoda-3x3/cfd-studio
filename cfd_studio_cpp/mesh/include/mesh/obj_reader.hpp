#pragma once

#include <string>

#include "mesh/mesh.hpp"

namespace cfd::mesh {

// Reads a Wavefront OBJ file: `v x y z` vertices and `f ...` faces (each
// face token may be a bare vertex index or `v/vt/vn` -- only the vertex
// index is used). Faces with more than 3 vertices are fan-triangulated
// from their first vertex. All other line types (vn, vt, g, o, mtllib,
// comments, ...) are ignored -- this reader only needs geometry. Throws
// std::runtime_error on a missing file or a face index out of range.
[[nodiscard]] Mesh read_obj(const std::string& path);

} // namespace cfd::mesh
