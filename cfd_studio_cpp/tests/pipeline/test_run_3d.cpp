#include <catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>
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

TEST_CASE("run_3d: ground_effect=true exports lowerWall as noSlip in the written case", "[pipeline][run_3d]") {
    cfd::mesh::Mesh box = cfd::mesh::make_box({1.0, 1.0, 1.0});

    Run3DOptions opts;
    opts.domain_mode = "external";
    opts.nx = 24;
    opts.ny = 16;
    opts.nz = 16;
    opts.n_steps = 15;
    opts.output_every = 15;
    opts.output_dir = temp_dir("external_ground_effect");
    opts.mesh_name = "box";
    opts.ground_effect = true;
    opts.altitude_gap = 0.5;

    auto result = run_3d(box, opts);
    REQUIRE(result.steps_run == 15);

    std::ifstream uf(fs::path(opts.output_dir) / "0" / "U");
    std::string u_text((std::istreambuf_iterator<char>(uf)), std::istreambuf_iterator<char>());
    auto lower_pos = u_text.find("lowerWall");
    auto upper_pos = u_text.find("upperWall");
    REQUIRE(lower_pos != std::string::npos);
    REQUIRE(upper_pos != std::string::npos);
    REQUIRE(u_text.substr(lower_pos, upper_pos - lower_pos).find("noSlip") != std::string::npos);
    REQUIRE(u_text.substr(upper_pos, 200).find("noSlip") == std::string::npos);

    cfd_test::verify_no_degenerate_faces(opts.output_dir);
    cfd_test::verify_owner_lt_neighbour(opts.output_dir);
}

TEST_CASE("run_3d: ground_effect is neutralized in internal mode", "[pipeline][run_3d]") {
    cfd::mesh::Mesh tube = cfd::mesh::make_tube(4.0, 0.5, 0.15, 16, /*capped=*/true);

    auto run_internal = [&](bool ground_effect, const std::string& dir_suffix) {
        Run3DOptions opts;
        opts.domain_mode = "internal";
        opts.nx = 30;
        opts.ny = 14;
        opts.nz = 14;
        opts.n_steps = 15;
        opts.output_every = 15;
        opts.output_dir = temp_dir("internal_ground_effect_" + dir_suffix);
        opts.mesh_name = "tube";
        opts.ground_effect = ground_effect;
        [[maybe_unused]] auto result = run_3d(tube, opts);
        std::ifstream uf(fs::path(opts.output_dir) / "0" / "U");
        return std::string((std::istreambuf_iterator<char>(uf)), std::istreambuf_iterator<char>());
    };

    // A stray ground_effect=true must be fully neutralized for internal
    // mode (its own lateral walls, including y=0, are already no-slip for
    // an unrelated reason -- it's a pipe) -- output must be byte-identical.
    REQUIRE(run_internal(true, "on") == run_internal(false, "off"));
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
