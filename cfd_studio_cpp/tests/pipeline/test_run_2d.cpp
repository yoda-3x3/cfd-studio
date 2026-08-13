#include <catch_amalgamated.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "pipeline/run_2d.hpp"
#include "solvers/scenario_presets_2d.hpp"

using namespace cfd::pipeline;
namespace fs = std::filesystem;

namespace {
std::string temp_dir(const std::string& name) {
    auto dir = fs::temp_directory_path() / ("cfd_pipeline_2d_test_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir.string();
}

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Extracts the ASCII float payload of a named <DataArray Name="..."> block
// and checks every value parsed is finite.
bool data_array_all_finite(const std::string& vti_content, const std::string& array_name) {
    std::string marker = "Name=\"" + array_name + "\"";
    auto pos = vti_content.find(marker);
    if (pos == std::string::npos) return false;
    auto block_start = vti_content.find('\n', pos);
    auto block_end = vti_content.find("</DataArray>", block_start);
    if (block_start == std::string::npos || block_end == std::string::npos) return false;
    std::istringstream iss(vti_content.substr(block_start, block_end - block_start));
    double v;
    bool saw_any = false;
    while (iss >> v) {
        saw_any = true;
        if (!std::isfinite(v)) return false;
    }
    return saw_any;
}
} // namespace

TEST_CASE("run_2d: all three presets run at reduced resolution and produce finite fields", "[pipeline][run_2d]") {
    for (const auto& scenario : cfd::solvers::scenario_keys_2d()) {
        Run2DOptions opts;
        opts.scenario = scenario;
        opts.nx = 21;
        opts.ny = scenario == "cavity" ? 21 : 15;
        opts.n_steps = 40;
        opts.output_every = 10;
        opts.output_dir = temp_dir(scenario);

        auto result = run_2d(opts);
        REQUIRE(result.steps_run == 40);
        REQUIRE(fs::exists(result.pvd_path));

        std::string pvd_content = read_file(result.pvd_path);
        REQUIRE(pvd_content.find(scenario + "_00000.vti") != std::string::npos);
        REQUIRE(pvd_content.find(scenario + "_00040.vti") != std::string::npos);

        std::string vti_path = (fs::path(opts.output_dir) / (scenario + "_00040.vti")).string();
        REQUIRE(fs::exists(vti_path));
        std::string vti_content = read_file(vti_path);
        REQUIRE(data_array_all_finite(vti_content, "velocity_magnitude"));
        REQUIRE(data_array_all_finite(vti_content, "vorticity"));
    }
}

TEST_CASE("run_2d: unknown scenario key throws", "[pipeline][run_2d]") {
    Run2DOptions opts;
    opts.scenario = "not_a_scenario";
    opts.output_dir = temp_dir("bad_scenario");
    REQUIRE_THROWS(run_2d(opts));
}

TEST_CASE("run_2d: non-positive output_every throws", "[pipeline][run_2d]") {
    Run2DOptions opts;
    opts.scenario = "cavity";
    opts.output_every = 0;
    opts.output_dir = temp_dir("bad_output_every");
    REQUIRE_THROWS_AS(run_2d(opts), std::invalid_argument);
}
