#include "mesh/convex_hull2d.hpp"

#include <cmath>

#include <libqhullcpp/Qhull.h>
#include <libqhullcpp/QhullError.h>

namespace cfd::mesh {

namespace {
// Qhull's C core does not reliably surface collinear/degenerate 2D input
// as a catchable orgQhull::QhullError on this build -- confirmed by direct
// testing: it crashes instead of throwing. Detecting collinearity
// ourselves before ever calling into Qhull sidesteps that fragile path
// entirely, rather than depending on Qhull's own error handling for a case
// it doesn't handle safely here.
bool all_collinear(const std::vector<Vec2>& points) {
    // Find two points spread apart to define a reference line -- points
    // pointer-close to the first vertex would make the cross-product
    // magnitude test below spuriously fail to detect collinearity along
    // a direction that's genuinely spread out but happens to start near p0.
    const Vec2& p0 = points[0];
    std::size_t ref = 0;
    double best_dist2 = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        double dx = points[i].x - p0.x, dy = points[i].y - p0.y;
        double d2 = dx * dx + dy * dy;
        if (d2 > best_dist2) { best_dist2 = d2; ref = i; }
    }
    if (best_dist2 < 1e-24) return true; // every point coincides with p0

    double dx = points[ref].x - p0.x, dy = points[ref].y - p0.y;
    double scale = std::sqrt(best_dist2);
    for (const auto& p : points) {
        double cross = dx * (p.y - p0.y) - dy * (p.x - p0.x);
        if (std::fabs(cross) > 1e-9 * scale * scale) return false;
    }
    return true;
}
} // namespace

double convex_hull_area_2d(const std::vector<Vec2>& points) {
    if (points.size() < 3) return 0.0;
    if (all_collinear(points)) return 0.0;

    std::vector<double> coords;
    coords.reserve(points.size() * 2);
    for (const auto& p : points) {
        coords.push_back(p.x);
        coords.push_back(p.y);
    }

    try {
        orgQhull::Qhull qhull;
        qhull.runQhull("", 2, static_cast<int>(points.size()), coords.data(), "Qt");
        // Qhull's volume() is the hull's enclosed measure in its own
        // dimensionality -- area for a 2D hull, exactly the quantity
        // scipy.spatial.ConvexHull.volume returns for 2D input (the name
        // is dimension-generic, not a 3D-only concept).
        return qhull.volume();
    } catch (const orgQhull::QhullError&) {
        // Degenerate input (collinear points, near-zero precision) --
        // caller falls back to a bounding-box extent product.
        return 0.0;
    }
}

} // namespace cfd::mesh
