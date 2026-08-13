#pragma once

#include <array>
#include <string>

namespace cfd::solvers {

// Port of solver3d/presets.py's PerformancePreset/PRESETS. Python's
// `prefer_native` backend hint isn't ported -- there's only the one native
// scalar/threaded backend choice in this codebase (kernel_backend.hpp), not
// a numba-vs-native distinction.
struct PerformancePreset3D {
    std::string key;
    int nx = 0, ny = 0, nz = 0;
    int steps = 0;
    int output_every = 0;
};

// Throws std::out_of_range for an unknown key.
[[nodiscard]] const PerformancePreset3D& performance_preset_3d(const std::string& key);

[[nodiscard]] std::array<std::string, 3> performance_preset_keys_3d();

inline constexpr const char* kDefaultPerformancePreset3D = "balanced";

} // namespace cfd::solvers
