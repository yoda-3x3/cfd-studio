#pragma once

#include <string>

#include "mesh/mesh.hpp"

namespace cfd::mesh {

// Reads a PLY file: ascii, binary_little_endian, or binary_big_endian
// format (auto-detected from the header). Only the `vertex` element's x/y/z
// properties and the `face` element's vertex-index list property are used;
// any other properties (normals, colors, texture coords, other elements)
// are read past and discarded. Faces with more than 3 vertices are
// fan-triangulated. Throws std::runtime_error on a malformed header or a
// property type this reader doesn't recognize.
[[nodiscard]] Mesh read_ply(const std::string& path);

} // namespace cfd::mesh
