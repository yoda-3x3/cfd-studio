"""
Numerical kernels for the 3D incompressible Navier-Stokes solver
(collocated grid, explicit projection method).

All arrays are padded with a 1-cell ghost layer on every side:
shape (nx+2, ny+2, nz+2), with real cells at indices 1..nx, 1..ny, 1..nz.

Two backends implement the same 7 functions (compute_rhs, divergence,
jacobi_pressure_solve, correct_velocity, set_thread_count,
get_thread_count, max_thread_count):

  - The numba-jitted Python below -- the DEFAULT. Proven stable across
    this project's history, and multi-threaded (real scaling on large
    grids -- e.g. faster than the native build below at the app's
    default 100x60x60 grid).
  - A compiled C++ shared library (kernels.cpp, built via
    build_native.py into `_kernels_cpp.dll`, loaded via ctypes -- no
    extra Python package needed for the bridge, unlike the f2py/meson
    toolchain an earlier Fortran version of this same idea needed),
    OPT-IN via the CFD_KERNELS=native environment variable. Numerically
    verified identical to the numba path. Deliberately single-threaded,
    no threading library linked in at all: an earlier OpenMP-enabled
    Fortran build of these same kernels stalled on real hardware mid-
    simulation (worker thread stuck, consistent with a fork/join issue
    accumulating over the thousands of parallel-region entries/exits a
    long run produces, since every solver step hits ~5 of them). Rather
    than re-learn that lesson in a new language, this build has no
    thread-team machinery in the process at all to deadlock. Measured
    tradeoff (same shape as the earlier Fortran build's): meaningfully
    faster than numba on SMALL grids (numba's per-call thread-dispatch
    overhead dominates there) but slower than numba's default multi-
    threaded execution on LARGE grids (including the app's own
    defaults) once numba has enough work to actually benefit from
    multiple cores.

The public names at the bottom of this module (`compute_rhs`, etc.) are
bound to whichever backend is active; the rest of the codebase just
calls `kernels.compute_rhs(...)` etc. without needing to know which one
it's getting.
"""
from __future__ import annotations

import ctypes
import os
from pathlib import Path

import numba
import numpy as np
from numba import njit, prange

_lib = None
_c_double_p = ctypes.POINTER(ctypes.c_double)
_c_int8_p = ctypes.POINTER(ctypes.c_int8)


def _ptr(arr: np.ndarray):
    return arr.ctypes.data_as(_c_double_p)


def _ptr_i8(arr: np.ndarray):
    return arr.ctypes.data_as(_c_int8_p)


try:
    _dll_path = Path(__file__).resolve().parent / "_kernels_cpp.dll"
    if _dll_path.exists():
        _lib = ctypes.CDLL(str(_dll_path))

        _lib.compute_rhs.argtypes = [
            _c_double_p, _c_double_p, _c_double_p, _c_int8_p,
            ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_double,
            ctypes.c_int, ctypes.c_int, ctypes.c_int,
            _c_double_p, _c_double_p, _c_double_p,
        ]
        _lib.compute_rhs.restype = None

        _lib.divergence.argtypes = [
            _c_double_p, _c_double_p, _c_double_p, _c_int8_p,
            ctypes.c_double, ctypes.c_double, ctypes.c_double,
            ctypes.c_int, ctypes.c_int, ctypes.c_int,
            _c_double_p,
        ]
        _lib.divergence.restype = None

        _lib.jacobi_pressure_solve.argtypes = [
            _c_double_p, _c_double_p, _c_int8_p,
            ctypes.c_double, ctypes.c_double, ctypes.c_double,
            ctypes.c_int, ctypes.c_double, ctypes.c_double,
            ctypes.c_int, ctypes.c_int, ctypes.c_int,
            _c_double_p, _c_double_p,
        ]
        _lib.jacobi_pressure_solve.restype = None

        _lib.correct_velocity.argtypes = [
            _c_double_p, _c_double_p, _c_double_p,
            _c_double_p, _c_int8_p,
            ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_double,
            ctypes.c_int, ctypes.c_int, ctypes.c_int,
        ]
        _lib.correct_velocity.restype = None

        _lib.set_thread_count.argtypes = [ctypes.c_int]
        _lib.set_thread_count.restype = None
        _lib.get_thread_count.argtypes = []
        _lib.get_thread_count.restype = ctypes.c_int
        _lib.max_thread_count.argtypes = []
        _lib.max_thread_count.restype = ctypes.c_int

        # smoke-test immediately, not just the load: a DLL that's present
        # but broken (antivirus-mangled copy, wrong architecture, etc.)
        # can still load successfully and only fail -- or in the worst
        # case crash outright -- the first time a function actually
        # runs. Catching that here, at module import, means a broken
        # install fails fast during startup with a normal Python
        # exception and falls back to numba, rather than surfacing
        # unpredictably mid-simulation later. Always attempted (not
        # gated on CFD_KERNELS) so `native_available` below is accurate
        # for callers -- e.g. performance presets -- that want to pick a
        # backend at runtime, not just via the environment variable.
        _lib.max_thread_count()
    else:
        _lib = None
