#include "mesh/clip.hpp"

#include <cmath>

namespace cfd::mesh {

namespace {

// Clips a single triangle against the half-space normal.(p-origin) >= 0,
// appending 0, 1, or 2 resulting triangles to `out`.
void clip_triangle_against_plane(const RawTriangle& tri, const Vec3& normal, const Vec3& origin,
                                  std::vector<RawTriangle>& out) {
    std::array<double, 3> dist;
    for (int i = 0; i < 3; ++i) dist[static_cast<std::size_t>(i)] = dot(normal, tri[static_cast<std::size_t>(i)] - origin);

    std::array<bool, 3> inside = {dist[0] >= 0.0, dist[1] >= 0.0, dist[2] >= 0.0};
    int n_inside = (inside[0] ? 1 : 0) + (inside[1] ? 1 : 0) + (inside[2] ? 1 : 0);

    if (n_inside == 0) return;
    if (n_inside == 3) { out.push_back(tri); return; }

    auto intersect = [&](int a, int b) -> Vec3 {
        double t = dist[static_cast<std::size_t>(a)] / (dist[static_cast<std::size_t>(a)] - dist[static_cast<std::size_t>(b)]);
        return tri[static_cast<std::size_t>(a)] + (tri[static_cast<std::size_t>(b)] - tri[static_cast<std::size_t>(a)]) * t;
    };

    if (n_inside == 1) {
        int in_idx = inside[0] ? 0 : (inside[1] ? 1 : 2);
        int o1 = (in_idx + 1) % 3, o2 = (in_idx + 2) % 3;
        Vec3 p1 = intersect(in_idx, o1);
        Vec3 p2 = intersect(in_idx, o2);
        out.push_back({tri[static_cast<std::size_t>(in_idx)], p1, p2});
        return;
    }

    // n_inside == 2: the clipped region is a quad (two original inside
    // vertices + two new intersection points), split into 2 triangles.
    // The exact winding here doesn't need to match any particular
    // convention -- callers that care about outward-facing normals (e.g.
    // io/openfoam_writer.py's _refine_object_patch) re-orient the final
    // triangle explicitly against a known reference point downstream.
    int out_idx = !inside[0] ? 0 : (!inside[1] ? 1 : 2);
    int i1 = (out_idx + 1) % 3, i2 = (out_idx + 2) % 3;
    Vec3 p1 = intersect(i1, out_idx);
    Vec3 p2 = intersect(i2, out_idx);
    out.push_back({tri[static_cast<std::size_t>(i1)], tri[static_cast<std::size_t>(i2)], p2});
    out.push_back({tri[static_cast<std::size_t>(i1)], p2, p1});
}

bool all_close_on_axis(const RawTriangle& tri, int axis, double value, double tol) {
    for (const auto& v : tri) {
        double c = axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
        if (std::fabs(c - value) >= tol) return false;
    }
    return true;
}

} // namespace

std::vector<RawTriangle> clip_mesh_to_box(
    const Mesh& mesh, double xmin, double xmax, double ymin, double ymax, double zmin, double zmax, double tol) {
    std::vector<RawTriangle> current;
    current.reserve(mesh.triangles.size());
    for (const auto& tri : mesh.triangles) {
        current.push_back({mesh.vertices[tri[0]], mesh.vertices[tri[1]], mesh.vertices[tri[2]]});
    }

    struct PlaneSpec { Vec3 normal, origin; };
    const std::array<PlaneSpec, 6> planes = {{
        {{1.0, 0.0, 0.0}, {xmin, 0.0, 0.0}}, {{-1.0, 0.0, 0.0}, {xmax, 0.0, 0.0}},
        {{0.0, 1.0, 0.0}, {0.0, ymin, 0.0}}, {{0.0, -1.0, 0.0}, {0.0, ymax, 0.0}},
        {{0.0, 0.0, 1.0}, {0.0, 0.0, zmin}}, {{0.0, 0.0, -1.0}, {0.0, 0.0, zmax}},
    }};

    for (const auto& plane : planes) {
        if (current.empty()) return {};
        std::vector<RawTriangle> next;
        next.reserve(current.size());
        for (const auto& tri : current) clip_triangle_against_plane(tri, plane.normal, plane.origin, next);
        current = std::move(next);
    }

    std::vector<RawTriangle> result;
    result.reserve(current.size());
    for (const auto& tri : current) {
        bool on_box_plane =
            all_close_on_axis(tri, 0, xmin, tol) || all_close_on_axis(tri, 0, xmax, tol) ||
            all_close_on_axis(tri, 1, ymin, tol) || all_close_on_axis(tri, 1, ymax, tol) ||
            all_close_on_axis(tri, 2, zmin, tol) || all_close_on_axis(tri, 2, zmax, tol);
        if (!on_box_plane) result.push_back(tri);
    }
    return result;
}

} // namespace cfd::mesh
