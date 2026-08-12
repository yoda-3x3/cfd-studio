#pragma once

#include <cstdint>
#include <vector>

namespace cfd::core {

// 6-connectivity binary dilation over an unpadded (nx,ny,nz) boolean grid,
// `iterations` rounds, matching scipy.ndimage.binary_dilation's default
// border behavior (out-of-bounds neighbors count as False, not wrapped or
// extended) -- used by the 3D solver's residual-mask construction
// (excluding a dilated shell around solid cells from the convergence
// check, ported from solver3d/navier_stokes3d.py).
[[nodiscard]] std::vector<std::uint8_t> dilate_6connectivity(
    const std::vector<std::uint8_t>& mask, int nx, int ny, int nz, int iterations);

} // namespace cfd::core
