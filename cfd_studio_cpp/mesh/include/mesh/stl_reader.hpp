#pragma once

#include <string>

#include "mesh/mesh.hpp"

namespace cfd::mesh {

// Reads an STL file (ASCII or binary, auto-detected). Does not deduplicate
// coincident vertices across facets -- STL has no shared-vertex concept, so
// every facet gets 3 fresh vertices; callers needing a welded mesh (e.g. for
// containment queries where coincident vertices should merge) should weld
// separately. Throws std::runtime_error on a malformed file.
[[nodiscard]] Mesh read_stl(const std::string& path);

} // namespace cfd::mesh
