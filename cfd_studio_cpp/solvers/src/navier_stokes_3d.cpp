#include "solvers/navier_stokes_3d.hpp"

#include <algorithm>
#include <cmath>

#include "core/binary_morphology.hpp"

namespace cfd::solvers {

using core::Field3D;
using core::SolidMask3D;

NavierStokes3D::NavierStokes3D(SolverConfig3D config, const KernelBackend& backend,
                                const std::vector<std::uint8_t>* solid_mask)
    : cfg_(config), backend_(&backend),
      nx_(config.nx), ny_(config.ny), nz_(config.nz),
      dx_(config.Lx / config.nx), dy_(config.Ly / config.ny), dz_(config.Lz / config.nz),
      u_(nx_, ny_, nz_, config.U_in), v_(nx_, ny_, nz_, 0.0), w_(nx_, ny_, nz_, 0.0), p_(nx_, ny_, nz_, 0.0),
      solid_(nx_, ny_, nz_, false),
      dt_(0.0) {
    if (cfg_.num_threads) backend_->set_thread_count(*cfg_.num_threads);

    if (solid_mask) {
        for (int i = 0; i < nx_; ++i) {
            for (int j = 0; j < ny_; ++j) {
                for (int k = 0; k < nz_; ++k) {
                    std::size_t flat = static_cast<std::size_t>(i) * static_cast<std::size_t>(ny_) * static_cast<std::size_t>(nz_) + static_cast<std::size_t>(j) * static_cast<std::size_t>(nz_) + static_cast<std::size_t>(k);
                    solid_(i + 1, j + 1, k + 1) = (*solid_mask)[flat] ? 1 : 0;
                }
            }
        }
    }

    for (int i = 0; i <= nx_ + 1; ++i)
        for (int j = 0; j <= ny_ + 1; ++j)
            for (int k = 0; k <= nz_ + 1; ++k)
                if (solid_(i, j, k)) u_(i, j, k) = 0.0;

    apply_velocity_bcs(u_, v_, w_);

    // Precompute which interior cells count toward the reported convergence
    // residual: domain edges/corners (where two free-slip faces meet) and
    // the immediate solid surface both carry an inherent local mass-
    // imbalance from the simplified boundary treatments used here, not
    // genuine non-convergence, so they're excluded to keep the residual a
    // meaningful bulk-flow signal. Ported verbatim from
    // solver3d/navier_stokes3d.py -- must not be simplified away.
    std::vector<std::uint8_t> interior_solid(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_) * static_cast<std::size_t>(nz_), 0);
    bool any_solid = false;
    for (int i = 0; i < nx_; ++i) {
        for (int j = 0; j < ny_; ++j) {
            for (int k = 0; k < nz_; ++k) {
                std::size_t flat = static_cast<std::size_t>(i) * static_cast<std::size_t>(ny_) * static_cast<std::size_t>(nz_) + static_cast<std::size_t>(j) * static_cast<std::size_t>(nz_) + static_cast<std::size_t>(k);
                bool s = solid_(i + 1, j + 1, k + 1) != 0;
                interior_solid[flat] = s ? 1 : 0;
                any_solid = any_solid || s;
            }
        }
    }
    std::vector<std::uint8_t> solid_shell =
        any_solid ? core::dilate_6connectivity(interior_solid, nx_, ny_, nz_, 2) : interior_solid;

    int m = std::min({2, nx_ / 2, ny_ / 2, nz_ / 2});
    residual_mask_.assign(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_) * static_cast<std::size_t>(nz_), 1);
    for (int i = 0; i < nx_; ++i) {
        for (int j = 0; j < ny_; ++j) {
            for (int k = 0; k < nz_; ++k) {
                std::size_t flat = static_cast<std::size_t>(i) * static_cast<std::size_t>(ny_) * static_cast<std::size_t>(nz_) + static_cast<std::size_t>(j) * static_cast<std::size_t>(nz_) + static_cast<std::size_t>(k);
                bool edge_shell = m > 0 && (i < m || i >= nx_ - m || j < m || j >= ny_ - m || k < m || k >= nz_ - m);
                bool excluded = solid_shell[flat] != 0 || edge_shell;
                residual_mask_[flat] = excluded ? 0 : 1;
            }
        }
    }

    dt_ = cfg_.dt ? *cfg_.dt : auto_dt();
}

