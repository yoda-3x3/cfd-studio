#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/field3d.hpp"
#include "solvers/kernel_backend.hpp"

namespace cfd::solvers {

enum class DomainMode3D { External, Internal };

// Port of solver3d/navier_stokes3d.py's SolverConfig3D. Domain is treated
// as a virtual wind tunnel: uniform inflow on the -x face, convective
// (zero-gradient) outflow on the +x face; the four lateral faces are
// free-slip (External, an approximation of an unbounded domain) or no-slip
// (Internal, a pipe/duct whose own wall the lateral faces represent).
struct SolverConfig3D {
    int nx = 0, ny = 0, nz = 0;
    double Lx = 0.0, Ly = 0.0, Lz = 0.0;
    double Re = 200.0;
    double U_in = 1.0;
    std::optional<double> dt; // nullopt => auto-computed stable dt
    double safety_factor = 0.4;
    int max_pressure_iters = 80;
    double pressure_tol = 1e-4;
    double pressure_omega = 1.0;
    std::optional<int> num_threads; // applied to the backend at construction, if set
    DomainMode3D domain_mode = DomainMode3D::External;
    // Opt-in, External-only: makes the y=0 face a no-slip wall (a ground
    // plane) instead of free-slip, for ground-effect simulations. Has no
    // effect when domain_mode is Internal, whose lateral faces (y=0
    // included) are already no-slip regardless.
    bool ground_effect = false;
};

// Interior-only (unpadded, nx*ny*nz) output fields, matching
// solver3d/navier_stokes3d.py's fields() dict.
struct Fields3D {
    std::vector<double> velocity_u, velocity_v, velocity_w, velocity_magnitude, pressure;
    std::vector<float> obstacle;
};

// 3D incompressible Navier-Stokes solver, explicit projection (Chorin)
// method on a collocated, ghost-cell-padded grid -- port of
// solver3d/navier_stokes3d.py's NavierStokes3DSolver. Holds a
// `const KernelBackend*` chosen once at construction (see
// kernel_backend.hpp) instead of Python's global backend-rebinding: thread-
// safe, testable, multiple solver instances never interfere.
class NavierStokes3D {
public:
    // `solid_mask`, if provided, must have exactly config.nx*config.ny*config.nz
    // entries in (i,j,k) C-order (i slowest, k fastest), true = solid cell.
    NavierStokes3D(SolverConfig3D config, const KernelBackend& backend,
                   const std::vector<std::uint8_t>* solid_mask = nullptr);

    double step(); // advances one time step, returns the convergence residual

    [[nodiscard]] Fields3D fields() const;

    [[nodiscard]] double time() const { return time_; }
    [[nodiscard]] int step_count() const { return step_count_; }
    [[nodiscard]] double last_residual() const { return last_residual_; }
    [[nodiscard]] double dt() const { return dt_; }
    [[nodiscard]] double dx() const { return dx_; }
    [[nodiscard]] double dy() const { return dy_; }
    [[nodiscard]] double dz() const { return dz_; }

private:
    void apply_velocity_bcs(core::Field3D& u, core::Field3D& v, core::Field3D& w) const;
    [[nodiscard]] double auto_dt() const;

    SolverConfig3D cfg_;
    const KernelBackend* backend_;
    int nx_, ny_, nz_;
    double dx_, dy_, dz_;
    core::Field3D u_, v_, w_, p_;
    core::SolidMask3D solid_;
    std::vector<std::uint8_t> residual_mask_; // unpadded nx*ny*nz, true = included in residual
    double time_ = 0.0;
    int step_count_ = 0;
    double last_residual_ = 0.0;
    double dt_;
};

} // namespace cfd::solvers
