#pragma once

#include <vector>

namespace cfd::mesh {

struct Vec2 {
    double x = 0.0, y = 0.0;
};

// Area of the 2D convex hull of `points`, via Qhull -- the same library
// scipy.spatial.ConvexHull wraps, used by solver3d/orientation.py's
// projected-silhouette-area heuristic (rank candidate flow axes by the
// mesh's smallest projected frontal area). Returns 0.0 on degenerate input
// (fewer than 3 points, all collinear, or any other Qhull failure) rather
// than throwing -- callers should fall back to a bounding-box extent
// product in that case, mirroring orientation.py's own fallback.
[[nodiscard]] double convex_hull_area_2d(const std::vector<Vec2>& points);

} // namespace cfd::mesh