except Exception:
    _lib = None

native_available = _lib is not None


@njit(inline="always")
def _upwind(field, i, j, k, vel, axis, h):
    if axis == 0:
        if vel > 0.0:
            return (field[i, j, k] - field[i - 1, j, k]) / h
        else:
            return (field[i + 1, j, k] - field[i, j, k]) / h
    elif axis == 1:
        if vel > 0.0:
            return (field[i, j, k] - field[i, j - 1, k]) / h
        else:
            return (field[i, j + 1, k] - field[i, j, k]) / h
    else:
        if vel > 0.0:
            return (field[i, j, k] - field[i, j, k - 1]) / h
        else:
            return (field[i, j, k + 1] - field[i, j, k]) / h


@njit(inline="always")
def _lap(field, i, j, k, dx2, dy2, dz2):
    return (
        (field[i + 1, j, k] - 2.0 * field[i, j, k] + field[i - 1, j, k]) / dx2
        + (field[i, j + 1, k] - 2.0 * field[i, j, k] + field[i, j - 1, k]) / dy2
        + (field[i, j, k + 1] - 2.0 * field[i, j, k] + field[i, j, k - 1]) / dz2
    )


@njit(parallel=True, cache=True)
def _compute_rhs_numba(u, v, w, solid, Re, dx, dy, dz):
    nx, ny, nz = u.shape[0] - 2, u.shape[1] - 2, u.shape[2] - 2
    dx2, dy2, dz2 = dx * dx, dy * dy, dz * dz
    rhs_u = np.zeros_like(u)
    rhs_v = np.zeros_like(v)
    rhs_w = np.zeros_like(w)

    for i in prange(1, nx + 1):
        for j in range(1, ny + 1):
            for k in range(1, nz + 1):
                if solid[i, j, k]:
                    continue
                uu, vv, ww = u[i, j, k], v[i, j, k], w[i, j, k]

                dudx = _upwind(u, i, j, k, uu, 0, dx)
                dudy = _upwind(u, i, j, k, vv, 1, dy)
                dudz = _upwind(u, i, j, k, ww, 2, dz)
                rhs_u[i, j, k] = -(uu * dudx + vv * dudy + ww * dudz) + _lap(u, i, j, k, dx2, dy2, dz2) / Re

                dvdx = _upwind(v, i, j, k, uu, 0, dx)
                dvdy = _upwind(v, i, j, k, vv, 1, dy)
                dvdz = _upwind(v, i, j, k, ww, 2, dz)
                rhs_v[i, j, k] = -(uu * dvdx + vv * dvdy + ww * dvdz) + _lap(v, i, j, k, dx2, dy2, dz2) / Re

                dwdx = _upwind(w, i, j, k, uu, 0, dx)
                dwdy = _upwind(w, i, j, k, vv, 1, dy)
                dwdz = _upwind(w, i, j, k, ww, 2, dz)
                rhs_w[i, j, k] = -(uu * dwdx + vv * dwdy + ww * dwdz) + _lap(w, i, j, k, dx2, dy2, dz2) / Re

    return rhs_u, rhs_v, rhs_w


@njit(parallel=True, cache=True)
def _divergence_numba(u, v, w, solid, dx, dy, dz):
    nx, ny, nz = u.shape[0] - 2, u.shape[1] - 2, u.shape[2] - 2
    div = np.zeros((nx + 2, ny + 2, nz + 2))
    for i in prange(1, nx + 1):
        for j in range(1, ny + 1):
            for k in range(1, nz + 1):
                if solid[i, j, k]:
                    continue
                div[i, j, k] = (
                    (u[i + 1, j, k] - u[i - 1, j, k]) / (2 * dx)
                    + (v[i, j + 1, k] - v[i, j - 1, k]) / (2 * dy)
                    + (w[i, j, k + 1] - w[i, j, k - 1]) / (2 * dz)
                )
    return div


