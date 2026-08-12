#pragma once

// Private (not in include/) shared math helpers for the scalar and
// threaded kernel backends -- factored out so the two backends can never
// silently drift apart in their actual numerics, only in how the outer
// loop over i is scheduled.
#include "core/grid_index.hpp"

namespace cfd::solvers::detail {

using cfd::core::idx3;

inline double upwind_x(const double* f, int i, int j, int k, double vel, double h, int ny, int nz) {
    if (vel > 0.0) return (f[idx3(i, j, k, ny, nz)] - f[idx3(i - 1, j, k, ny, nz)]) / h;
    return (f[idx3(i + 1, j, k, ny, nz)] - f[idx3(i, j, k, ny, nz)]) / h;
}

inline double upwind_y(const double* f, int i, int j, int k, double vel, double h, int ny, int nz) {
    if (vel > 0.0) return (f[idx3(i, j, k, ny, nz)] - f[idx3(i, j - 1, k, ny, nz)]) / h;
    return (f[idx3(i, j + 1, k, ny, nz)] - f[idx3(i, j, k, ny, nz)]) / h;
}

inline double upwind_z(const double* f, int i, int j, int k, double vel, double h, int ny, int nz) {
    if (vel > 0.0) return (f[idx3(i, j, k, ny, nz)] - f[idx3(i, j, k - 1, ny, nz)]) / h;
    return (f[idx3(i, j, k + 1, ny, nz)] - f[idx3(i, j, k, ny, nz)]) / h;
}

inline double laplacian(const double* f, int i, int j, int k, double dx2, double dy2, double dz2, int ny, int nz) {
    double c = f[idx3(i, j, k, ny, nz)];
    return (f[idx3(i + 1, j, k, ny, nz)] - 2.0 * c + f[idx3(i - 1, j, k, ny, nz)]) / dx2
         + (f[idx3(i, j + 1, k, ny, nz)] - 2.0 * c + f[idx3(i, j - 1, k, ny, nz)]) / dy2
         + (f[idx3(i, j, k + 1, ny, nz)] - 2.0 * c + f[idx3(i, j, k - 1, ny, nz)]) / dz2;
}

} // namespace cfd::solvers::detail
