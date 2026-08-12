// C++ port of the 3D solver's numerical kernels -- a 1:1 translation of
// solver3d/kernels.py (the numba reference) and solver3d/kernels.f90 (the
// earlier Fortran port this replaces), same ghost-cell index convention:
// real cells are 0..nx-1 / 0..ny-1 / 0..nz-1 in 0-based terms, with a
// ghost layer at index -1-equivalent (index 0 here, matching numpy's own
// 0-based (nx+2, ny+2, nz+2) arrays) and at nx+1 / ny+1 / nz+1.
//
// Arrays are indexed to match numpy's native C-contiguous (row-major)
// layout directly -- no Fortran-order transpose needed at the Python
// boundary (unlike the f2py bridge the Fortran version needed), since
// this is called via ctypes against raw array data.
//
// Deliberately single-threaded, no threading library at all: the earlier
// Fortran build's OpenMP version stalled on a real machine mid-simulation
// (worker thread stuck, consistent with a fork/join issue accumulating
// over the thousands of `!$OMP PARALLEL` region entries/exits a long run
// produces). Rather than re-learn that lesson in a new language, this
// build has no thread-team machinery in the process at all -- compiled
// single-core speed only, same tradeoff as the no-OpenMP Fortran build it
// replaces: meaningfully faster than numba on small grids (no per-call
// thread-dispatch overhead), slower than numba's actual multi-threaded
// execution once a grid is large enough to give numba real work to
// parallelize (including this app's own default grid size).
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <vector>

namespace {

inline size_t idx(int i, int j, int k, int ny, int nz) {
    return (size_t)i * (size_t)(ny + 2) * (size_t)(nz + 2) + (size_t)j * (size_t)(nz + 2) + (size_t)k;
}

inline double upwind_x(const double* f, int i, int j, int k, double vel, double h, int ny, int nz) {
    if (vel > 0.0) return (f[idx(i, j, k, ny, nz)] - f[idx(i - 1, j, k, ny, nz)]) / h;
    return (f[idx(i + 1, j, k, ny, nz)] - f[idx(i, j, k, ny, nz)]) / h;
}

inline double upwind_y(const double* f, int i, int j, int k, double vel, double h, int ny, int nz) {
    if (vel > 0.0) return (f[idx(i, j, k, ny, nz)] - f[idx(i, j - 1, k, ny, nz)]) / h;
    return (f[idx(i, j + 1, k, ny, nz)] - f[idx(i, j, k, ny, nz)]) / h;
}

inline double upwind_z(const double* f, int i, int j, int k, double vel, double h, int ny, int nz) {
    if (vel > 0.0) return (f[idx(i, j, k, ny, nz)] - f[idx(i, j, k - 1, ny, nz)]) / h;
    return (f[idx(i, j, k + 1, ny, nz)] - f[idx(i, j, k, ny, nz)]) / h;
}

inline double laplacian(const double* f, int i, int j, int k, double dx2, double dy2, double dz2, int ny, int nz) {
    double c = f[idx(i, j, k, ny, nz)];
    return (f[idx(i + 1, j, k, ny, nz)] - 2.0 * c + f[idx(i - 1, j, k, ny, nz)]) / dx2
         + (f[idx(i, j + 1, k, ny, nz)] - 2.0 * c + f[idx(i, j - 1, k, ny, nz)]) / dy2
         + (f[idx(i, j, k + 1, ny, nz)] - 2.0 * c + f[idx(i, j, k - 1, ny, nz)]) / dz2;
}

} // namespace