@njit(parallel=True, cache=True)
def _jacobi_pressure_solve_numba(p0, rhs, solid, dx, dy, dz, max_iters, tol, omega=1.7):
    """Over-relaxed Jacobi (JOR): still only reads the previous full sweep's
    values (unlike Gauss-Seidel), so it stays perfectly parallel across
    cores, while omega in (1,2) substantially speeds up convergence of the
    large-scale pressure error versus plain Jacobi (omega=1)."""
    nx, ny, nz = p0.shape[0] - 2, p0.shape[1] - 2, p0.shape[2] - 2
    dx2, dy2, dz2 = dx * dx, dy * dy, dz * dz
    denom = 2.0 * (1.0 / dx2 + 1.0 / dy2 + 1.0 / dz2)
    p = p0.copy()
    p_new = p0.copy()
    residual = 0.0

    for _ in range(max_iters):
        # Neumann BC (zero-gradient) on pressure at domain faces via ghost mirroring.
        for j in range(ny + 2):
            for k in range(nz + 2):
                p[0, j, k] = p[1, j, k]
                p[nx + 1, j, k] = p[nx, j, k]
        for i in range(nx + 2):
            for k in range(nz + 2):
                p[i, 0, k] = p[i, 1, k]
                p[i, ny + 1, k] = p[i, ny, k]
        for i in range(nx + 2):
            for j in range(ny + 2):
                p[i, j, 0] = p[i, j, 1]
                p[i, j, nz + 1] = p[i, j, nz]

        max_diff = 0.0
        for i in prange(1, nx + 1):
            for j in range(1, ny + 1):
                for k in range(1, nz + 1):
                    if solid[i, j, k]:
                        p_new[i, j, k] = 0.0
                        continue
                    val = (
                        (p[i + 1, j, k] + p[i - 1, j, k]) / dx2
                        + (p[i, j + 1, k] + p[i, j - 1, k]) / dy2
                        + (p[i, j, k + 1] + p[i, j, k - 1]) / dz2
                        - rhs[i, j, k]
                    ) / denom
                    p_new[i, j, k] = p[i, j, k] + omega * (val - p[i, j, k])

        # pin one reference cell to remove the pressure null space
        p_new[1, 1, 1] = 0.0

        max_diff = np.max(np.abs(p_new[1:nx + 1, 1:ny + 1, 1:nz + 1] - p[1:nx + 1, 1:ny + 1, 1:nz + 1]))
        p, p_new = p_new, p
        residual = max_diff
        if max_diff < tol:
            break

    return p, residual


@njit(parallel=True, cache=True)
def _correct_velocity_numba(u, v, w, p, solid, dt, dx, dy, dz):
    nx, ny, nz = u.shape[0] - 2, u.shape[1] - 2, u.shape[2] - 2
    for i in prange(1, nx + 1):
        for j in range(1, ny + 1):
            for k in range(1, nz + 1):
                if solid[i, j, k]:
                    u[i, j, k] = 0.0
                    v[i, j, k] = 0.0
                    w[i, j, k] = 0.0
                    continue
                u[i, j, k] -= dt * (p[i + 1, j, k] - p[i - 1, j, k]) / (2 * dx)
                v[i, j, k] -= dt * (p[i, j + 1, k] - p[i, j - 1, k]) / (2 * dy)
                w[i, j, k] -= dt * (p[i, j, k + 1] - p[i, j, k - 1]) / (2 * dz)
    return u, v, w


def _set_thread_count_numba(n: int):
    numba.set_num_threads(max(1, int(n)))


def _get_thread_count_numba() -> int:
    return numba.get_num_threads()


def _max_thread_count_numba() -> int:
    return numba.config.NUMBA_NUM_THREADS


# ----------------------------------------------------------------------
# C++-backed wrappers. `solid` is handed over as int8 (numpy's bool_ is
# byte-compatible with int8, so `.view(np.int8)` is zero-copy). Arrays
# are indexed to match numpy's native C-contiguous layout directly on
# the C++ side (see kernels.cpp), so no Fortran-order transpose/copy is
# needed at this boundary the way the earlier f2py bridge required.
def _compute_rhs_native(u, v, w, solid, Re, dx, dy, dz):
    u = np.ascontiguousarray(u, dtype=np.float64)
    v = np.ascontiguousarray(v, dtype=np.float64)
    w = np.ascontiguousarray(w, dtype=np.float64)
    solid_i8 = np.ascontiguousarray(solid, dtype=np.bool_).view(np.int8)
    nx, ny, nz = u.shape[0] - 2, u.shape[1] - 2, u.shape[2] - 2
    rhs_u = np.empty_like(u)
    rhs_v = np.empty_like(v)
    rhs_w = np.empty_like(w)
    _lib.compute_rhs(
        _ptr(u), _ptr(v), _ptr(w), _ptr_i8(solid_i8),
        Re, dx, dy, dz,
        nx, ny, nz,
        _ptr(rhs_u), _ptr(rhs_v), _ptr(rhs_w),
    )
    return rhs_u, rhs_v, rhs_w


