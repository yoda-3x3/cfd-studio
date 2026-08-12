#pragma once

#include <cstddef>

namespace cfd::core {

// Flat row-major index into a ghost-cell-padded (nx+2, ny+2, nz+2) array,
// matching numpy's native C-contiguous layout exactly (real cells at
// 1..nx / 1..ny / 1..nz, ghost layer at 0 and nx+1/ny+1/nz+1). This is the
// exact convention solver3d/kernels.cpp already established -- every 3D
// solver/kernel/geometry piece in this codebase must index arrays this way
// so no Fortran-order transpose or bespoke layout ever needs to be
// reasoned about at any boundary between modules.
[[nodiscard]] inline std::size_t idx3(int i, int j, int k, int ny, int nz) noexcept {
    return static_cast<std::size_t>(i) * static_cast<std::size_t>(ny + 2) * static_cast<std::size_t>(nz + 2)
         + static_cast<std::size_t>(j) * static_cast<std::size_t>(nz + 2)
         + static_cast<std::size_t>(k);
}

[[nodiscard]] inline std::size_t padded_total3(int nx, int ny, int nz) noexcept {
    return static_cast<std::size_t>(nx + 2) * static_cast<std::size_t>(ny + 2) * static_cast<std::size_t>(nz + 2);
}

// Flat row-major index into an unpadded (nx, ny) 2D array -- the 2D solver's
// convention (solver/navier_stokes.py), no ghost layer, y as the first axis.
[[nodiscard]] inline std::size_t idx2(int row_y, int col_x, int nx) noexcept {
    return static_cast<std::size_t>(row_y) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(col_x);
}

} // namespace cfd::core
