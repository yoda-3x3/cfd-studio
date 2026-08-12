// Multi-threaded kernel backend: same numerics as kernels_scalar.cpp (via
// the shared kernel_math.hpp helpers -- the two backends cannot silently
// diverge in their math, only in how the outer i-loop is scheduled), the
// outer i in [1,nx] loop of each kernel split across a persistent
// ThreadPool (see thread_pool.hpp for why this is not OpenMP).
#include "solvers/kernel_backend.hpp"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

#include "core/grid_index.hpp"
#include "kernel_math.hpp"
#include "thread_pool.hpp"

namespace cfd::solvers {
namespace {

using cfd::core::idx3;
using cfd::core::padded_total3;
using namespace cfd::solvers::detail;

ThreadPool& pool() {
    static ThreadPool instance(static_cast<int>(std::max(1u, std::thread::hardware_concurrency())));
    return instance;
}

void compute_rhs_threaded(
    const double* u, const double* v, const double* w, const std::int8_t* solid,
    double re, double dx, double dy, double dz,
    int nx, int ny, int nz,
    double* rhs_u, double* rhs_v, double* rhs_w) {
    std::size_t total = padded_total3(nx, ny, nz);
    std::fill(rhs_u, rhs_u + total, 0.0);
    std::fill(rhs_v, rhs_v + total, 0.0);
    std::fill(rhs_w, rhs_w + total, 0.0);
    double dx2 = dx * dx, dy2 = dy * dy, dz2 = dz * dz;

    pool().parallel_for(nx, [&](int ii) {
        int i = ii + 1;
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
    });
}

void divergence_threaded(
    const double* u, const double* v, const double* w, const std::int8_t* solid,
    double dx, double dy, double dz,
    int nx, int ny, int nz,
    double* div) {
    std::size_t total = padded_total3(nx, ny, nz);
    std::fill(div, div + total, 0.0);
    pool().parallel_for(nx, [&](int ii) {
        int i = ii + 1;
        for (int j = 1; j <= ny; j++) {
            for (int k = 1; k <= nz; k++) {
                if (solid[idx3(i, j, k, ny, nz)] != 0) continue;
                div[idx3(i, j, k, ny, nz)] =
                    (u[idx3(i + 1, j, k, ny, nz)] - u[idx3(i - 1, j, k, ny, nz)]) / (2.0 * dx)
                  + (v[idx3(i, j + 1, k, ny, nz)] - v[idx3(i, j - 1, k, ny, nz)]) / (2.0 * dy)
                  + (w[idx3(i, j, k + 1, ny, nz)] - w[idx3(i, j, k - 1, ny, nz)]) / (2.0 * dz);
            }
        }
    });
}

void jacobi_pressure_solve_threaded(
    const double* p0, const double* rhs, const std::int8_t* solid,
    double dx, double dy, double dz,
    int max_iters, double tol, double omega,
    int nx, int ny, int nz,
    double* p_out, double* residual_out) {
    std::size_t total = padded_total3(nx, ny, nz);
    std::vector<double> p(p0, p0 + total);
    std::vector<double> p_new(p0, p0 + total);
    std::vector<double> row_max(static_cast<std::size_t>(nx), 0.0); // per-i local max, reduced serially after each iteration

    double dx2 = dx * dx, dy2 = dy * dy, dz2 = dz * dz;
    double denom = 2.0 * (1.0 / dx2 + 1.0 / dy2 + 1.0 / dz2);
    double residual = 0.0;

    for (int iter = 0; iter < max_iters; iter++) {
        // Neumann BC mirroring stays serial: O(ny*nz + nx*nz + nx*ny), small
        // relative to the O(nx*ny*nz) main loop below, not worth threading.
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

        pool().parallel_for(nx, [&](int ii) {
            int i = ii + 1;
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
        });

        // pin one reference cell to remove the pressure null space
        p_new[idx3(1, 1, 1, ny, nz)] = 0.0;

        std::fill(row_max.begin(), row_max.end(), 0.0);
        pool().parallel_for(nx, [&](int ii) {
            int i = ii + 1;
            double local_max = 0.0;
            for (int j = 1; j <= ny; j++) {
                for (int k = 1; k <= nz; k++) {
                    std::size_t c = idx3(i, j, k, ny, nz);
                    double d = std::fabs(p_new[c] - p[c]);
                    if (d > local_max) local_max = d;
                }
            }
            row_max[static_cast<std::size_t>(ii)] = local_max; // each ii owned by exactly one thread/iteration -- no race
        });
        double max_diff = 0.0;
        for (double m : row_max) max_diff = std::max(max_diff, m);

        std::swap(p, p_new);
        residual = max_diff;
        if (max_diff < tol) break;
    }

    std::copy(p.begin(), p.end(), p_out);
    *residual_out = residual;
}

void correct_velocity_threaded(
    double* u, double* v, double* w,
    const double* p, const std::int8_t* solid,
    double dt, double dx, double dy, double dz,
    int nx, int ny, int nz) {
    pool().parallel_for(nx, [&](int ii) {
        int i = ii + 1;
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
    });
}

void set_thread_count_threaded(int n) { pool().resize(n); }
int get_thread_count_threaded() { return pool().thread_count(); }
int max_thread_count_threaded() { return static_cast<int>(std::max(1u, std::thread::hardware_concurrency())); }

} // namespace

const KernelBackend& threaded_backend() {
    static const KernelBackend backend{
        compute_rhs_threaded,
        divergence_threaded,
        jacobi_pressure_solve_threaded,
        correct_velocity_threaded,
        set_thread_count_threaded,
        get_thread_count_threaded,
        max_thread_count_threaded,
        "threaded",
    };
    return backend;
}

} // namespace cfd::solvers
