"""Background QThread worker that runs the CFD solver loop without
blocking the UI, periodically emitting progress/preview signals and
writing VTK output at a configurable cadence."""
from __future__ import annotations

import time as _time

from PySide6.QtCore import QObject, Signal

from ..io.vtk_writer import VTKSeriesWriter
from ..solver.scenarios import build_solver


class SimulationWorker(QObject):
    progress = Signal(int, int, float, float)  # step, total_steps, residual, elapsed_s
    preview = Signal(object)  # snapshot dict of fields() plus 'x','y' arrays
    finished = Signal(str)  # pvd path
    error = Signal(str)
    stopped = Signal()

    def __init__(
        self,
        scenario_key: str,
        Re: float,
        U: float,
        nx: int,
        ny: int,
        n_steps: int,
        output_every: int,
        output_dir: str,
        case_name: str,
        obstacle_x0=None,
        obstacle_width=None,
        obstacle_height=None,
        dt=None,
    ):
        super().__init__()
        self.scenario_key = scenario_key
        self.Re = Re
        self.U = U
        self.nx = nx
        self.ny = ny
        self.n_steps = n_steps
        self.output_every = max(1, output_every)
        self.output_dir = output_dir
        self.case_name = case_name
        self.obstacle_x0 = obstacle_x0
        self.obstacle_width = obstacle_width
        self.obstacle_height = obstacle_height
        self.dt = dt
        self._stop_requested = False

    def request_stop(self):
        self._stop_requested = True

    def run(self):
        try:
            solver = build_solver(
                self.scenario_key,
                Re=self.Re,
                U=self.U,
                nx=self.nx,
                ny=self.ny,
                obstacle_x0=self.obstacle_x0,
                obstacle_width=self.obstacle_width,
                obstacle_height=self.obstacle_height,
                dt=self.dt,
            )
        except Exception as exc:  # noqa: BLE001
            self.error.emit(f"Failed to build solver: {exc}")
            return

        writer = VTKSeriesWriter(output_dir=self.output_dir, case_name=self.case_name)
        writer.clear()

        # Throttle GUI updates by wall-clock time, not step count: preview
        # rendering (streamplot in particular) is much slower than a solver
        # step, so a fixed-step cadence can flood the GUI thread's event
        # queue and make the app appear to hang on fast/small runs.
        min_update_interval = 0.15
        start = _time.monotonic()
        last_update_time = start

        try:
            writer.write_step(0, solver.time, solver.nx, solver.ny, solver.dx, solver.dy, solver.fields())
            for step in range(1, self.n_steps + 1):
                if self._stop_requested:
                    self.stopped.emit()
                    return
                residual = solver.step()

                if step % self.output_every == 0 or step == self.n_steps:
                    writer.write_step(step, solver.time, solver.nx, solver.ny, solver.dx, solver.dy, solver.fields())

                now = _time.monotonic()
                if now - last_update_time >= min_update_interval or step == self.n_steps:
                    elapsed = now - start
                    self.progress.emit(step, self.n_steps, residual, elapsed)
                    snap = dict(solver.fields())
                    snap["x"] = solver.x
                    snap["y"] = solver.y
                    self.preview.emit(snap)
                    last_update_time = now
        except Exception as exc:  # noqa: BLE001
            self.error.emit(f"Simulation error at step {solver.step_count}: {exc}")
            return

        self.finished.emit(writer.pvd_path)
