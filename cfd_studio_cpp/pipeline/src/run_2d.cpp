#include "pipeline/run_2d.hpp"

#include <stdexcept>

#include "io/vtk_writer.hpp"
#include "solvers/navier_stokes_2d.hpp"
#include "solvers/scenario_presets_2d.hpp"

namespace cfd::pipeline {

Run2DResult run_2d(const Run2DOptions& opts, const std::function<void(int, double)>& on_progress) {
    if (opts.output_every <= 0) throw std::invalid_argument("run_2d: output_every must be positive");

    const auto& preset = cfd::solvers::scenario_preset_2d(opts.scenario);

    cfd::solvers::SolverConfig2D config;
    config.nx = opts.nx.value_or(preset.default_nx);
    config.ny = opts.ny.value_or(preset.default_ny);
    config.Lx = preset.Lx;
    config.Ly = preset.Ly;
    config.Re = opts.Re.value_or(preset.default_Re);
    config.kind = preset.kind;
    config.U = opts.U.value_or(preset.default_U);
    config.dt = opts.dt;
    if (preset.has_obstacle) config.obstacle = cfd::solvers::default_obstacle_2d(preset);

    cfd::solvers::NavierStokes2D solver(config);

    std::string case_name = opts.case_name.empty() ? opts.scenario : opts.case_name;
    cfd::io::VtkSeriesWriter writer(opts.output_dir, case_name);
    writer.clear();

    auto write_step = [&](int step_index) {
        auto fields = solver.fields();
        writer.write_step(step_index, solver.time(), config.nx, config.ny, solver.dx(), solver.dy(),
                           fields.velocity_u, fields.velocity_v, fields.velocity_magnitude,
                           fields.vorticity, fields.streamfunction, fields.obstacle);
    };

    write_step(0);
    double last_residual = 0.0;
    for (int step = 1; step <= opts.n_steps; ++step) {
        last_residual = solver.step();
        if (on_progress) on_progress(step, last_residual);
        if (step % opts.output_every == 0 || step == opts.n_steps) {
            write_step(step);
        }
    }

    return Run2DResult{writer.pvd_path(), opts.n_steps, last_residual};
}

} // namespace cfd::pipeline
