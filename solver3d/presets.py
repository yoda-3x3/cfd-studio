"""
Performance presets for the 3D solver: bundle grid resolution, run
length, output cadence, and kernel-backend preference into a few named
choices, instead of the user having to reason about each knob
separately.

Backend preference follows what's actually measured (see kernels.py /
build_native.py): the no-threading compiled C++ kernels have zero
threading overhead per call, which wins on small grids where numba's
per-call thread-dispatch cost dominates -- but numba's real multi-core
scaling wins once a grid is big enough to give it enough work to
amortize that cost. There's no single "just always faster" backend, so
presets pick the one that's actually faster at their own grid size, not
a fixed global default.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass
class PerformancePreset:
    key: str
    label: str
    description: str
    nx: int
    ny: int
    nz: int
    steps: int
    output_every: int
    prefer_native: bool


PRESETS: dict[str, PerformancePreset] = {
    "fast_preview": PerformancePreset(
        key="fast_preview",
        label="Fast Preview",
        description=(
            "Small grid, short run -- for quickly sanity-checking orientation, "
            "domain setup, and general flow shape before committing to a full run. "
            "Prefers the single-threaded compiled C++ kernels, which measurably win "
            "at this grid size (no per-call multi-thread dispatch overhead)."
        ),
        nx=50, ny=30, nz=30,
        steps=500, output_every=25,
        prefer_native=True,
    ),
    "balanced": PerformancePreset(
        key="balanced",
        label="Balanced",
        description="The app's normal defaults -- a reasonable trade-off of runtime vs. resolution for most cases.",
        nx=100, ny=60, nz=60,
        steps=1500, output_every=25,
        prefer_native=False,
    ),
    "high_quality": PerformancePreset(
        key="high_quality",
        label="High Quality",
        description=(
            "Larger grid, longer run, for a final/publication-quality result once "
            "you've already confirmed the setup looks right. Uses numba, which wins "
            "at this grid size thanks to real multi-core scaling."
        ),
        nx=160, ny=100, nz=100,
        steps=3000, output_every=50,
        prefer_native=False,
    ),
}

DEFAULT_PRESET_KEY = "balanced"
