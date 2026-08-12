"""
Two-tier disk cache so re-running a file you've already processed doesn't
redo work that's already been done:

  1. Geometry cache: keyed on the oriented mesh's actual vertex/face data
     plus grid resolution and domain-padding settings. A hit skips the
     mesh-containment voxelization query (independent of Re/steps/etc,
     since those don't change the solid mask).
  2. Run cache: keyed additionally on Re, inflow speed, step count, and
     output cadence. A hit means an identical simulation has already been
     run to completion — skip the run entirely and reuse its output.
"""
from __future__ import annotations

import hashlib
import json
import os
import time
from typing import Optional, Tuple

import numpy as np


def _cache_root() -> str:
    root = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "cache")
    os.makedirs(os.path.join(root, "geometry"), exist_ok=True)
    return root


def mesh_signature(mesh) -> str:
    h = hashlib.sha256()
    h.update(np.ascontiguousarray(mesh.vertices, dtype=np.float64).tobytes())
    h.update(np.ascontiguousarray(mesh.faces, dtype=np.int64).tobytes())
    return h.hexdigest()[:24]


def geometry_key(
    mesh_sig: str, nx: int, ny: int, nz: int, inflow_gap: float, wake_gap: float, lateral_gap: float,
    domain_mode: str = "external",
) -> str:
    payload = f"{mesh_sig}|{nx}|{ny}|{nz}|{inflow_gap:.6g}|{wake_gap:.6g}|{lateral_gap:.6g}|{domain_mode}"
    return hashlib.sha256(payload.encode()).hexdigest()[:24]


def run_key(geom_key: str, Re: float, U_in: float, n_steps: int, output_every: int) -> str:
    payload = f"{geom_key}|{Re:.6g}|{U_in:.6g}|{n_steps}|{output_every}"
    return hashlib.sha256(payload.encode()).hexdigest()[:24]


# ------------------------------------------------------------------
def load_geometry(geom_key: str) -> Optional[Tuple[np.ndarray, float, float, float]]:
    path = os.path.join(_cache_root(), "geometry", f"{geom_key}.npz")
    if not os.path.exists(path):
        return None
    try:
        data = np.load(path)
        return data["solid"], float(data["Lx"]), float(data["Ly"]), float(data["Lz"])
    except Exception:
        return None


def save_geometry(geom_key: str, solid: np.ndarray, Lx: float, Ly: float, Lz: float):
    path = os.path.join(_cache_root(), "geometry", f"{geom_key}.npz")
    np.savez_compressed(path, solid=solid, Lx=Lx, Ly=Ly, Lz=Lz)


# ------------------------------------------------------------------
def _manifest_path() -> str:
    return os.path.join(_cache_root(), "run_manifest.json")


def _load_manifest() -> dict:
    path = _manifest_path()
    if os.path.exists(path):
        try:
            with open(path, "r") as f:
                return json.load(f)
        except (json.JSONDecodeError, OSError):
            return {}
    return {}


def _save_manifest(manifest: dict):
    with open(_manifest_path(), "w") as f:
        json.dump(manifest, f, indent=2)


def load_run(key: str) -> Optional[str]:
    """Return the cached .pvd path if a prior identical run's output still exists."""
    manifest = _load_manifest()
    entry = manifest.get(key)
    if not entry:
        return None
    pvd_path = entry.get("pvd_path")
    if pvd_path and os.path.isfile(pvd_path):
        return pvd_path
    return None


def save_run(key: str, pvd_path: str, mesh_name: str):
    manifest = _load_manifest()
    manifest[key] = {
        "pvd_path": pvd_path,
        "mesh_name": mesh_name,
        "created_at": time.time(),
    }
    _save_manifest(manifest)
