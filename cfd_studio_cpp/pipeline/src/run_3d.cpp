#include "pipeline/run_3d.hpp"

#include <chrono>
#include <cstddef>
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
constexpr double kPreviewThrottleSeconds = 0.15;

std::uint64_t count_solid(const std::vector<std::uint8_t>& mask) {
    std::uint64_t n = 0;
    for (auto v : mask) n += v;
    return n;
}

} // namespace

namespace detail {
// XY slice at k=nz/2, XZ slice at j=ny/2, from Fields3D's unpadded
// row-major (nx,ny,nz) arrays (i slowest, k fastest -- see
// core/grid_index.hpp's idx3 convention, which Fields3D already follows).
Preview3DSlice extract_preview_slice(const cfd::solvers::Fields3D& fields, int nx, int ny, int nz, double dx,
                                      double dy, double dz) {
    Preview3DSlice slice;
    slice.nx = nx;
    slice.ny = ny;
    slice.nz = nz;
    slice.dx = dx;
    slice.dy = dy;
    slice.dz = dz;

    auto idx = [ny, nz](int i, int j, int k) {
        return static_cast<std::size_t>(i) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz) +
               static_cast<std::size_t>(j) * static_cast<std::size_t>(nz) + static_cast<std::size_t>(k);
    };

    int kmid = nz / 2;
    auto nxy = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
    slice.velocity_magnitude_xy.resize(nxy);
    slice.velocity_u_xy.resize(nxy);
    slice.velocity_v_xy.resize(nxy);
    slice.pressure_xy.resize(nxy);
    slice.obstacle_xy.resize(nxy);
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            std::size_t src = idx(i, j, kmid);
            std::size_t dst = static_cast<std::size_t>(i) * static_cast<std::size_t>(ny) + static_cast<std::size_t>(j);
            slice.velocity_magnitude_xy[dst] = fields.velocity_magnitude[src];
            slice.velocity_u_xy[dst] = fields.velocity_u[src];
            slice.velocity_v_xy[dst] = fields.velocity_v[src];
            slice.pressure_xy[dst] = fields.pressure[src];
            slice.obstacle_xy[dst] = fields.obstacle[src];
        }
    }

    int jmid = ny / 2;
    auto nxz = static_cast<std::size_t>(nx) * static_cast<std::size_t>(nz);
    slice.velocity_magnitude_xz.resize(nxz);
    slice.velocity_u_xz.resize(nxz);
    slice.velocity_w_xz.resize(nxz);
    slice.obstacle_xz.resize(nxz);
    for (int i = 0; i < nx; ++i) {
        for (int k = 0; k < nz; ++k) {
            std::size_t src = idx(i, jmid, k);
            std::size_t dst = static_cast<std::size_t>(i) * static_cast<std::size_t>(nz) + static_cast<std::size_t>(k);
            slice.velocity_magnitude_xz[dst] = fields.velocity_magnitude[src];
            slice.velocity_u_xz[dst] = fields.velocity_u[src];
            slice.velocity_w_xz[dst] = fields.velocity_w[src];
            slice.obstacle_xz[dst] = fields.obstacle[src];
        }
    }

    return slice;
}
} // namespace detail

Run3DResult run_3d(const cfd::mesh::Mesh& mesh, const Run3DOptions& opts,
                    const std::function<void(int, double)>& on_progress, const Preview3DCallback& on_preview) {
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

    Run3DResult result;
    result.foam_path = writer.foam_path();

    auto last_update = std::chrono::steady_clock::now();
    for (int step = 1; step <= opts.n_steps; ++step) {
        if (opts.stop_flag && opts.stop_flag->load()) {
            result.stopped = true;
            break;
        }

        result.final_residual = solver.step();
        result.steps_run = step;

        auto now = std::chrono::steady_clock::now();
        bool throttled_tick =
            std::chrono::duration<double>(now - last_update).count() >= kPreviewThrottleSeconds || step == opts.n_steps;
        if (throttled_tick) {
            last_update = now;
            if (on_progress) on_progress(step, result.final_residual);
            if (on_preview) {
                auto fields = solver.fields();
                on_preview(
                    detail::extract_preview_slice(fields, opts.nx, opts.ny, opts.nz, solver.dx(), solver.dy(), solver.dz()));
            }
        }

        if (step % opts.output_every == 0 || step == opts.n_steps) {
            write_step();
        }
    }

    if (cache && !result.stopped) {
        cache->save_run(run_key_str, writer.foam_path(), opts.mesh_name);
    }

    return result;
}

} // namespace cfd::pipeline
