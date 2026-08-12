#include <catch_amalgamated.hpp>

#include <cmath>
#include <vector>

#include "core/field3d.hpp"
#include "solvers/kernel_backend.hpp"

using cfd::core::Field3D;
using cfd::core::SolidMask3D;
using cfd::solvers::scalar_backend;
using cfd::solvers::threaded_backend;

namespace {
// Large enough on every axis that ThreadPool::parallel_for's "not worth it
// below n < workers*4" early-out doesn't just silently fall back to serial
// execution on a many-core machine -- this needs to actually exercise
// multiple threads to be a meaningful comparison.
constexpr int NX = 24, NY = 18, NZ = 14;

void fill_semi_realistic_fields(Field3D& u, Field3D& v, Field3D& w, Field3D& p, SolidMask3D& solid) {
    for (int i = 0; i <= NX + 1; ++i) {
        for (int j = 0; j <= NY + 1; ++j) {
            for (int k = 0; k <= NZ + 1; ++k) {
                u(i, j, k) = 1.0 + 0.1 * std::sin(0.3 * i) * std::cos(0.2 * j);
                v(i, j, k) = 0.05 * std::cos(0.25 * k);
                w(i, j, k) = 0.03 * std::sin(0.15 * i + 0.1 * k);
                p(i, j, k) = 0.01 * (i - NX / 2) * (j - NY / 2);
            }
        }
    }
    // A block obstacle, large enough to matter, not touching the boundary.
    for (int i = 8; i <= 12; ++i)
        for (int j = 6; j <= 9; ++j)
            for (int k = 5; k <= 8; ++k)
                solid(i, j, k) = 1;
}
} // namespace

TEST_CASE("kernels_threaded: compute_rhs is bit-identical to scalar", "[solvers][threaded]") {
    Field3D u(NX, NY, NZ), v(NX, NY, NZ), w(NX, NY, NZ), p(NX, NY, NZ);
    SolidMask3D solid(NX, NY, NZ);
    fill_semi_realistic_fields(u, v, w, p, solid);

    Field3D rhs_u_s(NX, NY, NZ), rhs_v_s(NX, NY, NZ), rhs_w_s(NX, NY, NZ);
    Field3D rhs_u_t(NX, NY, NZ), rhs_v_t(NX, NY, NZ), rhs_w_t(NX, NY, NZ);

    scalar_backend().compute_rhs(u.data(), v.data(), w.data(), solid.data(), 150.0, 0.05, 0.06, 0.07,
                                  NX, NY, NZ, rhs_u_s.data(), rhs_v_s.data(), rhs_w_s.data());
    threaded_backend().compute_rhs(u.data(), v.data(), w.data(), solid.data(), 150.0, 0.05, 0.06, 0.07,
                                    NX, NY, NZ, rhs_u_t.data(), rhs_v_t.data(), rhs_w_t.data());

    for (std::size_t i = 0; i < rhs_u_s.size(); ++i) {
        REQUIRE(rhs_u_s.data()[i] == rhs_u_t.data()[i]);
        REQUIRE(rhs_v_s.data()[i] == rhs_v_t.data()[i]);
        REQUIRE(rhs_w_s.data()[i] == rhs_w_t.data()[i]);
    }
}

TEST_CASE("kernels_threaded: divergence is bit-identical to scalar", "[solvers][threaded]") {
    Field3D u(NX, NY, NZ), v(NX, NY, NZ), w(NX, NY, NZ), p(NX, NY, NZ);
    SolidMask3D solid(NX, NY, NZ);
    fill_semi_realistic_fields(u, v, w, p, solid);

    Field3D div_s(NX, NY, NZ), div_t(NX, NY, NZ);
    scalar_backend().divergence(u.data(), v.data(), w.data(), solid.data(), 0.05, 0.06, 0.07, NX, NY, NZ, div_s.data());
    threaded_backend().divergence(u.data(), v.data(), w.data(), solid.data(), 0.05, 0.06, 0.07, NX, NY, NZ, div_t.data());

    for (std::size_t i = 0; i < div_s.size(); ++i) {
        REQUIRE(div_s.data()[i] == div_t.data()[i]);
    }
}

