#include <catch_amalgamated.hpp>

#include <cmath>

#include "solvers/kernel_backend.hpp"
#include "solvers/navier_stokes_3d.hpp"

using namespace cfd::solvers;

namespace {
bool all_finite(const std::vector<double>& v) {
    for (double x : v) {
        if (!std::isfinite(x)) return false;
    }
    return true;
}
} // namespace

TEST_CASE("NavierStokes3D: external flow, no obstacle, stays finite over several steps", "[solvers][navier_stokes_3d]") {
    SolverConfig3D cfg;
    cfg.nx = 12; cfg.ny = 8; cfg.nz = 6;
    cfg.Lx = 4.0; cfg.Ly = 2.0; cfg.Lz = 1.5;
    cfg.Re = 100.0;
    cfg.U_in = 1.0;
    cfg.domain_mode = DomainMode3D::External;

    NavierStokes3D solver(cfg, scalar_backend());
    for (int s = 0; s < 20; ++s) {
        double residual = solver.step();
        REQUIRE(std::isfinite(residual));
        REQUIRE(residual >= 0.0);
    }

    Fields3D f = solver.fields();
    REQUIRE(all_finite(f.velocity_u));
    REQUIRE(all_finite(f.velocity_v));
    REQUIRE(all_finite(f.velocity_w));
    REQUIRE(all_finite(f.pressure));
    for (float o : f.obstacle) REQUIRE(o == 0.0f); // no solid mask supplied
}

TEST_CASE("NavierStokes3D: obstacle cells report zero velocity", "[solvers][navier_stokes_3d]") {
    SolverConfig3D cfg;
    cfg.nx = 10; cfg.ny = 8; cfg.nz = 6;
    cfg.Lx = 3.0; cfg.Ly = 2.0; cfg.Lz = 1.5;
    cfg.Re = 80.0;
    cfg.U_in = 1.0;
    cfg.domain_mode = DomainMode3D::External;

    std::vector<std::uint8_t> solid_mask(static_cast<std::size_t>(cfg.nx) * cfg.ny * cfg.nz, 0);
    // A block near the center.
    for (int i = 4; i <= 5; ++i)
        for (int j = 3; j <= 4; ++j)
            for (int k = 2; k <= 3; ++k)
                solid_mask[static_cast<std::size_t>(i) * cfg.ny * cfg.nz + static_cast<std::size_t>(j) * cfg.nz + k] = 1;

    NavierStokes3D solver(cfg, scalar_backend(), &solid_mask);
    for (int s = 0; s < 10; ++s) solver.step();

    Fields3D f = solver.fields();
    for (int i = 4; i <= 5; ++i) {
        for (int j = 3; j <= 4; ++j) {
            for (int k = 2; k <= 3; ++k) {
                std::size_t flat = static_cast<std::size_t>(i) * cfg.ny * cfg.nz + static_cast<std::size_t>(j) * cfg.nz + k;
                REQUIRE(f.obstacle[flat] == 1.0f);
                REQUIRE(f.velocity_u[flat] == 0.0);
                REQUIRE(f.velocity_v[flat] == 0.0);
                REQUIRE(f.velocity_w[flat] == 0.0);
            }
        }
    }
    REQUIRE(all_finite(f.velocity_u));
}

TEST_CASE("NavierStokes3D: internal (pipe) mode stays finite over several steps", "[solvers][navier_stokes_3d]") {
    SolverConfig3D cfg;
    cfg.nx = 14; cfg.ny = 6; cfg.nz = 6;
    cfg.Lx = 5.0; cfg.Ly = 1.0; cfg.Lz = 1.0;
    cfg.Re = 100.0;
    cfg.U_in = 1.0;
    cfg.domain_mode = DomainMode3D::Internal;

    NavierStokes3D solver(cfg, scalar_backend());
    for (int s = 0; s < 20; ++s) {
        double residual = solver.step();
        REQUIRE(std::isfinite(residual));
    }
    Fields3D f = solver.fields();
    REQUIRE(all_finite(f.velocity_u));
    REQUIRE(all_finite(f.velocity_magnitude));
}

TEST_CASE("NavierStokes3D: scalar and threaded backends agree closely on the same case", "[solvers][navier_stokes_3d]") {
    // Not necessarily bit-identical (accumulation order in the pressure
    // solve's residual reduction can differ), but should track each other
    // closely -- a real divergence here would indicate a backend-specific
    // bug in the actual PDE physics, not just floating-point noise.
    SolverConfig3D cfg;
    cfg.nx = 12; cfg.ny = 10; cfg.nz = 8;
    cfg.Lx = 4.0; cfg.Ly = 2.0; cfg.Lz = 1.5;
    cfg.Re = 120.0;
    cfg.U_in = 1.0;

    NavierStokes3D solver_s(cfg, scalar_backend());
    NavierStokes3D solver_t(cfg, threaded_backend());
    for (int s = 0; s < 15; ++s) {
        solver_s.step();
        solver_t.step();
    }

    Fields3D fs = solver_s.fields();
    Fields3D ft = solver_t.fields();
    for (std::size_t k = 0; k < fs.velocity_u.size(); ++k) {
        REQUIRE(fs.velocity_u[k] == Catch::Approx(ft.velocity_u[k]).margin(1e-9));
    }
}
