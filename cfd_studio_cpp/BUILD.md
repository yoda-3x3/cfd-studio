# Building CFD Studio (C++)

## One-time setup

None of this touches your permanent system PATH — `build.ps1` sets up the
right toolchain for its own process only. Run these once to install the
prerequisites at the fixed locations `build.ps1` expects.

1. **CMake** (if not already installed — check with `cmake --version`):
   ```powershell
   winget install -e --id Kitware.CMake --accept-package-agreements --accept-source-agreements
   ```

2. **Qt 6.9.3 + its matching MinGW 13.1.0 compiler**, via `aqtinstall` (no
   Qt account or GUI installer needed — pulls official binaries directly):
   ```powershell
   pip install aqtinstall
   python -m aqt install-qt windows desktop 6.9.3 win64_mingw -O C:\Users\reach\Dance\Qt
   python -m aqt install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 -O C:\Users\reach\Dance\Qt
   ```
   Must be MinGW 13.1.0 specifically — it's the exact compiler Qt's
   prebuilt 6.9.3 MinGW binaries were built with. A different GCC version
   compiling against them is an ABI risk, not just a "might not link" one.

3. **vcpkg** (provides `eigen3` and `qhull`):
   ```powershell
   cd C:\Users\reach\Dance
   git clone https://github.com/microsoft/vcpkg.git
   .\vcpkg\bootstrap-vcpkg.bat
   ```

4. **Embree** (mesh ray-casting/BVH), the official prebuilt Windows binary —
   **not** vcpkg's `embree` port, which pulls in `tbb`, and `tbb` fails to
   compile on this MinGW toolchain (a real upstream TBB/MinGW bug in its
   ITT-notify profiling code, confirmed by direct testing, not something
   fixable from this project's side):
   ```powershell
   mkdir C:\Users\reach\Dance\third_party
   cd C:\Users\reach\Dance\third_party
   # Download the *.windows.zip asset from the latest release:
   # https://github.com/RenderKit/embree/releases  (this project was built against v4.4.0)
   Expand-Archive embree-4.4.0.x64.windows.zip -DestinationPath .
   ```
   Mixing this MSVC-built binary into a MinGW project is safe here because
   Embree's public API is pure C (`extern "C"`) — no C++ ABI crosses the
   boundary. Confirmed with a standalone link+run spike before adopting
   this as the real integration path.

## Building

```powershell
cd C:\Users\reach\Dance\cfd_paraview_app\cfd_studio_cpp
.\build.ps1          # configure + build + run tests
.\build.ps1 -Clean   # wipe build/ and vcpkg_installed/ and start fresh
.\build.ps1 -SkipTests
```

Output:
- `build\cli\cfd_headless.exe` — the CLI (Phase 5 target, not yet implemented)
- `build\tests\cfd_tests.exe` — the Catch2 test suite

Both are fully self-contained: statically-linked MinGW runtime
(`-static -static-libgcc -static-libstdc++`), with `embree4.dll` /
`tbb12.dll` / `tbbmalloc.dll` / `libqhull_r.dll` copied next to each
executable automatically as a post-build step. Confirmed to run with
nothing on `PATH` but bare Windows system directories — no need to have
Qt/MinGW/vcpkg on `PATH` to *run* the built executables, only to *build*
them (which `build.ps1` handles for you).

## Why a script instead of "just run cmake"

This project depends on a specific Qt-matched MinGW compiler that is
deliberately **not** on your system PATH (to avoid silently building
against whatever other MinGW/GCC might already be there — this machine
also has an unrelated, newer MinGW from a different install). `build.ps1`
pins the exact toolchain instead of hoping the ambient environment happens
to be right.
