"""Locate a ParaView install and launch it against a result file, with the
chosen path persisted so the user only has to locate it once."""
from __future__ import annotations

import glob
import json
import os
import subprocess
from typing import List, Optional


def _config_dir() -> str:
    base = os.environ.get("APPDATA") or os.path.expanduser("~")
    path = os.path.join(base, "CFDParaviewApp")
    os.makedirs(path, exist_ok=True)
    return path


def _config_path() -> str:
    return os.path.join(_config_dir(), "config.json")


def load_config() -> dict:
    path = _config_path()
    if os.path.exists(path):
        try:
            with open(path, "r") as f:
                return json.load(f)
        except (json.JSONDecodeError, OSError):
            return {}
    return {}


def save_config(cfg: dict):
    with open(_config_path(), "w") as f:
        json.dump(cfg, f, indent=2)


def find_paraview_candidates() -> List[str]:
    patterns = [
        r"C:\Program Files\ParaView*\bin\paraview.exe",
        r"C:\Program Files (x86)\ParaView*\bin\paraview.exe",
        os.path.expanduser(r"~\AppData\Local\Programs\ParaView*\bin\paraview.exe"),
    ]
    found = []
    for pattern in patterns:
        found.extend(glob.glob(pattern))
    # prefer newest-looking version string, but keep all for the user to pick
    return sorted(set(found), reverse=True)


def get_paraview_path() -> Optional[str]:
    cfg = load_config()
    saved = cfg.get("paraview_path")
    if saved and os.path.isfile(saved):
        return saved
    candidates = find_paraview_candidates()
    return candidates[0] if candidates else None


def set_paraview_path(path: str):
    if not os.path.isfile(path):
        raise FileNotFoundError(f"No such file: {path}")
    cfg = load_config()
    cfg["paraview_path"] = path
    save_config(cfg)


def launch_paraview(data_file: str, paraview_exe: Optional[str] = None) -> subprocess.Popen:
    exe = paraview_exe or get_paraview_path()
    if not exe or not os.path.isfile(exe):
        raise FileNotFoundError(
            "ParaView executable not found. Please locate it via Settings > Locate ParaView..."
        )
    if not os.path.isfile(data_file):
        raise FileNotFoundError(f"Result file not found: {data_file}")
    return subprocess.Popen([exe, "--data", data_file])
