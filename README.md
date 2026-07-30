# CFD Studio

A desktop CFD app with a live preview and direct ParaView export, covering both quick
2D scenarios and custom 3D geometry from an uploaded mesh file.

## What it does

### 2D Flow Scenarios tab
- Solves the 2D incompressible Navier-Stokes equations using a vorticity-streamfunction
  formulation (explicit time-stepping, sparse-LU streamfunction solve reused every step).
- Three built-in scenarios: **Lid-Driven Cavity**, **Channel Flow**, and **Flow Past a
  Wall-Mounted Obstacle**.
- Live in-app preview: velocity magnitude + streamlines, and a convergence (residual) plot.
- Fields exported: `velocity`, `velocity_magnitude`, `vorticity`, `streamfunction`, `obstacle`.

### 3D Custom Geometry tab
- Upload your own **STL / OBJ / PLY / OFF** file; the app centers it, normalizes its size
  (largest dimension = 1.0, used as the characteristic length for the Reynolds number),
  and voxelizes it onto a Cartesian grid via point-in-mesh containment testing.
- Solves the 3D incompressible Navier-Stokes equations with an explicit projection
  (Chorin) method on a collocated grid: uniform inflow on one face, convective outflow
  on the opposite face, free-slip walls on the other four (a virtual wind tunnel), and
  the uploaded geometry enforced as an immersed solid boundary.
- **Multi-core**: the per-step advection/diffusion and pressure-Poisson kernels are
  JIT-compiled with numba and parallelized across CPU cores (`prange`); the CPU thread
  count is adjustable in the UI. In testing, expect a real but modest speedup (often
  best around 4-8 threads) rather than linear scaling to every core — this class of
  stencil computation tends to be memory-bandwidth-bound, not purely compute-bound.
- Live preview shows a 2D slice through the mid-plane of the domain (full 3D
  exploration is what ParaView is for).
- **Exports as a real OpenFOAM case** (not VTK): the solver's Cartesian grid + solid
  mask is converted into an actual OpenFOAM polyMesh (`constant/polyMesh/`:
  points/faces/owner/neighbour/boundary) with per-timestep `U`/`p` field files, plus
  a `.foam` placeholder ParaView opens directly. The uploaded geometry's surface
  becomes a genuine boundary patch named `object` (not just a masked-out region) —
  open it via **MeshRegions → patch/object** in ParaView if you want to isolate the
  body's surface, e.g. to color it by surface pressure.
- **Cut-cell surface refinement**: rather than exporting the object's surface as
  blocky, stair-stepped voxel faces, every boundary cell is clipped against your
  *actual* uploaded triangle mesh via a real boolean difference (box minus your
  geometry, using the `manifold3d` engine) — the resulting real-geometry triangles
  replace the flat voxel face for that cell. In testing on a sphere this cut the
  surface's deviation from the true shape roughly 14x (radius std 0.065 → 0.0045);
  on the wing.stl test case, 90%+ of surface cells got the real-geometry treatment,
  with the remainder (mostly at the thin trailing edge) falling back to a flat voxel
  face when the clip is degenerate. This is a **geometry-only** upgrade — the solver
  and its physics are completely unchanged; only the exported surface's shape improves.
- **Orientation check**: right after upload, a dialog analyzes the mesh's principal
  axes and ranks them by projected frontal (silhouette) area — the axis presenting
  the smallest cross-section to the flow is suggested as the flow direction (this is
  an explainable geometric heuristic, not a trained model). A 3D preview shows the
  mesh with a flow-direction arrow so you can accept the suggestion, pick one of the
  other two candidate axes, or flip the flow direction, before anything is simulated.
  Re-openable anytime via **Re-check Orientation...**.
- **Caching**: results are cached on disk keyed on the mesh's actual geometry (after
  orientation) plus every setting that affects the outcome. Re-running the same file
  with identical settings reuses the finished result instantly instead of recomputing
  (a 25x+ speedup in testing); changing only Re/steps/threads while keeping the same
  mesh, grid, and domain padding still reuses the voxelization step. Check **Force
  re-run** to bypass the cache.

## Running it

```bash
"C:\Users\reach\Dance\.venv\Scripts\python.exe" "C:\Users\reach\Dance\cfd_paraview_app\main.py"
```

Or double-click the **CFD Studio** shortcut on the Desktop.

## First-time ParaView setup

The app auto-detects ParaView under `C:\Program Files\ParaView*`. If it's installed
elsewhere, use **Settings > Locate ParaView...** to browse to `paraview.exe` once —
the path is remembered in `%APPDATA%\CFDParaviewApp\config.json`.

## Notes

- The 2D solver reports velocity, vorticity, and streamfunction — there is no explicit
  pressure field in that formulation (a common simplification for 2D vorticity-based
  solvers). The 3D solver does report pressure.
- The 3D immersed-boundary treatment is a simple binary voxel mask (cells are either
  fully solid or fully fluid) — the OpenFOAM mesh is exact given that voxelization
  (no further approximation once cells are classified), but the voxelization itself
  is a "staircased" approximation of the uploaded surface, not body-fitted; finer
  grids reduce this. The free-slip domain walls use a boundary treatment that carries
  a small, known, localized divergence artifact right at the 12 domain edges/corners
  — excluded from the reported convergence residual since it doesn't reflect the bulk
  flow. Non-watertight uploaded meshes may also give less reliable inside/outside
  classification near seams.
- ParaView's OpenFOAM reader hides the `0` (initial condition) timestep from the
  animation list by default — that's normal reader behavior (toggle "Skip Zero Time"
  in its properties if you want to see it), not a sign anything is missing.
- Grid resolution, Reynolds number, and step count are all adjustable; time steps are
  chosen automatically for numerical stability. Finer 3D grids and more pressure
  iterations improve accuracy at the cost of runtime and memory.