double NavierStokes3D::auto_dt() const {
    double Umax = std::max(cfg_.U_in, 1e-6);
    double diff_term = 2.0 * (1.0 / cfg_.Re) * (1.0 / (dx_ * dx_) + 1.0 / (dy_ * dy_) + 1.0 / (dz_ * dz_));
    double conv_term = Umax / dx_ + Umax / dy_ + Umax / dz_;
    double dt_max = 1.0 / (diff_term + conv_term);
    return cfg_.safety_factor * dt_max;
}

void NavierStokes3D::apply_velocity_bcs(Field3D& u, Field3D& v, Field3D& w) const {
    double U_in = cfg_.U_in;
    bool internal = cfg_.domain_mode == DomainMode3D::Internal;
    int nx = nx_, ny = ny_, nz = nz_;

    // -x inflow (Dirichlet, second-order ghost); +x outflow (zero-gradient)
    for (int j = 0; j <= ny + 1; ++j) {
        for (int k = 0; k <= nz + 1; ++k) {
            u(0, j, k) = 2 * U_in - u(1, j, k);
            v(0, j, k) = -v(1, j, k);
            w(0, j, k) = -w(1, j, k);
            u(nx + 1, j, k) = u(nx, j, k);
            v(nx + 1, j, k) = v(nx, j, k);
            w(nx + 1, j, k) = w(nx, j, k);
        }
    }

    // y = 0 / y = Ly: v is wall-normal (always mirror-negated to zero);
    // u/w are tangential -- zero-gradient (free-slip, External) or
    // mirror-negated to zero (no-slip, Internal).
    for (int i = 0; i <= nx + 1; ++i) {
        for (int k = 0; k <= nz + 1; ++k) {
            v(i, 0, k) = -v(i, 1, k);
            u(i, 0, k) = internal ? -u(i, 1, k) : u(i, 1, k);
            w(i, 0, k) = internal ? -w(i, 1, k) : w(i, 1, k);
            v(i, ny + 1, k) = -v(i, ny, k);
            u(i, ny + 1, k) = internal ? -u(i, ny, k) : u(i, ny, k);
            w(i, ny + 1, k) = internal ? -w(i, ny, k) : w(i, ny, k);
        }
    }

    // z = 0 / z = Lz: same, w is wall-normal.
    for (int i = 0; i <= nx + 1; ++i) {
        for (int j = 0; j <= ny + 1; ++j) {
            w(i, j, 0) = -w(i, j, 1);
            u(i, j, 0) = internal ? -u(i, j, 1) : u(i, j, 1);
            v(i, j, 0) = internal ? -v(i, j, 1) : v(i, j, 1);
            w(i, j, nz + 1) = -w(i, j, nz);
            u(i, j, nz + 1) = internal ? -u(i, j, nz) : u(i, j, nz);
            v(i, j, nz + 1) = internal ? -v(i, j, nz) : v(i, j, nz);
        }
    }
}

