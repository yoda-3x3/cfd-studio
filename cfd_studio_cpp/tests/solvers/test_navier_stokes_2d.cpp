#include <catch_amalgamated.hpp>

#include <cmath>

#include "solvers/navier_stokes_2d.hpp"

using namespace cfd::solvers;

namespace {
bool all_finite(const std::vector<double>& v) {
    for (double x : v) {
        if (!std::isfinite(x)) return false;
    }
    return true;
}
} // namespace

TEST_CASE("NavierStokes2D: lid-driven cavity stays finite and respects lid BC", "[solvers][navier_stokes_2d]") {
    SolverConfig2D cfg;
    cfg.nx = 21; cfg.ny = 21;
    cfg.Re = 100.0;
    cfg.U = 1.0;
    cfg.kind = ScenarioKind2D::Cavity;

    NavierStokes2D solver(cfg);
    for (int s = 0; s < 30; ++s) {
        double residual = solver.step();
        REQUIRE(std::isfinite(residual));
        REQUIRE(residual >= 0.0);
    }

    Fields2D f = solver.fields();
    REQUIRE(all_finite(f.velocity_u));
    REQUIRE(all_finite(f.velocity_v));
    REQUIRE(all_finite(f.streamfunction));
    REQUIRE(all_finite(f.vorticity));

    // Top row (the lid) should move at U; side/bottom walls at 0 (no-slip).
    for (int i = 0; i < cfg.nx; ++i) {
        REQUIRE(f.velocity_u[static_cast<std::size_t>(cfg.ny - 1) * cfg.nx + static_cast<std::size_t>(i)]
                == Catch::Approx(cfg.U));
        REQUIRE(f.velocity_u[static_cast<std::size_t>(i)] == Catch::Approx(0.0).margin(1e-12)); // bottom row
    }
}

TEST_CASE("NavierStokes2D: channel flow stays finite", "[solvers][navier_stokes_2d]") {
    SolverConfig2D cfg;
    cfg.nx = 41; cfg.ny = 15;
    cfg.Lx = 3.0; cfg.Ly = 1.0;
    cfg.Re = 150.0;
    cfg.U = 1.0;
    cfg.kind = ScenarioKind2D::Channel;

    NavierStokes2D solver(cfg);
    for (int s = 0; s < 30; ++s) {
        double residual = solver.step();
        REQUIRE(std::isfinite(residual));
    }
    Fields2D f = solver.fields();
    REQUIRE(all_finite(f.velocity_u));
    REQUIRE(all_finite(f.velocity_v));

    // Inflow column should be exactly U (fixed BC).
    for (int j = 0; j < cfg.ny; ++j) {
        REQUIRE(f.velocity_u[static_cast<std::size_t>(j) * cfg.nx] == Catch::Approx(cfg.U));
    }
}

TEST_CASE("NavierStokes2D: obstacle scenario zeroes velocity inside the solid block", "[solvers][navier_stokes_2d]") {
    SolverConfig2D cfg;
    cfg.nx = 41; cfg.ny = 21;
    cfg.Lx = 3.0; cfg.Ly = 1.0;
    cfg.Re = 150.0;
    cfg.U = 1.0;
    cfg.kind = ScenarioKind2D::ObstacleScenario;
    cfg.obstacle = Obstacle2D{1.0, 0.3, 0.3};

    NavierStokes2D solver(cfg);
    for (int s = 0; s < 20; ++s) solver.step();

    Fields2D f = solver.fields();
    REQUIRE(all_finite(f.velocity_u));
    bool any_solid = false;
    for (std::size_t k = 0; k < f.obstacle.size(); ++k) {
        if (f.obstacle[k] == 1.0f) {
            any_solid = true;
            REQUIRE(f.velocity_u[k] == 0.0);
            REQUIRE(f.velocity_v[k] == 0.0);
        }
    }
    REQUIRE(any_solid); // the obstacle placement should actually intersect the grid
}