def _divergence_native(u, v, w, solid, dx, dy, dz):
    u = np.ascontiguousarray(u, dtype=np.float64)
    v = np.ascontiguousarray(v, dtype=np.float64)
    w = np.ascontiguousarray(w, dtype=np.float64)
    solid_i8 = np.ascontiguousarray(solid, dtype=np.bool_).view(np.int8)
    nx, ny, nz = u.shape[0] - 2, u.shape[1] - 2, u.shape[2] - 2
    div = np.empty_like(u)
    _lib.divergence(
        _ptr(u), _ptr(v), _ptr(w), _ptr_i8(solid_i8),
        dx, dy, dz,
        nx, ny, nz,
        _ptr(div),
    )
    return div


def _jacobi_pressure_solve_native(p0, rhs, solid, dx, dy, dz, max_iters, tol, omega=1.7):
    p0 = np.ascontiguousarray(p0, dtype=np.float64)
    rhs = np.ascontiguousarray(rhs, dtype=np.float64)
    solid_i8 = np.ascontiguousarray(solid, dtype=np.bool_).view(np.int8)
    nx, ny, nz = p0.shape[0] - 2, p0.shape[1] - 2, p0.shape[2] - 2
    p_out = np.empty_like(p0)
    residual = ctypes.c_double(0.0)
    _lib.jacobi_pressure_solve(
        _ptr(p0), _ptr(rhs), _ptr_i8(solid_i8),
        dx, dy, dz,
        max_iters, tol, omega,
        nx, ny, nz,
        _ptr(p_out), ctypes.byref(residual),
    )
    return p_out, residual.value


def _correct_velocity_native(u, v, w, p, solid, dt, dx, dy, dz):
    u = np.ascontiguousarray(u, dtype=np.float64)
    v = np.ascontiguousarray(v, dtype=np.float64)
    w = np.ascontiguousarray(w, dtype=np.float64)
    p = np.ascontiguousarray(p, dtype=np.float64)
    solid_i8 = np.ascontiguousarray(solid, dtype=np.bool_).view(np.int8)
    nx, ny, nz = u.shape[0] - 2, u.shape[1] - 2, u.shape[2] - 2
    _lib.correct_velocity(
        _ptr(u), _ptr(v), _ptr(w),
        _ptr(p), _ptr_i8(solid_i8),
        dt, dx, dy, dz,
        nx, ny, nz,
    )
    return u, v, w


def _set_thread_count_native(n: int):
    _lib.set_thread_count(max(1, int(n)))


def _get_thread_count_native() -> int:
    return int(_lib.get_thread_count())


def _max_thread_count_native() -> int:
    return int(_lib.max_thread_count())


# ----------------------------------------------------------------------
# Public API: bound to whichever backend is active. `use_native_backend`
# rebinds these at runtime -- e.g. performance presets switch backends
# per-run, not just once at import time -- and it's safe to call anytime
# since every caller (NavierStokes3DSolver included) looks up
# `kernels.compute_rhs` etc. through the module namespace on every call
# rather than holding a stale local reference.
using_native = False


def use_native_backend(enable: bool) -> bool:
    """Switch backends. Returns the backend actually active afterward
    (True = the compiled C++ kernels, False = numba) -- if `enable` is
    True but the C++ extension isn't available, this silently falls back
    to numba rather than raising, matching this module's overall
    philosophy of always keeping the app working even without the
    compiled build."""
    global compute_rhs, divergence, jacobi_pressure_solve, correct_velocity
    global set_thread_count, get_thread_count, max_thread_count, using_native

    if enable and native_available:
        compute_rhs = _compute_rhs_native
        divergence = _divergence_native
        jacobi_pressure_solve = _jacobi_pressure_solve_native
        correct_velocity = _correct_velocity_native
        set_thread_count = _set_thread_count_native
        get_thread_count = _get_thread_count_native
        max_thread_count = _max_thread_count_native
        using_native = True
    else:
        compute_rhs = _compute_rhs_numba
        divergence = _divergence_numba
        jacobi_pressure_solve = _jacobi_pressure_solve_numba
        correct_velocity = _correct_velocity_numba
        set_thread_count = _set_thread_count_numba
        get_thread_count = _get_thread_count_numba
        max_thread_count = _max_thread_count_numba
        using_native = False
    return using_native


# initial default: numba, unless CFD_KERNELS=native is set (preserves
# the existing environment-variable-based opt-in for anyone using it
# outside the UI's performance presets)
use_native_backend(os.environ.get("CFD_KERNELS", "").strip().lower() == "native")
