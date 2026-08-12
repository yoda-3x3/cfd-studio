"""Background QThread worker for the 3D custom-geometry solver: takes the
already-loaded, already-oriented mesh (orientation is confirmed by the user
before Run is ever clicked), voxelizes it onto the grid, runs the
multi-core solver, and writes a real OpenFOAM case (openable in ParaView
via its .foam placeholder). Checks the on-disk cache first so re-running
an identical file+settings combination doesn't redo work already done."""
from __future__ import annotations

import time as _time

from PySide6.QtCore import QObject, Signal

from ..io.openfoam_writer import OpenFoamCaseWriter
from ..solver3d import cache, geometry, kernels
from ..solver3d.navier_stokes3d import NavierStokes3DSolver, SolverConfig3D


class Simulation3DWorker(QObject):
    status = Signal(str)
    progress = Signal(int, int, float, float)  # step, total_steps, residual, elapsed_s
    preview = Signal(object)  # dict: mid-plane 2D slice through the 3D field
    finished = Signal(str, bool)  # .foam path, was_cached
    error = Signal(str)
    stopped = Signal()

    def __init__(
        self,
        mesh,
        mesh_name: str,
        nx: int,
        ny: int,
        nz: int,
        Re: float,
        U_in: float,
        n_steps: int,
        output_every: int,
        output_dir: str,
        num_threads: int,
        inflow_gap: float = 1.5,
        wake_gap: float = 4.0,
        lateral_gap: float = 1.5,
        force_rerun: bool = False,
        domain_mode: str = "external",
    ):
        super().__init__()
        self.mesh = mesh
        self.mesh_name = mesh_name
        self.nx, self.ny, self.nz = nx, ny, nz
        self.Re = Re
        self.U_in = U_in
        self.n_steps = n_steps
        self.output_every = max(1, output_every)
        self.output_dir = output_dir
        self.num_threads = num_threads
        self.inflow_gap = inflow_gap
        self.wake_gap = wake_gap
        self.lateral_gap = lateral_gap
        self.force_rerun = force_rerun
        self.domain_mode = domain_mode
        self._stop_requested = False

    def request_stop(self):
        self._stop_requested = True

    def run(self):
        try:
            internal = self.domain_mode == "internal"
            mesh_sig = cache.mesh_signature(self.mesh)
            geom_key = cache.geometry_key(
                mesh_sig, self.nx, self.ny, self.nz, self.inflow_gap, self.wake_gap, self.lateral_gap,
                domain_mode=self.domain_mode,
            )
            run_cache_key = cache.run_key(geom_key, self.Re, self.U_in, self.n_steps, self.output_every)

            if not self.force_rerun:
                cached_foam = cache.load_run(run_cache_key)
                if cached_foam:
                    self.status.emit(f"Found identical prior run — reusing cached result (no recompute needed).")
                    self.finished.emit(cached_foam, True)
                    return

            self.status.emit("Preparing geometry...")
            if internal:
                geo = geometry.prepare_internal_geometry(
                    self.mesh, inflow_gap=self.inflow_gap, wake_gap=self.wake_gap
                )
            else:
                geo = geometry.prepare_geometry(
                    self.mesh, inflow_gap=self.inflow_gap, wake_gap=self.wake_gap, lateral_gap=self.lateral_gap
                )

            cached_geom = None if self.force_rerun else cache.load_geometry(geom_key)
            if cached_geom is not None:
                self.status.emit("Reusing cached voxelization (geometry/grid unchanged)...")
                solid, Lx, Ly, Lz = cached_geom
            else:
                self.status.emit(f"Voxelizing geometry onto a {self.nx}x{self.ny}x{self.nz} grid...")
                if internal:
                    solid = geometry.voxelize_internal_to_grid(geo.mesh, self.nx, self.ny, self.nz, geo.Lx, geo.Ly, geo.Lz)
                else:
                    solid = geometry.voxelize_to_grid(geo.mesh, self.nx, self.ny, self.nz, geo.Lx, geo.Ly, geo.Lz)
                Lx, Ly, Lz = geo.Lx, geo.Ly, geo.Lz
                cache.save_geometry(geom_key, solid, Lx, Ly, Lz)

            if solid.sum() == 0:
                self.error.emit(
                    "The uploaded geometry did not intersect the simulation grid at all "
                    "(it may be degenerate, or the grid too coarse). Try a finer grid."
                )
                return

            kernels.set_thread_count(self.num_threads)
            cfg = SolverConfig3D(
                nx=self.nx, ny=self.ny, nz=self.nz,
                Lx=Lx, Ly=Ly, Lz=Lz,
                Re=self.Re, U_in=self.U_in,
                num_threads=self.num_threads,
                domain_mode=self.domain_mode,
            )

            self.status.emit(f"Compiling parallel kernels for {self.num_threads} core(s) (first run only)...")
            solver = NavierStokes3DSolver(cfg, solid_mask=solid)
        except Exception as exc:  # noqa: BLE001
            self.error.emit(f"Failed to set up simulation: {exc}")
            return

        try:
            self.status.emit("Writing OpenFOAM mesh (fitting real surface geometry to the boundary cells)...")
            writer = OpenFoamCaseWriter(
                self.output_dir, self.nx, self.ny, self.nz, solver.dx, solver.dy, solver.dz, solid,
                surface_mesh=geo.mesh, domain_mode=self.domain_mode,
            )
            total = writer.n_cells_smoothed + writer.n_cells_fallback
            if total:
                self.status.emit(
                    f"Mesh ready: {writer.n_cells_smoothed}/{total} surface cells fitted to the real "
                    f"uploaded geometry ({writer.n_cells_fallback} kept as flat voxel faces)."
                )
        except Exception as exc:  # noqa: BLE001
            self.error.emit(f"Failed to write OpenFOAM mesh: {exc}")
            return

        self.status.emit("Running...")
        k_mid = self.nz // 2
        j_mid = self.ny // 2
        min_update_interval = 0.15
        start = _time.monotonic()
        last_update_time = start

        try:
            writer.write_timestep(solver.time, solver.fields())
            for step in range(1, self.n_steps + 1):
                if self._stop_requested:
                    self.stopped.emit()
                    return
                residual = solver.step()

                if step % self.output_every == 0 or step == self.n_steps:
                    writer.write_timestep(solver.time, solver.fields())

                now = _time.monotonic()
                if now - last_update_time >= min_update_interval or step == self.n_steps:
                    elapsed = now - start
                    self.progress.emit(step, self.n_steps, residual, elapsed)
                    fields = solver.fields()
                    slice_snap = {
                        "x": solver.x,
                        "y": solver.y,
                        "z": solver.z,
                        # XY mid-plane slice (z = Lz/2)
                        "velocity_magnitude": fields["velocity_magnitude"][:, :, k_mid],
                        "velocity_u": fields["velocity_u"][:, :, k_mid],
                        "velocity_v": fields["velocity_v"][:, :, k_mid],
                        "pressure": fields["pressure"][:, :, k_mid],
                        "obstacle": fields["obstacle"][:, :, k_mid],
                        # XZ mid-plane slice (y = Ly/2) -- a second, orthogonal
                        # view so the live preview isn't blind to spanwise/
                        # vertical structure between updates
                        "velocity_magnitude_xz": fields["velocity_magnitude"][:, j_mid, :],
                        "velocity_u_xz": fields["velocity_u"][:, j_mid, :],
                        "velocity_w_xz": fields["velocity_w"][:, j_mid, :],
                        "obstacle_xz": fields["obstacle"][:, j_mid, :],
                    }
                    self.preview.emit(slice_snap)
                    last_update_time = now
        except Exception as exc:  # noqa: BLE001
            self.error.emit(f"Simulation error at step {solver.step_count}: {exc}")
            return

        cache.save_run(run_cache_key, writer.foam_path, self.mesh_name)
        self.finished.emit(writer.foam_path, False)
