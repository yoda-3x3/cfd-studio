#include "pipeline/run_3d.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>

#include "io/openfoam_writer.hpp"
#include "io/run_cache.hpp"
#include "solvers/kernel_backend.hpp"
#include "solvers/navier_stokes_3d.hpp"
#include "solvers/voxelizer.hpp"

namespace cfd::pipeline {

namespace {
std::uint64_t count_solid(const std::vector<std::uint8_t>& mask) {
    std::uint64_t n = 0;
    for (auto v : mask) n += v;
    return n;
}
} // namespace

Run3DResult run_3d(const cfd::mesh::Mesh& mesh, const Run3DOptions& opts,
                    const std::function<void(int, double)>& on_progress) {
    if (opts.output_every <= 0) throw std::invalid_argument("run_3d: output_every must be positive");
    bool internal = opts.domain_mode == "internal";
    if (!internal && opts.domain_mode != "external") {
        throw std::invalid_argument("run_3d: domain_mode must be \"external\" or \"internal\"");
    }

    std::optional<cfd::io::RunCache> cache;
    std::string geom_key, run_key_str;
    if (opts.cache_root) {
        cache.emplace(*opts.cache_root);
        std::string mesh_sig = cfd::io::mesh_signature(mesh);
        geom_key = cfd::io::geometry_key(mesh_sig, opts.nx, opts.ny, opts.nz, opts.inflow_gap, opts.wake_gap,
                                          opts.lateral_gap, opts.domain_mode);
        run_key_str = cfd::io::run_key(geom_key, opts.Re, opts.U_in, opts.n_steps, opts.output_every);
        if (!opts.force_rerun) {
            if (auto cached = cache->load_run(run_key_str)) {
                return Run3DResult{*cached, true, 0, 0.0};
            }
        }
    }

    cfd::solvers::PreparedGeometry geo = internal
        ? cfd::solvers::prepare_internal_geometry(mesh, 1.0, opts.inflow_gap, opts.wake_gap)
        : cfd::solvers::prepare_geometry(mesh, 1.0, opts.inflow_gap, opts.wake_gap, opts.lateral_gap);

    std::vector<std::uint8_t> solid;
    double Lx = geo.Lx, Ly = geo.Ly, Lz = geo.Lz;
    bool have_cached_geom = false;
    if (cache && !opts.force_rerun) {
        if (auto cached_geom = cache->load_geometry(geom_key)) {
            solid = std::move(cached_geom->solid);
            Lx = cached_geom->Lx;
            Ly = cached_geom->Ly;
            Lz = cached_geom->Lz;
            have_cached_geom = true;
        }
    }
    if (!have_cached_geom) {
        solid = internal ? cfd::solvers::voxelize_internal_to_grid(geo.mesh, opts.nx, opts.ny, opts.nz, Lx, Ly, Lz)
                          : cfd::solvers::voxelize_to_grid(geo.mesh, opts.nx, opts.ny, opts.nz, Lx, Ly, Lz);
        if (cache) {
            cfd::io::GeometryCacheEntry entry{solid, opts.nx, opts.ny, opts.nz, Lx, Ly, Lz};
            cache->save_geometry(geom_key, entry);
        }
    }

    if (count_solid(solid) == 0) {
        throw std::runtime_error("run_3d: voxelized geometry didn't intersect the grid (solid mask is empty)");
    }

    cfd::solvers::SolverConfig3D config;
    config.nx = opts.nx;
    config.ny = opts.ny;
    config.nz = opts.nz;
    config.Lx = Lx;
    config.Ly = Ly;
    config.Lz = Lz;
    config.Re = opts.Re;
    config.U_in = opts.U_in;
    config.num_threads = opts.num_threads;
    config.domain_mode = internal ? cfd::solvers::DomainMode3D::Internal : cfd::solvers::DomainMode3D::External;

    const cfd::solvers::KernelBackend& backend = (opts.num_threads && *opts.num_threads > 1)
                                                      ? cfd::solvers::threaded_backend()
                                                      : cfd::solvers::scalar_backend();

    cfd::solvers::NavierStokes3D solver(config, backend, &solid);

    cfd::io::OpenFoamCaseWriter writer(opts.output_dir, opts.nx, opts.ny, opts.nz, solver.dx(), solver.dy(),
                                        solver.dz(), solid, &geo.mesh, opts.domain_mode);

    auto write_step = [&]() {
        auto fields = solver.fields();
        writer.write_timestep(solver.time(), fields.velocity_u.data(), fields.velocity_v.data(),
                               fields.velocity_w.data(), fields.pressure.data());
    };

    write_step();
    double last_residual = 0.0;
    for (int step = 1; step <= opts.n_steps; ++step) {
        last_residual = solver.step();
        if (on_progress) on_progress(step, last_residual);
        if (step % opts.output_every == 0 || step == opts.n_steps) {
            write_step();
        }
    }

    if (cache) {
        cache->save_run(run_key_str, writer.foam_path(), opts.mesh_name);
    }

    return Run3DResult{writer.foam_path(), false, opts.n_steps, last_residual};
}

} // namespace cfd::pipeline