TEST_CASE("kernels_threaded: correct_velocity is bit-identical to scalar", "[solvers][threaded]") {
    Field3D u0(NX, NY, NZ), v0(NX, NY, NZ), w0(NX, NY, NZ), p(NX, NY, NZ);
    SolidMask3D solid(NX, NY, NZ);
    fill_semi_realistic_fields(u0, v0, w0, p, solid);

    Field3D us = u0, vs = v0, ws = w0;
    Field3D ut = u0, vt = v0, wt = w0;

    scalar_backend().correct_velocity(us.data(), vs.data(), ws.data(), p.data(), solid.data(), 0.001, 0.05, 0.06, 0.07, NX, NY, NZ);
    threaded_backend().correct_velocity(ut.data(), vt.data(), wt.data(), p.data(), solid.data(), 0.001, 0.05, 0.06, 0.07, NX, NY, NZ);

    for (std::size_t i = 0; i < us.size(); ++i) {
        REQUIRE(us.data()[i] == ut.data()[i]);
        REQUIRE(vs.data()[i] == vt.data()[i]);
        REQUIRE(ws.data()[i] == wt.data()[i]);
    }
}

TEST_CASE("kernels_threaded: jacobi_pressure_solve is bit-identical to scalar", "[solvers][threaded]") {
    Field3D u(NX, NY, NZ), v(NX, NY, NZ), w(NX, NY, NZ), p0(NX, NY, NZ), rhs(NX, NY, NZ);
    SolidMask3D solid(NX, NY, NZ);
    fill_semi_realistic_fields(u, v, w, p0, solid);
    for (int i = 0; i <= NX + 1; ++i)
        for (int j = 0; j <= NY + 1; ++j)
            for (int k = 0; k <= NZ + 1; ++k)
                rhs(i, j, k) = 0.02 * std::sin(0.1 * i) * std::cos(0.1 * j) * std::sin(0.1 * k);

    Field3D p_out_s(NX, NY, NZ), p_out_t(NX, NY, NZ);
    double residual_s = -1.0, residual_t = -1.0;

    scalar_backend().jacobi_pressure_solve(p0.data(), rhs.data(), solid.data(), 0.05, 0.06, 0.07, 40, 1e-10, 1.7,
                                            NX, NY, NZ, p_out_s.data(), &residual_s);
    threaded_backend().jacobi_pressure_solve(p0.data(), rhs.data(), solid.data(), 0.05, 0.06, 0.07, 40, 1e-10, 1.7,
                                              NX, NY, NZ, p_out_t.data(), &residual_t);

    REQUIRE(residual_s == residual_t);
    for (std::size_t i = 0; i < p_out_s.size(); ++i) {
        REQUIRE(p_out_s.data()[i] == p_out_t.data()[i]);
    }
}

TEST_CASE("kernels_threaded: thread count accessors", "[solvers][threaded]") {
    int max_threads = threaded_backend().max_thread_count();
    REQUIRE(max_threads >= 1);
    threaded_backend().set_thread_count(2);
    REQUIRE(threaded_backend().get_thread_count() == 2);
    threaded_backend().set_thread_count(max_threads); // restore for subsequent tests
}

TEST_CASE("kernels_threaded: repeated parallel-region entry/exit does not hang or crash", "[solvers][threaded][slow]") {
    // The whole reason this backend exists instead of OpenMP: an earlier
    // Fortran/OpenMP build stalled from fork/join overhead accumulating
    // over thousands of parallel-region entries on a long run. This is a
    // bounded stand-in for that failure mode -- if the persistent-thread-
    // pool design has a similar problem, it should show up well within a
    // few thousand repeated calls, not require an actual multi-minute run.
    Field3D u(NX, NY, NZ), v(NX, NY, NZ), w(NX, NY, NZ), p(NX, NY, NZ), rhs_u(NX, NY, NZ), rhs_v(NX, NY, NZ), rhs_w(NX, NY, NZ), div(NX, NY, NZ);
    SolidMask3D solid(NX, NY, NZ);
    fill_semi_realistic_fields(u, v, w, p, solid);

    threaded_backend().set_thread_count(threaded_backend().max_thread_count());

    for (int step = 0; step < 3000; ++step) {
        threaded_backend().compute_rhs(u.data(), v.data(), w.data(), solid.data(), 150.0, 0.05, 0.06, 0.07,
                                        NX, NY, NZ, rhs_u.data(), rhs_v.data(), rhs_w.data());
        threaded_backend().divergence(u.data(), v.data(), w.data(), solid.data(), 0.05, 0.06, 0.07, NX, NY, NZ, div.data());
        threaded_backend().correct_velocity(u.data(), v.data(), w.data(), p.data(), solid.data(), 1e-4, 0.05, 0.06, 0.07, NX, NY, NZ);
    }
    REQUIRE(true); // reaching here at all (not hanging/crashing) is the actual assertion
}
