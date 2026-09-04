#include "colormap.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr int kStopCount = 5;
constexpr std::array<std::array<int, 3>, kStopCount> kColormapStops = {{
    {{68, 1, 84}},
    {{59, 82, 139}},
    {{33, 145, 140}},
    {{94, 201, 98}},
    {{253, 231, 37}},
}};
} // namespace

QColor colormap_sample(double t) {
    t = std::clamp(t, 0.0, 1.0);
    double scaled = t * (kStopCount - 1);
    int lo = std::clamp(static_cast<int>(std::floor(scaled)), 0, kStopCount - 2);
    double frac = scaled - lo;
    const auto& a = kColormapStops[static_cast<std::size_t>(lo)];
    const auto& b = kColormapStops[static_cast<std::size_t>(lo + 1)];
    int r = static_cast<int>(a[0] + frac * (b[0] - a[0]));
    int g = static_cast<int>(a[1] + frac * (b[1] - a[1]));
    int bl = static_cast<int>(a[2] + frac * (b[2] - a[2]));
    return {r, g, bl};
}

namespace {
constexpr int kFlowStopCount = 6;
constexpr std::array<std::array<int, 3>, kFlowStopCount> kFlowColormapStops = {{
    {{20, 20, 140}},   // deep blue -- slowest
    {{0, 120, 255}},   // blue
    {{0, 230, 230}},   // cyan
    {{80, 230, 60}},   // green
    {{255, 225, 0}},   // yellow
    {{255, 40, 20}},   // red -- fastest
}};
} // namespace

QColor flow_colormap_sample(double t) {
    t = std::clamp(t, 0.0, 1.0);
    double scaled = t * (kFlowStopCount - 1);
    int lo = std::clamp(static_cast<int>(std::floor(scaled)), 0, kFlowStopCount - 2);
    double frac = scaled - lo;
    const auto& a = kFlowColormapStops[static_cast<std::size_t>(lo)];
    const auto& b = kFlowColormapStops[static_cast<std::size_t>(lo + 1)];
    int r = static_cast<int>(a[0] + frac * (b[0] - a[0]));
    int g = static_cast<int>(a[1] + frac * (b[1] - a[1]));
    int bl = static_cast<int>(a[2] + frac * (b[2] - a[2]));
    return {r, g, bl};
}
