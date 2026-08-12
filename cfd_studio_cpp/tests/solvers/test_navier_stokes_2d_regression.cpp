#include <catch_amalgamated.hpp>

#include <cmath>

#include "solvers/navier_stokes_2d.hpp"

using namespace cfd::solvers;

namespace {
bool all_finite(const std::vector<double>& v) {
    for (double x : v) if (!std::isfinite(x)) return false;
    return true;
}
} // namespace

// Regression test for a real bug found during development: both
// build_and_factorize_laplacian() and solve_psi() declared their per-
// neighbor coefficient array as `static const double coeffs[4]`. A
// function-local `static` initializes exactly once for the entire process,
// so the first NavierStokes2D ever constructed would permanently freeze
// `coeffs` (derived from ITS OWN dx/dy) for every later instance with
// different grid spacing -- silently corrupting the Laplacian assembly for
// any solver constructed after the first one with different dx/dy. This
// was invisible in isolated single-instance runs (order/count-dependent
// bug) and only showed up once multiple differently-configured solvers ran
// in the same process, exactly as a GUI switching between scenario presets
// or a CLI running multiple cases back to back would.
TEST_CASE("NavierStokes2D: sequential instances with different grid spacing don't corrupt each other", "[solvers][navier_stokes_2d]") {
    SolverConfig2D cavity_cfg;
    cavity_cfg.nx = 21; cavity_cfg.ny = 21;
    cavity_cfg.Re = 100.0;
    cavity_cfg.U = 1.0;
    cavity_cfg.kind = ScenarioKind2D::Cavity;
    {
        NavierStokes2D solver(cavity_cfg);
        for (int s = 0; s < 30; ++s) solver.step();
        REQUIRE(all_finite(solver.fields().velocity_u));
    }

    // Different dx/dy than the cavity case above (different nx/ny/Lx/Ly) --
    // constructed and run AFTER another instance already exists/existed in
    // this process, which is exactly the condition the bug needed.
    SolverConfig2D channel_cfg;
    channel_cfg.nx = 41; channel_cfg.ny = 15;
    channel_cfg.Lx = 3.0; channel_cfg.Ly = 1.0;
    channel_cfg.Re = 150.0;
    channel_cfg.U = 1.0;
    channel_cfg.kind = ScenarioKind2D::Channel;
    {
        NavierStokes2D solver(channel_cfg);
        for (int s = 0; s < 30; ++s) solver.step();
        Fields2D f = solver.fields();
        REQUIRE(all_finite(f.velocity_u));
        for (int j = 0; j < channel_cfg.ny; ++j) {
            REQUIRE(f.velocity_u[static_cast<std::size_t>(j) * channel_cfg.nx] == Catch::Approx(channel_cfg.U));
        }
    }

    // A third instance, back to yet another grid spacing, for good measure.
    {
        NavierStokes2D solver(cavity_cfg);
        for (int s = 0; s < 10; ++s) solver.step();
        REQUIRE(all_finite(solver.fields().velocity_u));
    }
}
