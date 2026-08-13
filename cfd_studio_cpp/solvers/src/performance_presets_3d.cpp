#include "solvers/performance_presets_3d.hpp"

#include <unordered_map>

namespace cfd::solvers {

namespace {
const std::unordered_map<std::string, PerformancePreset3D>& all_presets_3d() {
    static const std::unordered_map<std::string, PerformancePreset3D> presets = {
        {"fast_preview", PerformancePreset3D{"fast_preview", 50, 30, 30, 500, 25}},
        {"balanced", PerformancePreset3D{"balanced", 100, 60, 60, 1500, 25}},
        {"high_quality", PerformancePreset3D{"high_quality", 160, 100, 100, 3000, 50}},
    };
    return presets;
}
} // namespace

const PerformancePreset3D& performance_preset_3d(const std::string& key) {
    return all_presets_3d().at(key);
}

std::array<std::string, 3> performance_preset_keys_3d() {
    return {"fast_preview", "balanced", "high_quality"};
}

} // namespace cfd::solvers
