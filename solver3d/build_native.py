"""Compile kernels.cpp into `_kernels_cpp.dll`, the optional compiled
backend kernels.py loads via ctypes when available (falls back to the
numba implementation otherwise). Needs a C++ compiler (g++); nothing
extra is needed at runtime once this has been run once -- statically
linked, so the resulting DLL has no MinGW runtime DLLs to bundle
(confirmed via `ntldd -R` on the build output: only Windows system DLLs
remain as dependencies).

Usage:
    python solver3d/build_native.py
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main() -> int:
    cmd = [
        "g++",
        "-O3",
        # NOT -march=native: that bakes in the *build* machine's specific
        # CPU instruction set, which can crash (illegal instruction, not a
        # catchable Python exception) on a different machine at runtime.
        # x86-64-v2 is the safe portable baseline for any CPU capable of
        # running Windows 11 -- covers effectively all real target machines.
        "-march=x86-64-v2", "-mtune=generic",
        "-shared", "-static",  # static: no runtime DLLs to bundle/find at import time
        "-o", "_kernels_cpp.dll",
        "kernels.cpp",
    ]
    print("Running:", " ".join(cmd))
    result = subprocess.run(cmd, cwd=HERE)
    if result.returncode != 0:
        print("Build failed.", file=sys.stderr)
        return result.returncode
    print("Build succeeded: _kernels_cpp.dll is ready in", HERE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