extern "C" {

void compute_rhs(
    const double* u, const double* v, const double* w, const int8_t* solid,
    double re, double dx, double dy, double dz,
    int nx, int ny, int nz,
    double* rhs_u, double* rhs_v, double* rhs_w
) {
    size_t total = (size_t)(nx + 2) * (size_t)(ny + 2) * (size_t)(nz + 2);
    std::fill(rhs_u, rhs_u + total, 0.0);
    std::fill(rhs_v, rhs_v + total, 0.0);
    std::fill(rhs_w, rhs_w + total, 0.0);
    double dx2 = dx * dx, dy2 = dy * dy, dz2 = dz * dz;

    for (int i = 1; i <= nx; i++) {
        for (int j = 1; j <= ny; j++) {
            for (int k = 1; k <= nz; k++) {
                if (solid[idx(i, j, k, ny, nz)] != 0) continue;
                size_t c = idx(i, j, k, ny, nz);
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

void divergence(
    const double* u, const double* v, const double* w, const int8_t* solid,
    double dx, double dy, double dz,
    int nx, int ny, int nz,
    double* div
) {
    size_t total = (size_t)(nx + 2) * (size_t)(ny + 2) * (size_t)(nz + 2);
    std::fill(div, div + total, 0.0);
    for (int i = 1; i <= nx; i++) {
        for (int j = 1; j <= ny; j++) {
            for (int k = 1; k <= nz; k++) {
                if (solid[idx(i, j, k, ny, nz)] != 0) continue;
                div[idx(i, j, k, ny, nz)] =
                    (u[idx(i + 1, j, k, ny, nz)] - u[idx(i - 1, j, k, ny, nz)]) / (2.0 * dx)
                  + (v[idx(i, j + 1, k, ny, nz)] - v[idx(i, j - 1, k, ny, nz)]) / (2.0 * dy)
                  + (w[idx(i, j, k + 1, ny, nz)] - w[idx(i, j, k - 1, ny, nz)]) / (2.0 * dz);
            }
        }
    }
}

void jacobi_pressure_solve(
    const double* p0, const double* rhs, const int8_t* solid,
    double dx, double dy, double dz,
    int max_iters, double tol, double omega,
    int nx, int ny, int nz,
    double* p_out, double* residual_out
) {
    size_t total = (size_t)(nx + 2) * (size_t)(ny + 2) * (size_t)(nz + 2);
    std::vector<double> p(p0, p0 + total);
    std::vector<double> p_new(p0, p0 + total);

    double dx2 = dx * dx, dy2 = dy * dy, dz2 = dz * dz;
    double denom = 2.0 * (1.0 / dx2 + 1.0 / dy2 + 1.0 / dz2);
    double residual = 0.0;

    for (int iter = 0; iter < max_iters; iter++) {
        // Neumann BC (zero-gradient) on pressure at domain faces via ghost mirroring.
        for (int j = 0; j <= ny + 1; j++) {
            for (int k = 0; k <= nz + 1; k++) {
                p[idx(0, j, k, ny, nz)] = p[idx(1, j, k, ny, nz)];
                p[idx(nx + 1, j, k, ny, nz)] = p[idx(nx, j, k, ny, nz)];
            }
        }
        for (int i = 0; i <= nx + 1; i++) {
            for (int k = 0; k <= nz + 1; k++) {
                p[idx(i, 0, k, ny, nz)] = p[idx(i, 1, k, ny, nz)];
                p[idx(i, ny + 1, k, ny, nz)] = p[idx(i, ny, k, ny, nz)];
            }
        }
        for (int i = 0; i <= nx + 1; i++) {
            for (int j = 0; j <= ny + 1; j++) {
                p[idx(i, j, 0, ny, nz)] = p[idx(i, j, 1, ny, nz)];
                p[idx(i, j, nz + 1, ny, nz)] = p[idx(i, j, nz, ny, nz)];
            }
        }

        for (int i = 1; i <= nx; i++) {
            for (int j = 1; j <= ny; j++) {
                for (int k = 1; k <= nz; k++) {
                    size_t c = idx(i, j, k, ny, nz);
                    if (solid[c] != 0) {
                        p_new[c] = 0.0;
                        continue;
                    }
                    double val = ((p[idx(i + 1, j, k, ny, nz)] + p[idx(i - 1, j, k, ny, nz)]) / dx2
                                + (p[idx(i, j + 1, k, ny, nz)] + p[idx(i, j - 1, k, ny, nz)]) / dy2
                                + (p[idx(i, j, k + 1, ny, nz)] + p[idx(i, j, k - 1, ny, nz)]) / dz2
                                - rhs[c]) / denom;
                    p_new[c] = p[c] + omega * (val - p[c]);
                }
            }
        }

        // pin one reference cell to remove the pressure null space
        p_new[idx(1, 1, 1, ny, nz)] = 0.0;

        double max_diff = 0.0;
        for (int i = 1; i <= nx; i++) {
            for (int j = 1; j <= ny; j++) {
                for (int k = 1; k <= nz; k++) {
                    size_t c = idx(i, j, k, ny, nz);
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

void correct_velocity(
    double* u, double* v, double* w,
    const double* p, const int8_t* solid,
    double dt, double dx, double dy, double dz,
    int nx, int ny, int nz
) {
    for (int i = 1; i <= nx; i++) {
        for (int j = 1; j <= ny; j++) {
            for (int k = 1; k <= nz; k++) {
                size_t c = idx(i, j, k, ny, nz);
                if (solid[c] != 0) {
                    u[c] = 0.0;
                    v[c] = 0.0;
                    w[c] = 0.0;
                    continue;
                }
                u[c] -= dt * (p[idx(i + 1, j, k, ny, nz)] - p[idx(i - 1, j, k, ny, nz)]) / (2.0 * dx);
                v[c] -= dt * (p[idx(i, j + 1, k, ny, nz)] - p[idx(i, j - 1, k, ny, nz)]) / (2.0 * dy);
                w[c] -= dt * (p[idx(i, j, k + 1, ny, nz)] - p[idx(i, j, k - 1, ny, nz)]) / (2.0 * dz);
            }
        }
    }
}

// Always single-threaded (see file header) -- these mirror the same
// trivial stubs the no-OpenMP Fortran build used.
void set_thread_count(int n) {
    (void)n; // intentionally ignored
}

int get_thread_count() {
    return 1;
}

int max_thread_count() {
    return 1;
}

} // extern "C"
