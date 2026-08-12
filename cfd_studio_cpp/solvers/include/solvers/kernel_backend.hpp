#pragma once

#include <cstdint>

namespace cfd::solvers {

// The 7-function contract every kernel backend implements, ported from
// solver3d/kernels.py's dual numba/native design -- but as an explicit
// value (a struct of function pointers chosen once at NavierStokes3D
// construction) instead of Python's live module-attribute rebinding. No
// global mutable state: multiple solver instances can hold different
// backends and never interfere with each other.
//
// All arrays are ghost-cell-padded (nx+2, ny+2, nz+2), row-major, per
// core::idx3 -- see core/grid_index.hpp. `solid` is int8 (0/1) to match the
// original ctypes/numpy-bool_-as-int8 boundary convention.
struct KernelBackend {
    void (*compute_rhs)(
        const double* u, const double* v, const double* w, const std::int8_t* solid,
        double re, double dx, double dy, double dz,
        int nx, int ny, int nz,
        double* rhs_u, double* rhs_v, double* rhs_w);

    void (*divergence)(
        const double* u, const double* v, const double* w, const std::int8_t* solid,
        double dx, double dy, double dz,
        int nx, int ny, int nz,
        double* div);

    void (*jacobi_pressure_solve)(
        const double* p0, const double* rhs, const std::int8_t* solid,
        double dx, double dy, double dz,
        int max_iters, double tol, double omega,
        int nx, int ny, int nz,
        double* p_out, double* residual_out);

    void (*correct_velocity)(
        double* u, double* v, double* w,
        const double* p, const std::int8_t* solid,
        double dt, double dx, double dy, double dz,
        int nx, int ny, int nz);

    void (*set_thread_count)(int n);
    int (*get_thread_count)();
    int (*max_thread_count)();

    const char* name; // "scalar" or "threaded" -- for logging/diagnostics only
};

// kernels_scalar.cpp -- kernels.cpp ported near-verbatim, always 1 thread.
// Doubles as both the small-grid-optimal backend and the correctness
// reference used by tests (see solvers/kernel_backend.hpp doc above).
const KernelBackend& scalar_backend();

// kernels_threaded.cpp -- persistent std::thread pool, created once and
// never torn down mid-run. Deliberately NOT OpenMP: an earlier Fortran
// build's OpenMP version stalled on real hardware mid-simulation (fork/join
// overhead accumulating over thousands of parallel-region entries on a
// long run) -- see kernels_threaded.cpp's file header for the full account.
const KernelBackend& threaded_backend();

} // namespace cfd::solvers
