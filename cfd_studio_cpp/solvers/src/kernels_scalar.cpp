// Near-verbatim port of solver3d/kernels.cpp (itself a 1:1 translation of
// solver3d/kernels.py's numba reference and the earlier kernels.f90 Fortran
// version). Deliberately single-threaded, no threading library at all: the
// earlier Fortran build's OpenMP version stalled on a real machine mid-
// simulation (worker thread stuck, consistent with a fork/join issue
// accumulating over the thousands of `!$OMP PARALLEL` region entries/exits
// a long run produces). This backend doubles as both the small-grid-optimal
// choice (no per-call thread-dispatch overhead) and the correctness
// reference other backends are tested against.
#include "solvers/kernel_backend.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/grid_index.hpp"

namespace cfd::solvers {
namespace {

using cfd::core::idx3;
using cfd::core::padded_total3;

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

void compute_rhs_scalar(
    const double* u, const double* v, const double* w, const std::int8_t* solid,
    double re, double dx, double dy, double dz,
    int nx, int ny, int nz,
    double* rhs_u, double* rhs_v, double* rhs_w) {
    std::size_t total = padded_total3(nx, ny, nz);
    std::fill(rhs_u, rhs_u + total, 0.0);
    std::fill(rhs_v, rhs_v + total, 0.0);
    std::fill(rhs_w, rhs_w + total, 0.0);
    double dx2 = dx * dx, dy2 = dy * dy, dz2 = dz * dz;

    for (int i = 1; i <= nx; i++) {
        for (int j = 1; j <= ny; j++) {
            for (int k = 1; k <= nz; k++) {
                if (solid[idx3(i, j, k, ny, nz)] != 0) continue;
                std::size_t c = idx3(i, j, k, ny, nz);
                double uu = u[c], vv = v[c], ww = w[c];

                double dudx = upwind_x(u, i, j, k, uu, dx, ny, nz);
                double dudy = upwind_y(u, i, j, k, vv, dy, ny, nz);
                double dudz = upwind_z(u, i, j, k, ww, dz, ny, nz);
                rhs_u[c] = -(uu * dudx + vv * dudy + ww * dudz) + laplacian(u, i, j, k, dx2, dy2, dz2, ny, nz) / re;

                double dvdx = upwind_x(v, i, j, k, uu, dx, ny, nz);
                double dvdy = upwind_y(v, i, j, k, vv, dy, ny, nz);
                double dvdz = upwind_z(v, i, j, k, ww, dz, ny, nz);
                rhs_v[c] = -(uu * dvdx + vv * dvdy + ww * dvdz) + laplacian(v, i, j, k, dx2, dy2, dz2, ny, nz) / re;

                double dwdx = upwind_x(w, i, j, k, uu, dx, ny, nz);
                double dwdy = upwind_y(w, i, j, k, vv, dy, ny, nz);
                double dwdz = upwind_z(w, i, j, k, ww, dz, ny, nz);
                rhs_w[c] = -(uu * dwdx + vv * dwdy + ww * dwdz) + laplacian(w, i, j, k, dx2, dy2, dz2, ny, nz) / re;
            }
        }
    }
}

void divergence_scalar(
    const double* u, const double* v, const double* w, const std::int8_t* solid,
    double dx, double dy, double dz,
    int nx, int ny, int nz,
    double* div) {
    std::size_t total = padded_total3(nx, ny, nz);
    std::fill(div, div + total, 0.0);
    for (int i = 1; i <= nx; i++) {
        for (int j = 1; j <= ny; j++) {
            for (int k = 1; k <= nz; k++) {
                if (solid[idx3(i, j, k, ny, nz)] != 0) continue;
                div[idx3(i, j, k, ny, nz)] =
                    (u[idx3(i + 1, j, k, ny, nz)] - u[idx3(i - 1, j, k, ny, nz)]) / (2.0 * dx)
                  + (v[idx3(i, j + 1, k, ny, nz)] - v[idx3(i, j - 1, k, ny, nz)]) / (2.0 * dy)
                  + (w[idx3(i, j, k + 1, ny, nz)] - w[idx3(i, j, k - 1, ny, nz)]) / (2.0 * dz);
            }
        }
    }
}

void jacobi_pressure_solve_scalar(
    const double* p0, const double* rhs, const std::int8_t* solid,
    double dx, double dy, double dz,
    int max_iters, double tol, double omega,
    int nx, int ny, int nz,
    double* p_out, double* residual_out) {
    std::size_t total = padded_total3(nx, ny, nz);
    std::vector<double> p(p0, p0 + total);
    std::vector<double> p_new(p0, p0 + total);

    double dx2 = dx * dx, dy2 = dy * dy, dz2 = dz * dz;
    double denom = 2.0 * (1.0 / dx2 + 1.0 / dy2 + 1.0 / dz2);
    double residual = 0.0;

    for (int iter = 0; iter < max_iters; iter++) {
        // Neumann BC (zero-gradient) on pressure at domain faces via ghost mirroring.
        for (int j = 0; j <= ny + 1; j++) {
            for (int k = 0; k <= nz + 1; k++) {
                p[idx3(0, j, k, ny, nz)] = p[idx3(1, j, k, ny, nz)];
                p[idx3(nx + 1, j, k, ny, nz)] = p[idx3(nx, j, k, ny, nz)];
            }
        }
        for (int i = 0; i <= nx + 1; i++) {
            for (int k = 0; k <= nz + 1; k++) {
                p[idx3(i, 0, k, ny, nz)] = p[idx3(i, 1, k, ny, nz)];
                p[idx3(i, ny + 1, k, ny, nz)] = p[idx3(i, ny, k, ny, nz)];
            }
        }
        for (int i = 0; i <= nx + 1; i++) {
            for (int j = 0; j <= ny + 1; j++) {
                p[idx3(i, j, 0, ny, nz)] = p[idx3(i, j, 1, ny, nz)];
                p[idx3(i, j, nz + 1, ny, nz)] = p[idx3(i, j, nz, ny, nz)];
            }
        }

        for (int i = 1; i <= nx; i++) {
            for (int j = 1; j <= ny; j++) {
                for (int k = 1; k <= nz; k++) {
                    std::size_t c = idx3(i, j, k, ny, nz);
                    if (solid[c] != 0) {
                        p_new[c] = 0.0;
                        continue;
                    }
                    double val = ((p[idx3(i + 1, j, k, ny, nz)] + p[idx3(i - 1, j, k, ny, nz)]) / dx2
                                + (p[idx3(i, j + 1, k, ny, nz)] + p[idx3(i, j - 1, k, ny, nz)]) / dy2
                                + (p[idx3(i, j, k + 1, ny, nz)] + p[idx3(i, j, k - 1, ny, nz)]) / dz2
                                - rhs[c]) / denom;
                    p_new[c] = p[c] + omega * (val - p[c]);
                }
            }
        }

        // pin one reference cell to remove the pressure null space
        p_new[idx3(1, 1, 1, ny, nz)] = 0.0;

        double max_diff = 0.0;
        for (int i = 1; i <= nx; i++) {
            for (int j = 1; j <= ny; j++) {
                for (int k = 1; k <= nz; k++) {
                    std::size_t c = idx3(i, j, k, ny, nz);
                    double d = std::fabs(p_new[c] - p[c]);
                    if (d > max_diff) max_diff = d;
                }
            }
        }

        std::swap(p, p_new);
        residual = max_diff;
        if (max_diff < tol) break;
    }

    std::copy(p.begin(), p.end(), p_out);
    *residual_out = residual;
}

void correct_velocity_scalar(
    double* u, double* v, double* w,
    const double* p, const std::int8_t* solid,
    double dt, double dx, double dy, double dz,
    int nx, int ny, int nz) {
    for (int i = 1; i <= nx; i++) {
        for (int j = 1; j <= ny; j++) {
            for (int k = 1; k <= nz; k++) {
                std::size_t c = idx3(i, j, k, ny, nz);
                if (solid[c] != 0) {
                    u[c] = 0.0;
                    v[c] = 0.0;
                    w[c] = 0.0;
                    continue;
                }
                u[c] -= dt * (p[idx3(i + 1, j, k, ny, nz)] - p[idx3(i - 1, j, k, ny, nz)]) / (2.0 * dx);
                v[c] -= dt * (p[idx3(i, j + 1, k, ny, nz)] - p[idx3(i, j - 1, k, ny, nz)]) / (2.0 * dy);
                w[c] -= dt * (p[idx3(i, j, k + 1, ny, nz)] - p[idx3(i, j, k - 1, ny, nz)]) / (2.0 * dz);
            }
        }
    }
}

// Always single-threaded (see file header) -- these mirror the same
// trivial stubs the no-OpenMP Fortran/C build used.
void set_thread_count_scalar(int) {}
int get_thread_count_scalar() { return 1; }
int max_thread_count_scalar() { return 1; }

} // namespace

const KernelBackend& scalar_backend() {
    static const KernelBackend backend{
        compute_rhs_scalar,
        divergence_scalar,
        jacobi_pressure_solve_scalar,
        correct_velocity_scalar,
        set_thread_count_scalar,
        get_thread_count_scalar,
        max_thread_count_scalar,
        "scalar",
    };
    return backend;
}

} // namespace cfd::solvers
