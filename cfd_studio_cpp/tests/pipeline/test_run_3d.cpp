#include <catch_amalgamated.hpp>

#include <filesystem>
#include <stdexcept>

#include "../io/foam_case_verify.hpp"
#include "mesh/primitives.hpp"
#include "pipeline/run_3d.hpp"

using namespace cfd::pipeline;
namespace fs = std::filesystem;

namespace {
std::string temp_dir(const std::string& name) {
    auto dir = fs::temp_directory_path() / ("cfd_pipeline_3d_test_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir.string();
}
} // namespace

TEST_CASE("run_3d: external mode against a solid box produces a valid OpenFOAM case", "[pipeline][run_3d]") {
    cfd::mesh::Mesh box = cfd::mesh::make_box({1.0, 1.0, 1.0});

    Run3DOptions opts;
    opts.domain_mode = "external";
    opts.nx = 24;
    opts.ny = 16;
    opts.nz = 16;
    opts.n_steps = 15;
    opts.output_every = 15;
    opts.output_dir = temp_dir("external_box");
    opts.mesh_name = "box";

    auto result = run_3d(box, opts);
    REQUIRE(result.steps_run == 15);
    REQUIRE_FALSE(result.was_cached);
    REQUIRE(fs::exists(result.foam_path));
    REQUIRE(fs::exists(fs::path(opts.output_dir) / "constant" / "polyMesh" / "points"));
    REQUIRE(fs::exists(fs::path(opts.output_dir) / "constant" / "polyMesh" / "boundary"));
    REQUIRE(fs::exists(fs::path(opts.output_dir) / "0" / "U"));
    REQUIRE(fs::exists(fs::path(opts.output_dir) / "0" / "p"));

    cfd_test::verify_no_degenerate_faces(opts.output_dir);
    cfd_test::verify_owner_lt_neighbour(opts.output_dir);
}

TEST_CASE("run_3d: internal mode against a synthetic pipe produces a valid OpenFOAM case", "[pipeline][run_3d]") {
    cfd::mesh::Mesh tube = cfd::mesh::make_tube(4.0, 0.5, 0.15, 16, /*capped=*/true);

    Run3DOptions opts;
    opts.domain_mode = "internal";
    opts.nx = 30;
    opts.ny = 14;
    opts.nz = 14;
    opts.n_steps = 15;
    opts.output_every = 15;
    opts.output_dir = temp_dir("internal_tube");
    opts.mesh_name = "tube";

    auto result = run_3d(tube, opts);
    REQUIRE(result.steps_run == 15);
    REQUIRE(fs::exists(result.foam_path));
    REQUIRE(fs::exists(fs::path(opts.output_dir) / "0" / "U"));
    REQUIRE(fs::exists(fs::path(opts.output_dir) / "0" / "p"));

    cfd_test::verify_no_degenerate_faces(opts.output_dir);
    cfd_test::verify_owner_lt_neighbour(opts.output_dir);
}

TEST_CASE("run_3d: rejects an unrecognized domain_mode", "[pipeline][run_3d]") {
    cfd::mesh::Mesh box = cfd::mesh::make_box({1.0, 1.0, 1.0});
    Run3DOptions opts;
    opts.domain_mode = "sideways";
    opts.output_dir = temp_dir("bad_domain_mode");
    REQUIRE_THROWS_AS(run_3d(box, opts), std::invalid_argument);
}

TEST_CASE("run_3d: rejects a non-positive output_every", "[pipeline][run_3d]") {
    cfd::mesh::Mesh box = cfd::mesh::make_box({1.0, 1.0, 1.0});
    Run3DOptions opts;
    opts.output_every = 0;
    opts.output_dir = temp_dir("bad_output_every");
    REQUIRE_THROWS_AS(run_3d(box, opts), std::invalid_argument);
}
