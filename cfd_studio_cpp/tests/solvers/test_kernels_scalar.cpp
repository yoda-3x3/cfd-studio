#include <catch_amalgamated.hpp>

#include <cmath>
#include <vector>

#include "core/field3d.hpp"
#include "solvers/kernel_backend.hpp"

using cfd::core::Field3D;
using cfd::core::SolidMask3D;
using cfd::solvers::scalar_backend;

namespace {
constexpr int NX = 6, NY = 5, NZ = 4;
}

TEST_CASE("kernels_scalar: divergence of a uniform velocity field is ~0", "[solvers]") {
    Field3D u(NX, NY, NZ, 1.0), v(NX, NY, NZ, 0.0), w(NX, NY, NZ, 0.0);
    SolidMask3D solid(NX, NY, NZ, false);
    Field3D div(NX, NY, NZ, -999.0);

    scalar_backend().divergence(u.data(), v.data(), w.data(), solid.data(),
                                 0.1, 0.1, 0.1, NX, NY, NZ, div.data());

    for (int i = 1; i <= NX; ++i)
        for (int j = 1; j <= NY; ++j)
            for (int k = 1; k <= NZ; ++k)
                REQUIRE(std::fabs(div(i, j, k)) < 1e-12);
}

TEST_CASE("kernels_scalar: compute_rhs skips solid cells (stays zero)", "[solvers]") {
    Field3D u(NX, NY, NZ, 1.0), v(NX, NY, NZ, 0.3), w(NX, NY, NZ, -0.2);
    SolidMask3D solid(NX, NY, NZ, false);
    solid(3, 3, 2) = 1;

    Field3D rhs_u(NX, NY, NZ), rhs_v(NX, NY, NZ), rhs_w(NX, NY, NZ);
    scalar_backend().compute_rhs(u.data(), v.data(), w.data(), solid.data(),
                                  100.0, 0.1, 0.1, 0.1, NX, NY, NZ,
                                  rhs_u.data(), rhs_v.data(), rhs_w.data());

    REQUIRE(rhs_u(3, 3, 2) == 0.0);
    REQUIRE(rhs_v(3, 3, 2) == 0.0);
    REQUIRE(rhs_w(3, 3, 2) == 0.0);
}

TEST_CASE("kernels_scalar: correct_velocity zeroes solid cells regardless of pressure gradient", "[solvers]") {
    Field3D u(NX, NY, NZ, 1.0), v(NX, NY, NZ, 1.0), w(NX, NY, NZ, 1.0);
    Field3D p(NX, NY, NZ);
    SolidMask3D solid(NX, NY, NZ, false);
    solid(2, 2, 2) = 1;

    // Give pressure some non-trivial gradient so the fluid-cell branch is exercised too.
    for (int i = 0; i <= NX + 1; ++i)
        for (int j = 0; j <= NY + 1; ++j)
            for (int k = 0; k <= NZ + 1; ++k)
                p(i, j, k) = static_cast<double>(i);

    scalar_backend().correct_velocity(u.data(), v.data(), w.data(), p.data(), solid.data(),
                                       0.01, 0.1, 0.1, 0.1, NX, NY, NZ);

    REQUIRE(u(2, 2, 2) == 0.0);
    REQUIRE(v(2, 2, 2) == 0.0);
    REQUIRE(w(2, 2, 2) == 0.0);
    // A fluid cell should have been perturbed away from its initial value of 1.0
    // by the pressure gradient correction.
    REQUIRE(u(4, 4, 2) != 1.0);
}

TEST_CASE("kernels_scalar: jacobi_pressure_solve converges on a zero RHS", "[solvers]") {
    Field3D p0(NX, NY, NZ, 0.0);
    Field3D rhs(NX, NY, NZ, 0.0);
    SolidMask3D solid(NX, NY, NZ, false);
    Field3D p_out(NX, NY, NZ);
    double residual = -1.0;

    scalar_backend().jacobi_pressure_solve(p0.data(), rhs.data(), solid.data(),
                                            0.1, 0.1, 0.1, 200, 1e-8, 1.7,
                                            NX, NY, NZ, p_out.data(), &residual);

    // Zero RHS + zero initial pressure is already the (trivial) solution --
    // should converge immediately to a near-zero residual.
    REQUIRE(residual < 1e-6);
}

TEST_CASE("kernels_scalar: set/get/max_thread_count are the trivial single-thread stubs", "[solvers]") {
    const auto& backend = scalar_backend();
    REQUIRE(backend.max_thread_count() == 1);
    backend.set_thread_count(8); // must be a no-op, not a crash
    REQUIRE(backend.get_thread_count() == 1);
}
