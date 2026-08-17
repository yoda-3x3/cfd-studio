#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "mesh/mesh.hpp"
#include "solvers/navier_stokes_3d.hpp"

namespace cfd::pipeline {

struct Run3DOptions {
    std::string domain_mode = "external"; // "external" | "internal"
    int nx = 100, ny = 60, nz = 60;
    double Re = 200.0, U_in = 1.0;
    int n_steps = 1500, output_every = 25;
    std::string output_dir;
    std::string mesh_name; // for cache bookkeeping/logging
    std::optional<int> num_threads;
    double inflow_gap = 1.5, wake_gap = 4.0, lateral_gap = 1.5;
    // Opt-in, "external" domain_mode only (silently ignored for
    // "internal" -- see run_3d's ground_effect_active): places the object
    // altitude_gap*L above a no-slip ground plane at y=0 instead of
    // centering it, for ground-effect simulations.
    bool ground_effect = false;
    double altitude_gap = 1.5;
    bool force_rerun = false;
    std::optional<std::string> cache_root; // unset -> caching disabled

    // If set, checked once per step; setting it true from any thread
    // cancels the run after the current step completes. See
    // Run2DOptions::stop_flag for the same contract.
    std::atomic<bool>* stop_flag = nullptr;
};

struct Run3DResult {
    std::string foam_path;
    bool was_cached = false;
    int steps_run = 0;
    double final_residual = 0.0;
    bool stopped = false; // true if stop_flag was observed set before n_steps completed
};

// Live mid-plane slices of the running 3D solve, for GUI plotting -- XY
// slice at k=nz/2, XZ slice at j=ny/2, each row-major (nx, ny) / (nx, nz).
// Deliberately not the full 3D volume: port of ui/worker3d.py's preview
// dict, which slices for the same reason (marshaling a full 3D field every
// throttled tick is unnecessary data volume when only 2 cross-sections are
// ever displayed).
struct Preview3DSlice {
    std::vector<double> velocity_magnitude_xy, velocity_u_xy, velocity_v_xy, pressure_xy;
    std::vector<float> obstacle_xy;
    std::vector<double> velocity_magnitude_xz, velocity_u_xz, velocity_w_xz;
    std::vector<float> obstacle_xz;
    int nx = 0, ny = 0, nz = 0;
    double dx = 0.0, dy = 0.0, dz = 0.0;
};
using Preview3DCallback = std::function<void(const Preview3DSlice&)>;

// Runs the 3D solver end to end and writes an OpenFOAM case -- port of
// ui/worker3d.py's Simulation3DWorker.run(). `mesh` must already be
// oriented (the caller's responsibility, matching the Python worker's
// contract: orientation happens before the worker is constructed, not
// inside it). on_progress/on_preview both fire on a 0.15s wall-clock
// throttle (see Run2DOptions/run_2d for why). Throws std::invalid_argument
// for an unrecognized domain_mode or non-positive output_every,
// std::runtime_error if the voxelized geometry doesn't intersect the grid
// at all.
[[nodiscard]] Run3DResult run_3d(const cfd::mesh::Mesh& mesh, const Run3DOptions& opts,
                                  const std::function<void(int step, double residual)>& on_progress = nullptr,
                                  const Preview3DCallback& on_preview = nullptr);

namespace detail {
// Implementation detail of run_3d, exposed only so its slicing math gets a
// direct unit test independent of running an actual solve.
[[nodiscard]] Preview3DSlice extract_preview_slice(const cfd::solvers::Fields3D& fields, int nx, int ny, int nz,
                                                     double dx, double dy, double dz);
} // namespace detail

} // namespace cfd::pipeline
