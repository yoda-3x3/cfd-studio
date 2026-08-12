"""
Fluid property presets, for deriving a Reynolds number from real physical
inputs (characteristic length, velocity, fluid) instead of entering Re
directly. Only kinematic viscosity (nu) feeds the solver math (Re = U*L/nu);
density is stored/displayed for reference only — the solver is
non-dimensional and has no density-dependent terms.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass
class MaterialPreset:
    label: str
    rho: float  # kg/m^3 — reference/display only, not used in Re
    nu: float  # m^2/s — kinematic viscosity, used in Re = U*L/nu


PRESETS: dict[str, MaterialPreset] = {
    "air_20c": MaterialPreset("Air (20°C, 1 atm)", rho=1.204, nu=1.516e-5),
    "water_20c": MaterialPreset("Water (20°C)", rho=998.2, nu=1.004e-6),
}

CUSTOM_KEY = "custom"


def reynolds_number(velocity: float, length: float, nu: float) -> float:
    if nu <= 0:
        return 0.0
    return velocity * length / nu
