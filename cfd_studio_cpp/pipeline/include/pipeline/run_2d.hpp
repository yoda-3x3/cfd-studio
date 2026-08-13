#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "solvers/navier_stokes_2d.hpp"

namespace cfd::pipeline {

struct Run2DOptions {
    std::string scenario; // "cavity" | "channel" | "obstacle"
    std::optional<int> nx, ny; // unset -> preset default
    std::optional<double> Re, U, dt;
    std::optional<double> obstacle_x0, obstacle_width, obstacle_height; // unset -> preset default (only used if the preset has_obstacle)
    int n_steps = 2000;
    int output_every = 20;
    std::string output_dir;
    std::string case_name; // default: scenario key

    // If set, checked once per step; setting it true from any thread (e.g.
    // a GUI's Stop button) cancels the run after the current step
    // completes. Mirrors ui/worker.py's request_stop()/_stop_requested,
    // but atomic since C++ has no GIL to make a plain bool flag safe
    // across threads.
    std::atomic<bool>* stop_flag = nullptr;
};

struct Run2DResult {
    std::string pvd_path;
    int steps_run = 0;
    double final_residual = 0.0;
    bool stopped = false; // true if stop_flag was observed set before n_steps completed
};

// Fires alongside on_progress, same wall-clock throttle -- a live field
// snapshot for GUI plotting. Port of ui/worker.py's separate `preview`
// signal.
using Preview2DCallback = std::function<void(const cfd::solvers::Fields2D& fields, int nx, int ny, double dx, double dy)>;

// Runs the 2D vorticity-streamfunction solver for a scenario preset end to
// end and writes a VTK series -- port of ui/worker.py's
// SimulationWorker.run(). on_progress/on_preview both fire on a 0.15s
// wall-clock throttle (not every step -- matches worker.py's
// min_update_interval, since a fixed per-step cadence would flood a GUI's
// event queue on a fast solver). Throws std::out_of_range for an unknown
// opts.scenario key, std::invalid_argument for a non-positive output_every.
[[nodiscard]] Run2DResult run_2d(const Run2DOptions& opts,
                                  const std::function<void(int step, double residual)>& on_progress = nullptr,
                                  const Preview2DCallback& on_preview = nullptr);

} // namespace cfd::pipeline
