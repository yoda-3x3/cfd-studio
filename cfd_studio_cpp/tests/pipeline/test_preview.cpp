#include <catch_amalgamated.hpp>

#include <atomic>
#include <filesystem>

#include "mesh/primitives.hpp"
#include "pipeline/run_2d.hpp"
#include "pipeline/run_3d.hpp"
#include "solvers/navier_stokes_3d.hpp"

using namespace cfd::pipeline;
namespace fs = std::filesystem;

namespace {
std::string temp_dir(const std::string& name) {
    auto dir = fs::temp_directory_path() / ("cfd_preview_test_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir.string();
}
} // namespace

TEST_CASE("extract_preview_slice: XY slice at k=nz/2 and XZ slice at j=ny/2 match the source field", "[pipeline][preview]") {
    int nx = 4, ny = 3, nz = 5;
    auto n = static_cast<std::size_t>(nx) * ny * nz;
    cfd::solvers::Fields3D fields;
    fields.velocity_magnitude.resize(n);
    fields.velocity_u.resize(n);
    fields.velocity_v.resize(n);
    fields.velocity_w.resize(n);
    fields.pressure.resize(n);
    fields.obstacle.resize(n);

    auto idx = [ny, nz](int i, int j, int k) {
        return static_cast<std::size_t>(i) * ny * nz + static_cast<std::size_t>(j) * nz + static_cast<std::size_t>(k);
    };
    // Every field gets a distinct, index-derived value so a slice's
    // correctness can be checked by recomputing the expected value from
    // (i,j,k) rather than just trusting whatever the extractor produced.
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                double v = i * 100.0 + j * 10.0 + k;
                std::size_t s = idx(i, j, k);
                fields.velocity_magnitude[s] = v;
                fields.velocity_u[s] = v + 0.1;
                fields.velocity_v[s] = v + 0.2;
                fields.velocity_w[s] = v + 0.3;
                fields.pressure[s] = v + 0.4;
                fields.obstacle[s] = static_cast<float>((i + j + k) % 2);
            }
        }
    }

    auto slice = detail::extract_preview_slice(fields, nx, ny, nz, 0.1, 0.2, 0.3);
    REQUIRE(slice.nx == nx);
    REQUIRE(slice.ny == ny);
    REQUIRE(slice.nz == nz);
    REQUIRE(slice.dx == Catch::Approx(0.1));
    REQUIRE(slice.dy == Catch::Approx(0.2));
    REQUIRE(slice.dz == Catch::Approx(0.3));

    int kmid = nz / 2;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double expected = i * 100.0 + j * 10.0 + kmid;
            std::size_t dst = static_cast<std::size_t>(i) * ny + j;
            REQUIRE(slice.velocity_magnitude_xy[dst] == Catch::Approx(expected));
            REQUIRE(slice.velocity_u_xy[dst] == Catch::Approx(expected + 0.1));
            REQUIRE(slice.velocity_v_xy[dst] == Catch::Approx(expected + 0.2));
            REQUIRE(slice.pressure_xy[dst] == Catch::Approx(expected + 0.4));
        }
    }

    int jmid = ny / 2;
    for (int i = 0; i < nx; ++i) {
        for (int k = 0; k < nz; ++k) {
            double expected = i * 100.0 + jmid * 10.0 + k;
            std::size_t dst = static_cast<std::size_t>(i) * nz + k;
            REQUIRE(slice.velocity_magnitude_xz[dst] == Catch::Approx(expected));
            REQUIRE(slice.velocity_u_xz[dst] == Catch::Approx(expected + 0.1));
            REQUIRE(slice.velocity_w_xz[dst] == Catch::Approx(expected + 0.3));
        }
    }
}

TEST_CASE("run_2d: progress/preview callbacks are wall-clock throttled, not per-step", "[pipeline][preview]") {
    Run2DOptions opts;
    opts.scenario = "cavity";
    opts.nx = 21;
    opts.ny = 21;
    opts.n_steps = 300; // enough steps that per-step firing would be obviously more than a few callbacks
    opts.output_every = 300;
    opts.output_dir = temp_dir("2d_throttle");

    int progress_calls = 0, preview_calls = 0;
    int last_progress_step = 0;
    auto result = run_2d(
        opts, [&](int step, double) { ++progress_calls; last_progress_step = step; },
        [&](const cfd::solvers::Fields2D&, int, int, double, double) { ++preview_calls; });

    REQUIRE(result.steps_run == 300);
    REQUIRE(progress_calls >= 1);
    REQUIRE(progress_calls < opts.n_steps); // proves throttling actually happened, not a per-step fire
    REQUIRE(preview_calls == progress_calls); // both fire together on the same throttled tick
    REQUIRE(last_progress_step == 300); // the final step always fires, regardless of throttle timing
}

TEST_CASE("run_2d: stop_flag halts the run and is reflected in the result", "[pipeline][preview]") {
    Run2DOptions opts;
    opts.scenario = "cavity";
    opts.nx = 21;
    opts.ny = 21;
    opts.n_steps = 1000000; // would run effectively forever without the stop flag
    opts.output_every = 1000000;
    opts.output_dir = temp_dir("2d_stop");

    std::atomic<bool> stop_flag{false};
    opts.stop_flag = &stop_flag;

    int calls = 0;
    auto result = run_2d(opts, [&](int step, double) {
        ++calls;
        if (step >= 5) stop_flag.store(true);
    });

    REQUIRE(result.stopped);
    REQUIRE(result.steps_run < opts.n_steps);
}

TEST_CASE("run_3d: preview slices arrive during a run and match the grid dims", "[pipeline][preview]") {
    cfd::mesh::Mesh box = cfd::mesh::make_box({1.0, 1.0, 1.0});

    Run3DOptions opts;
    opts.domain_mode = "external";
    opts.nx = 24;
    opts.ny = 16;
    opts.nz = 16;
    opts.n_steps = 10;
    opts.output_every = 10;
    opts.output_dir = temp_dir("3d_preview");

    int preview_calls = 0;
    auto result = run_3d(box, opts, nullptr, [&](const Preview3DSlice& slice) {
        ++preview_calls;
        REQUIRE(slice.nx == opts.nx);
        REQUIRE(slice.ny == opts.ny);
        REQUIRE(slice.nz == opts.nz);
        REQUIRE(slice.velocity_magnitude_xy.size() == static_cast<std::size_t>(opts.nx) * opts.ny);
        REQUIRE(slice.velocity_magnitude_xz.size() == static_cast<std::size_t>(opts.nx) * opts.nz);
    });

    REQUIRE(result.steps_run == 10);
    REQUIRE(preview_calls >= 1);
}
