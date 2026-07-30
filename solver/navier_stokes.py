"""
2D incompressible Navier-Stokes solver using the vorticity-streamfunction
formulation on a collocated grid.

    u = d(psi)/dy         v = -d(psi)/dx
    d(omega)/dt + u*d(omega)/dx + v*d(omega)/dy = (1/Re) * laplacian(omega)
    laplacian(psi) = -omega

Boundary vorticity is enforced with Thom's formula, which also handles
no-slip walls, a moving lid, and a solid rectangular obstacle mounted on
the bottom wall (treated as an internal wall with the same formula).

Time integration is explicit (forward Euler with upwind convection for the
vorticity transport); the streamfunction Poisson equation is solved with a
sparse LU factorization computed once and reused every step, since the
matrix depends only on grid/geometry, not on the solution.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Optional

import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla


@dataclass
class Obstacle:
    """Rectangular obstacle mounted on the bottom wall, in physical units."""
    x0: float
    width: float
    height: float

    def x1(self) -> float:
        return self.x0 + self.width


@dataclass
class SolverConfig:
    nx: int = 81
    ny: int = 81
    Lx: float = 1.0
    Ly: float = 1.0
    Re: float = 100.0
    kind: str = "cavity"  # "cavity" | "channel" | "obstacle"
    U: float = 1.0  # lid speed (cavity) or inflow speed (channel/obstacle)
    obstacle: Optional[Obstacle] = None
    dt: Optional[float] = None  # None => auto-compute a stable dt
    safety_factor: float = 0.5


class VorticityStreamSolver:
    def __init__(self, config: SolverConfig):
        self.cfg = config
        nx, ny = config.nx, config.ny
        self.nx, self.ny = nx, ny
        self.dx = config.Lx / (nx - 1)
        self.dy = config.Ly / (ny - 1)
        self.x = np.linspace(0.0, config.Lx, nx)
        self.y = np.linspace(0.0, config.Ly, ny)

        self.psi = np.zeros((ny, nx))
        self.omega = np.zeros((ny, nx))
        self.u = np.zeros((ny, nx))
        self.v = np.zeros((ny, nx))

        self.solid = np.zeros((ny, nx), dtype=bool)
        if config.kind == "obstacle" and config.obstacle is not None:
            ob = config.obstacle
            xmask = (self.x >= ob.x0) & (self.x <= ob.x1())
            ymask = self.y <= ob.height
            self.solid[np.ix_(ymask, xmask)] = True

        self.is_channel = config.kind in ("channel", "obstacle")
        self.Q = config.U * config.Ly  # volumetric flow rate (channel top psi)

        self._classify_nodes()
        self._set_fixed_psi_values()
        self._build_and_factorize_laplacian()

        self.time = 0.0
        self.step_count = 0
        self.last_residual = 0.0

        if config.dt is not None:
            self.dt = config.dt
        else:
            self.dt = self._auto_dt()

    # ------------------------------------------------------------------
    def _classify_nodes(self):
        ny, nx = self.ny, self.nx
        edge = np.zeros((ny, nx), dtype=bool)
        edge[0, :] = True
        edge[-1, :] = True
        edge[:, 0] = True
        edge[:, -1] = True

        self.is_left = np.zeros((ny, nx), dtype=bool)
        self.is_right = np.zeros((ny, nx), dtype=bool)
        self.is_bottom = np.zeros((ny, nx), dtype=bool)
        self.is_top = np.zeros((ny, nx), dtype=bool)
        self.is_left[:, 0] = True
        self.is_right[:, -1] = True
        self.is_bottom[0, :] = True
        self.is_top[-1, :] = True

        # fixed = domain edges + solid (obstacle) nodes; everything else unknown
        self.fixed = edge | self.solid
        self.unknown = ~self.fixed

        idx = -np.ones((ny, nx), dtype=int)
        idx[self.unknown] = np.arange(int(self.unknown.sum()))
        self.idx = idx
        self.n_unknown = int(self.unknown.sum())

    # ------------------------------------------------------------------
    def _set_fixed_psi_values(self):
        ny, nx = self.ny, self.nx
        psi = self.psi
        if self.cfg.kind == "cavity":
            psi[self.fixed] = 0.0
        else:
            # channel / obstacle: bottom wall + obstacle = 0, top wall = Q,
            # inflow column = linear profile U*y, outflow extrapolated later.
            # Initialize the WHOLE field (including interior) to the uniform
            # inflow profile so the initial condition is smooth/consistent
            # instead of jumping straight from 0 to Q over one grid cell.
            psi[:, :] = self.cfg.U * self.y[:, None]
            psi[self.solid] = 0.0
            psi[self.is_top] = self.Q
            psi[self.is_left] = self.cfg.U * self.y
            psi[self.is_right] = self.cfg.U * self.y

    # ------------------------------------------------------------------
    def _build_and_factorize_laplacian(self):
        """Assemble the sparse Laplacian operator for unknown psi nodes."""
        ny, nx = self.ny, self.nx
        dx2, dy2 = self.dx ** 2, self.dy ** 2
        cx, cy = 1.0 / dx2, 1.0 / dy2
        cc = -2.0 * (cx + cy)

        rows, cols, vals = [], [], []
        n = self.n_unknown
        idx = self.idx

        js, is_ = np.where(self.unknown)
        for j, i in zip(js, is_):
            k = idx[j, i]
            rows.append(k)
            cols.append(k)
            vals.append(cc)
            for (dj, di, c) in ((0, -1, cx), (0, 1, cx), (-1, 0, cy), (1, 0, cy)):
                nj, ni = j + dj, i + di
                if self.unknown[nj, ni]:
                    rows.append(k)
                    cols.append(idx[nj, ni])
                    vals.append(c)
                # if neighbor is fixed, its contribution goes to the RHS at solve time

        A = sp.csc_matrix((vals, (rows, cols)), shape=(n, n))
        self._lu = spla.splu(A)

    # ------------------------------------------------------------------
    def _auto_dt(self) -> float:
        # Combined explicit-scheme stability bound (diffusion + convection
        # acting together in the same forward-Euler update), rather than
        # taking the min of two separate limits, which underestimates the
        # restriction when both terms are significant at once.
        Umax = max(self.cfg.U, 1e-6)
        diff_term = 2.0 * (1.0 / self.cfg.Re) * (1.0 / self.dx ** 2 + 1.0 / self.dy ** 2)
        conv_term = Umax / self.dx + Umax / self.dy
        dt_max = 1.0 / (diff_term + conv_term)
        return self.cfg.safety_factor * dt_max

    # ------------------------------------------------------------------
    def _wall_omega(self, j, i, dirs):
        """Thom's-formula vorticity at a fixed node (j,i) using the fluid/
        interior neighbor(s) indicated by dirs, a list of (dj, di, tangential_velocity, h)."""
        psi = self.psi
        vals = []
        for dj, di, ut, h in dirs:
            nj, ni = j + dj, i + di
            psi_wall = psi[j, i]
            psi_nb = psi[nj, ni]
            # Thom's formula; sign of the velocity term depends on orientation.
            # dj,di point FROM the wall node TOWARD the fluid.
            sign = 1.0
            if di == 1 or dj == 1:
                sign = -1.0
            vals.append(2.0 * (psi_wall - psi_nb) / (h * h) + sign * 2.0 * ut / h)
        return float(np.mean(vals)) if vals else 0.0

    def _update_boundary_and_obstacle_vorticity(self):
        ny, nx = self.ny, self.nx
        dx, dy = self.dx, self.dy
        U = self.cfg.U

        # Domain walls (each fixed edge node has exactly one interior neighbor).
        for j in range(ny):
            self.omega[j, 0] = self._wall_omega(j, 0, [(0, 1, 0.0, dx)]) if not self.solid[j, 0] else self.omega[j, 0]
        for j in range(ny):
            if not self.solid[j, nx - 1]:
                self.omega[j, nx - 1] = self._wall_omega(j, nx - 1, [(0, -1, 0.0, dx)])
        for i in range(nx):
            if not self.solid[0, i]:
                self.omega[0, i] = self._wall_omega(0, i, [(1, 0, 0.0, dy)])
        lid_u = U if self.cfg.kind == "cavity" else 0.0
        for i in range(nx):
            if not self.solid[ny - 1, i]:
                self.omega[ny - 1, i] = self._wall_omega(ny - 1, i, [(-1, 0, lid_u, dy)])

        if self.cfg.kind == "cavity":
            # corners already covered by the loops above (overwritten twice, fine)
            pass

        if self.solid.any():
            solid_js, solid_is = np.where(self.solid)
            for j, i in zip(solid_js, solid_is):
                dirs = []
                if j + 1 < ny and not self.solid[j + 1, i]:
                    dirs.append((1, 0, 0.0, dy))
                if j - 1 >= 0 and not self.solid[j - 1, i]:
                    dirs.append((-1, 0, 0.0, dy))
                if i + 1 < nx and not self.solid[j, i + 1]:
                    dirs.append((0, 1, 0.0, dx))
                if i - 1 >= 0 and not self.solid[j, i - 1]:
                    dirs.append((0, -1, 0.0, dx))
                if dirs:
                    self.omega[j, i] = self._wall_omega(j, i, dirs)

        if self.is_channel:
            # inflow: irrotational uniform stream
            self.omega[:, 0] = 0.0
            # outflow: zero-gradient (convective) extrapolation
            self.omega[:, -1] = self.omega[:, -2]
            self.psi[:, -1] = 2 * self.psi[:, -2] - self.psi[:, -3]
            self.psi[0, -1] = 0.0
            self.psi[-1, -1] = self.Q

    # ------------------------------------------------------------------
    def _compute_velocity(self):
        dx, dy = self.dx, self.dy
        psi = self.psi
        u = np.zeros_like(psi)
        v = np.zeros_like(psi)
        u[1:-1, :] = (psi[2:, :] - psi[:-2, :]) / (2 * dy)
        v[:, 1:-1] = -(psi[:, 2:] - psi[:, :-2]) / (2 * dx)

        u[0, :] = 0.0
        u[-1, :] = self.cfg.U if self.cfg.kind == "cavity" else 0.0
        if self.is_channel:
            u[:, 0] = self.cfg.U
            u[:, -1] = u[:, -2]
            v[:, 0] = 0.0
            v[:, -1] = v[:, -2]
        u[self.solid] = 0.0
        v[self.solid] = 0.0
        self.u, self.v = u, v

    # ------------------------------------------------------------------
    def _vorticity_transport(self) -> np.ndarray:
        dx, dy, Re = self.dx, self.dy, self.cfg.Re
        omega = self.omega
        u, v = self.u, self.v

        domega_dx = np.zeros_like(omega)
        domega_dy = np.zeros_like(omega)

        u_pos = u > 0
        domega_dx[:, 1:] = np.where(u_pos[:, 1:], (omega[:, 1:] - omega[:, :-1]) / dx, 0.0)
        domega_dx[:, :-1] += np.where(~u_pos[:, :-1], (omega[:, 1:] - omega[:, :-1]) / dx, 0.0)

        v_pos = v > 0
        domega_dy[1:, :] = np.where(v_pos[1:, :], (omega[1:, :] - omega[:-1, :]) / dy, 0.0)
        domega_dy[:-1, :] += np.where(~v_pos[:-1, :], (omega[1:, :] - omega[:-1, :]) / dy, 0.0)

        lap = np.zeros_like(omega)
        lap[1:-1, 1:-1] = (
            (omega[1:-1, 2:] - 2 * omega[1:-1, 1:-1] + omega[1:-1, :-2]) / dx ** 2
            + (omega[2:, 1:-1] - 2 * omega[1:-1, 1:-1] + omega[:-2, 1:-1]) / dy ** 2
        )

        rhs = -(u * domega_dx + v * domega_dy) + lap / Re
        return rhs

    # ------------------------------------------------------------------
    def _solve_psi(self):
        ny, nx = self.ny, self.nx
        dx2, dy2 = self.dx ** 2, self.dy ** 2
        cx, cy = 1.0 / dx2, 1.0 / dy2

        b = -self.omega[self.unknown].copy()
        js, is_ = np.where(self.unknown)
        psi = self.psi
        for k, (j, i) in enumerate(zip(js, is_)):
            for dj, di, c in ((0, -1, cx), (0, 1, cx), (-1, 0, cy), (1, 0, cy)):
                nj, ni = j + dj, i + di
                if self.fixed[nj, ni]:
                    b[k] -= c * psi[nj, ni]

        sol = self._lu.solve(b)
        new_psi = psi.copy()
        new_psi[self.unknown] = sol
        residual = float(np.max(np.abs(new_psi[self.unknown] - psi[self.unknown]))) if self.n_unknown else 0.0
        self.psi = new_psi
        return residual

    # ------------------------------------------------------------------
    def step(self) -> float:
        """Advance the solution by one time step. Returns a residual metric."""
        self._update_boundary_and_obstacle_vorticity()
        self._compute_velocity()
        domega_dt = self._vorticity_transport()
        self.omega = self.omega + self.dt * domega_dt
        self.omega[self.solid] = 0.0
        residual = self._solve_psi()
        self._compute_velocity()

        self.time += self.dt
        self.step_count += 1
        self.last_residual = residual
        return residual

    # ------------------------------------------------------------------
    def velocity_magnitude(self) -> np.ndarray:
        return np.sqrt(self.u ** 2 + self.v ** 2)

    def fields(self) -> dict:
        return {
            "velocity_u": self.u,
            "velocity_v": self.v,
            "velocity_magnitude": self.velocity_magnitude(),
            "vorticity": self.omega,
            "streamfunction": self.psi,
            "obstacle": self.solid.astype(np.float32),
        }
