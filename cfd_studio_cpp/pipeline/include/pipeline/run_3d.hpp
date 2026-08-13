#pragma once

#include <functional>
#include <optional>
#include <string>

#include "mesh/mesh.hpp"

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
    bool force_rerun = false;
    std::optional<std::string> cache_root; // unset -> caching disabled
};

struct Run3DResult {
    std::string foam_path;
    bool was_cached = false;
    int steps_run = 0;
    double final_residual = 0.0;
};

// Runs the 3D solver end to end and writes an OpenFOAM case -- port of
// ui/worker3d.py's Simulation3DWorker.run(). `mesh` must already be
// oriented (the caller's responsibility, matching the Python worker's
// contract: orientation happens before the worker is constructed, not
// inside it). Throws std::invalid_argument for an unrecognized domain_mode
// or non-positive output_every, std::runtime_error if the voxelized
// geometry doesn't intersect the grid at all.
[[nodiscard]] Run3DResult run_3d(const cfd::mesh::Mesh& mesh, const Run3DOptions& opts,
                                  const std::function<void(int step, double residual)>& on_progress = nullptr);

} // namespace cfd::pipeline
