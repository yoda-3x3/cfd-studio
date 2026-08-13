#include "solvers/materials.hpp"

#include <unordered_map>

namespace cfd::solvers {

namespace {
const std::unordered_map<std::string, MaterialPreset>& all_material_presets() {
    static const std::unordered_map<std::string, MaterialPreset> presets = {
        {"air_20c", MaterialPreset{"air_20c", "Air (20°C, 1 atm)", 1.204, 1.516e-5}},
        {"water_20c", MaterialPreset{"water_20c", "Water (20°C)", 998.2, 1.004e-6}},
    };
    return presets;
}
} // namespace

const MaterialPreset& material_preset(const std::string& key) {
    return all_material_presets().at(key);
}

std::array<std::string, 2> material_preset_keys() {
    return {"air_20c", "water_20c"};
}

double reynolds_number(double velocity, double length, double nu) {
    if (nu <= 0.0) return 0.0;
    return velocity * length / nu;
}

} // namespace cfd::solvers
