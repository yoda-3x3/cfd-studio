#pragma once

#include <functional>
#include <optional>
#include <string>

namespace cfd::pipeline {

struct Run2DOptions {
    std::string scenario; // "cavity" | "channel" | "obstacle"
    std::optional<int> nx, ny; // unset -> preset default
    std::optional<double> Re, U, dt;
    int n_steps = 2000;
    int output_every = 20;
    std::string output_dir;
    std::string case_name; // default: scenario key
};

struct Run2DResult {
    std::string pvd_path;
    int steps_run = 0;
    double final_residual = 0.0;
};

// Runs the 2D vorticity-streamfunction solver for a scenario preset end to
// end and writes a VTK series -- port of ui/worker.py's
// SimulationWorker.run(). Throws std::out_of_range for an unknown
// opts.scenario key, std::invalid_argument for a non-positive output_every.
[[nodiscard]] Run2DResult run_2d(const Run2DOptions& opts,
                                  const std::function<void(int step, double residual)>& on_progress = nullptr);

} // namespace cfd::pipeline