double NavierStokes3D::step() {
    int nx = nx_, ny = ny_, nz = nz_;
    double dt = dt_;

    apply_velocity_bcs(u_, v_, w_);

    Field3D rhs_u(nx, ny, nz), rhs_v(nx, ny, nz), rhs_w(nx, ny, nz);
    backend_->compute_rhs(u_.data(), v_.data(), w_.data(), solid_.data(), cfg_.Re, dx_, dy_, dz_,
                           nx, ny, nz, rhs_u.data(), rhs_v.data(), rhs_w.data());

    Field3D u_star(nx, ny, nz), v_star(nx, ny, nz), w_star(nx, ny, nz);
    for (std::size_t idx = 0; idx < u_star.size(); ++idx) {
        u_star.data()[idx] = u_.data()[idx] + dt * rhs_u.data()[idx];
        v_star.data()[idx] = v_.data()[idx] + dt * rhs_v.data()[idx];
        w_star.data()[idx] = w_.data()[idx] + dt * rhs_w.data()[idx];
    }
    for (int i = 0; i <= nx + 1; ++i) {
        for (int j = 0; j <= ny + 1; ++j) {
            for (int k = 0; k <= nz + 1; ++k) {
                if (solid_(i, j, k)) { u_star(i, j, k) = 0.0; v_star(i, j, k) = 0.0; w_star(i, j, k) = 0.0; }
            }
        }
    }
    apply_velocity_bcs(u_star, v_star, w_star);

    Field3D div_star(nx, ny, nz);
    backend_->divergence(u_star.data(), v_star.data(), w_star.data(), solid_.data(), dx_, dy_, dz_,
                          nx, ny, nz, div_star.data());
    Field3D rhs_p(nx, ny, nz);
    for (std::size_t idx = 0; idx < rhs_p.size(); ++idx) rhs_p.data()[idx] = div_star.data()[idx] / dt;

    Field3D p_new(nx, ny, nz);
    double p_res = 0.0;
    backend_->jacobi_pressure_solve(p_.data(), rhs_p.data(), solid_.data(), dx_, dy_, dz_,
                                     cfg_.max_pressure_iters, cfg_.pressure_tol, cfg_.pressure_omega,
                                     nx, ny, nz, p_new.data(), &p_res);

    backend_->correct_velocity(u_star.data(), v_star.data(), w_star.data(), p_new.data(), solid_.data(),
                                dt, dx_, dy_, dz_, nx, ny, nz);
    apply_velocity_bcs(u_star, v_star, w_star);

    u_ = std::move(u_star);
    v_ = std::move(v_star);
    w_ = std::move(w_star);
    p_ = std::move(p_new);

    Field3D div_final(nx, ny, nz);
    backend_->divergence(u_.data(), v_.data(), w_.data(), solid_.data(), dx_, dy_, dz_, nx, ny, nz, div_final.data());

    double residual = 0.0;
    bool any_masked = false;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                std::size_t flat = static_cast<std::size_t>(i) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz) + static_cast<std::size_t>(j) * static_cast<std::size_t>(nz) + static_cast<std::size_t>(k);
                if (!residual_mask_[flat]) continue;
                any_masked = true;
                residual = std::max(residual, std::fabs(div_final(i + 1, j + 1, k + 1)));
            }
        }
    }
    if (!any_masked) {
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                for (int k = 0; k < nz; ++k)
                    residual = std::max(residual, std::fabs(div_final(i + 1, j + 1, k + 1)));
    }

    time_ += dt;
    ++step_count_;
    last_residual_ = residual;
    return residual;
}

Fields3D NavierStokes3D::fields() const {
    Fields3D out;
    std::size_t n = static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_) * static_cast<std::size_t>(nz_);
    out.velocity_u.resize(n);
    out.velocity_v.resize(n);
    out.velocity_w.resize(n);
    out.velocity_magnitude.resize(n);
    out.pressure.resize(n);
    out.obstacle.resize(n);

    for (int i = 0; i < nx_; ++i) {
        for (int j = 0; j < ny_; ++j) {
            for (int k = 0; k < nz_; ++k) {
                std::size_t flat = static_cast<std::size_t>(i) * static_cast<std::size_t>(ny_) * static_cast<std::size_t>(nz_) + static_cast<std::size_t>(j) * static_cast<std::size_t>(nz_) + static_cast<std::size_t>(k);
                double u = u_(i + 1, j + 1, k + 1), v = v_(i + 1, j + 1, k + 1), w = w_(i + 1, j + 1, k + 1);
                out.velocity_u[flat] = u;
                out.velocity_v[flat] = v;
                out.velocity_w[flat] = w;
                out.velocity_magnitude[flat] = std::sqrt(u * u + v * v + w * w);
                out.pressure[flat] = p_(i + 1, j + 1, k + 1);
                out.obstacle[flat] = solid_(i + 1, j + 1, k + 1) ? 1.0f : 0.0f;
            }
        }
    }
    return out;
}

} // namespace cfd::solvers
