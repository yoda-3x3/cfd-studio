#include <catch_amalgamated.hpp>

#include "solvers/performance_presets_3d.hpp"
#include "solvers/scenario_presets_2d.hpp"

using namespace cfd::solvers;

// Pins the exact constants ported from the Python originals
// (solver/scenarios.py's PRESETS, solver3d/presets.py's PRESETS) so a
// future accidental edit here is caught immediately, same spirit as
// test_navier_stokes_2d_regression.cpp.

TEST_CASE("scenario_preset_2d: cavity matches solver/scenarios.py", "[solvers][presets]") {
    const auto& p = scenario_preset_2d("cavity");
    REQUIRE(p.kind == ScenarioKind2D::Cavity);
    REQUIRE(p.Lx == Catch::Approx(1.0));
    REQUIRE(p.Ly == Catch::Approx(1.0));
    REQUIRE(p.default_nx == 81);
    REQUIRE(p.default_ny == 81);
    REQUIRE(p.default_Re == Catch::Approx(400.0));
    REQUIRE(p.default_U == Catch::Approx(1.0));
    REQUIRE_FALSE(p.has_obstacle);
}

TEST_CASE("scenario_preset_2d: channel matches solver/scenarios.py", "[solvers][presets]") {
    const auto& p = scenario_preset_2d("channel");
    REQUIRE(p.kind == ScenarioKind2D::Channel);
    REQUIRE(p.Lx == Catch::Approx(3.0));
    REQUIRE(p.Ly == Catch::Approx(1.0));
    REQUIRE(p.default_nx == 181);
    REQUIRE(p.default_ny == 61);
    REQUIRE(p.default_Re == Catch::Approx(200.0));
    REQUIRE(p.default_U == Catch::Approx(1.0));
    REQUIRE_FALSE(p.has_obstacle);
}

TEST_CASE("scenario_preset_2d: obstacle matches solver/scenarios.py, including default geometry", "[solvers][presets]") {
    const auto& p = scenario_preset_2d("obstacle");
    REQUIRE(p.kind == ScenarioKind2D::ObstacleScenario);
    REQUIRE(p.Lx == Catch::Approx(4.0));
    REQUIRE(p.Ly == Catch::Approx(1.0));
    REQUIRE(p.default_nx == 241);
    REQUIRE(p.default_ny == 61);
    REQUIRE(p.default_Re == Catch::Approx(200.0));
    REQUIRE(p.default_U == Catch::Approx(1.0));
    REQUIRE(p.has_obstacle);

    Obstacle2D obs = default_obstacle_2d(p);
    REQUIRE(obs.x0 == Catch::Approx(4.0 * 0.35));
    REQUIRE(obs.width == Catch::Approx(4.0 * 0.15));
    REQUIRE(obs.height == Catch::Approx(1.0 * 0.3));
}

TEST_CASE("scenario_preset_2d: unknown key throws, keys list has all 3", "[solvers][presets]") {
    REQUIRE_THROWS(scenario_preset_2d("not_a_scenario"));
    auto keys = scenario_keys_2d();
    REQUIRE(keys.size() == 3);
}

TEST_CASE("performance_preset_3d: matches solver3d/presets.py", "[solvers][presets]") {
    const auto& fast = performance_preset_3d("fast_preview");
    REQUIRE(fast.nx == 50);
    REQUIRE(fast.ny == 30);
    REQUIRE(fast.nz == 30);
    REQUIRE(fast.steps == 500);
    REQUIRE(fast.output_every == 25);

    const auto& balanced = performance_preset_3d("balanced");
    REQUIRE(balanced.nx == 100);
    REQUIRE(balanced.ny == 60);
    REQUIRE(balanced.nz == 60);
    REQUIRE(balanced.steps == 1500);
    REQUIRE(balanced.output_every == 25);

    const auto& hq = performance_preset_3d("high_quality");
    REQUIRE(hq.nx == 160);
    REQUIRE(hq.ny == 100);
    REQUIRE(hq.nz == 100);
    REQUIRE(hq.steps == 3000);
    REQUIRE(hq.output_every == 50);

    REQUIRE(std::string(kDefaultPerformancePreset3D) == "balanced");
    REQUIRE_THROWS(performance_preset_3d("not_a_preset"));
    REQUIRE(performance_preset_keys_3d().size() == 3);
}
