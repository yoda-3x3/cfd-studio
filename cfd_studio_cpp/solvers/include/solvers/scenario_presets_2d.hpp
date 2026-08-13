#pragma once

#include <array>
#include <string>

#include "solvers/navier_stokes_2d.hpp"

namespace cfd::solvers {

// Port of solver/scenarios.py's ScenarioPreset/PRESETS.
struct ScenarioPreset2D {
    std::string key;
    ScenarioKind2D kind;
    double Lx = 0.0, Ly = 0.0;
    int default_nx = 0, default_ny = 0;
    double default_Re = 0.0, default_U = 0.0;
    bool has_obstacle = false;
};

// Throws std::out_of_range for an unknown key.
[[nodiscard]] const ScenarioPreset2D& scenario_preset_2d(const std::string& key);

[[nodiscard]] std::array<std::string, 3> scenario_keys_2d();

// The preset's default obstacle geometry (x0=Lx*0.35, width=Lx*0.15,
// height=Ly*0.3), matching solver/scenarios.py's build_solver. Only
// meaningful when preset.has_obstacle is true.
[[nodiscard]] Obstacle2D default_obstacle_2d(const ScenarioPreset2D& preset);

} // namespace cfd::solvers
