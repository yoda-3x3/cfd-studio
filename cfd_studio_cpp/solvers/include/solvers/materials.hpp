#pragma once

#include <array>
#include <string>

namespace cfd::solvers {

// Fluid property presets, for deriving a Reynolds number from real
// physical inputs (characteristic length, velocity, fluid) instead of
// entering Re directly. Port of solver3d/materials.py's MaterialPreset.
// Only kinematic viscosity (nu) feeds the solver math (Re = U*L/nu);
// density is reference/display only -- the solver is non-dimensional and
// has no density-dependent terms.
struct MaterialPreset {
    std::string key, label;
    double rho = 0.0; // kg/m^3, reference/display only
    double nu = 0.0;  // m^2/s, kinematic viscosity, used in Re = U*L/nu
};

// Throws std::out_of_range for an unknown key.
[[nodiscard]] const MaterialPreset& material_preset(const std::string& key);

[[nodiscard]] std::array<std::string, 2> material_preset_keys();

// Returns 0.0 for nu <= 0, matching solver3d/materials.py's reynolds_number.
[[nodiscard]] double reynolds_number(double velocity, double length, double nu);

} // namespace cfd::solvers
