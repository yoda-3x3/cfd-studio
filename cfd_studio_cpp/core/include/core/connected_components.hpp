#pragma once

#include <cstdint>
#include <vector>

namespace cfd::core {

// 6-connectivity connected-component labeling over a boolean (nx,ny,nz)
// grid (unpadded -- no ghost layer, C-order/row-major, matching numpy's
// default `scipy.ndimage.label` layout that solver3d/geometry.py's internal-
// flow lumen detection relies on). `mask[i,j,k]` true means "foreground" --
// i.e. the caller passes the complement of the containment mask when
// looking for enclosed cavities, exactly as geometry.py does.
//
// Returns one label per foreground cell (0 = background/not-foreground,
// 1..num_components = component id), plus the component count. Component
// ids are assigned in the order first encountered during a single raster
// scan + BFS flood fill -- deterministic for a given input, not meaningful
// beyond that.
struct ComponentLabels {
    std::vector<std::int32_t> labels; // size nx*ny*nz, C-order (i slowest, k fastest)
    int num_components = 0;
};

[[nodiscard]] ComponentLabels label_components_6connectivity(
    const std::vector<std::uint8_t>& mask, int nx, int ny, int nz);

} // namespace cfd::core
