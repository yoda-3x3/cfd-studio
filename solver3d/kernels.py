"""
Numba-accelerated, multi-core kernels for the 3D incompressible
Navier-Stokes solver (collocated grid, explicit projection method).

All arrays are padded with a 1-cell ghost layer on every side:
shape (nx+2, ny+2, nz+2), with real cells at indices 1..nx, 1..ny, 1..nz.

The outer loop of every kernel is parallelized across the i-index with
numba's `prange`, so a single simulation step spreads across all
available CPU cores.
"""
from __future__ import annotations

import numba
import numpy as np
from numba import njit, prange


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
def compute_rhs(u, v, w, solid, Re, dx, dy, dz):
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
def divergence(u, v, w, solid, dx, dy, dz):
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
def jacobi_pressure_solve(p0, rhs, solid, dx, dy, dz, max_iters, tol, omega=1.7):
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
def correct_velocity(u, v, w, p, solid, dt, dx, dy, dz):
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


def set_thread_count(n: int):
    numba.set_num_threads(max(1, int(n)))


def get_thread_count() -> int:
    return numba.get_num_threads()


def max_thread_count() -> int:
    return numba.config.NUMBA_NUM_THREADS
