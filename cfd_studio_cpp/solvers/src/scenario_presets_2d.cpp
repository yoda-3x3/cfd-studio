#include "solvers/scenario_presets_2d.hpp"

#include <unordered_map>

namespace cfd::solvers {

namespace {
const std::unordered_map<std::string, ScenarioPreset2D>& all_presets_2d() {
    static const std::unordered_map<std::string, ScenarioPreset2D> presets = {
        {"cavity", ScenarioPreset2D{"cavity", ScenarioKind2D::Cavity, 1.0, 1.0, 81, 81, 400.0, 1.0, false}},
        {"channel", ScenarioPreset2D{"channel", ScenarioKind2D::Channel, 3.0, 1.0, 181, 61, 200.0, 1.0, false}},
        {"obstacle", ScenarioPreset2D{"obstacle", ScenarioKind2D::ObstacleScenario, 4.0, 1.0, 241, 61, 200.0, 1.0, true}},
    };
    return presets;
}
} // namespace

const ScenarioPreset2D& scenario_preset_2d(const std::string& key) {
    return all_presets_2d().at(key);
}

std::array<std::string, 3> scenario_keys_2d() {
    return {"cavity", "channel", "obstacle"};
}

Obstacle2D default_obstacle_2d(const ScenarioPreset2D& preset) {
    return Obstacle2D{preset.Lx * 0.35, preset.Lx * 0.15, preset.Ly * 0.3};
}

} // namespace cfd::solvers
