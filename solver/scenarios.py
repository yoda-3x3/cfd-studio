"""Preset scenarios for the CFD app, each mapping to a SolverConfig."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from .navier_stokes import Obstacle, SolverConfig, VorticityStreamSolver


@dataclass
class ScenarioPreset:
    key: str
    label: str
    description: str
    kind: str
    Lx: float
    Ly: float
    default_nx: int
    default_ny: int
    default_Re: float
    default_U: float
    has_obstacle: bool = False


PRESETS = {
    "cavity": ScenarioPreset(
        key="cavity",
        label="Lid-Driven Cavity",
        description=(
            "Classic benchmark: a square cavity with all walls stationary "
            "except the top, which slides at a constant speed, driving a "
            "recirculating vortex."
        ),
        kind="cavity",
        Lx=1.0,
        Ly=1.0,
        default_nx=81,
        default_ny=81,
        default_Re=400.0,
        default_U=1.0,
    ),
    "channel": ScenarioPreset(
        key="channel",
        label="Channel Flow",
        description=(
            "Uniform flow enters a horizontal channel with no-slip top and "
            "bottom walls and develops toward a parabolic (Poiseuille) "
            "profile downstream."
        ),
        kind="channel",
        Lx=3.0,
        Ly=1.0,
        default_nx=181,
        default_ny=61,
        default_Re=200.0,
        default_U=1.0,
    ),
    "obstacle": ScenarioPreset(
        key="obstacle",
        label="Flow Past a Wall-Mounted Obstacle",
        description=(
            "Channel flow over a rectangular bump mounted on the bottom "
            "wall, producing flow acceleration over the top and a "
            "recirculation zone downstream."
        ),
        kind="obstacle",
        Lx=4.0,
        Ly=1.0,
        default_nx=241,
        default_ny=61,
        default_Re=200.0,
        default_U=1.0,
        has_obstacle=True,
    ),
}


def build_solver(
    preset_key: str,
    Re: float,
    U: float,
    nx: int,
    ny: int,
    obstacle_x0: Optional[float] = None,
    obstacle_width: Optional[float] = None,
    obstacle_height: Optional[float] = None,
    dt: Optional[float] = None,
) -> VorticityStreamSolver:
    preset = PRESETS[preset_key]
    obstacle = None
    if preset.has_obstacle:
        obstacle = Obstacle(
            x0=obstacle_x0 if obstacle_x0 is not None else preset.Lx * 0.35,
            width=obstacle_width if obstacle_width is not None else preset.Lx * 0.15,
            height=obstacle_height if obstacle_height is not None else preset.Ly * 0.3,
        )
    config = SolverConfig(
        nx=nx,
        ny=ny,
        Lx=preset.Lx,
        Ly=preset.Ly,
        Re=Re,
        kind=preset.kind,
        U=U,
        obstacle=obstacle,
        dt=dt,
    )
    return VorticityStreamSolver(config)
