#include "mesh/cap_boundaries.hpp"

#include <array>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cfd::mesh {

namespace {
inline std::uint64_t edge_key(std::uint32_t a, std::uint32_t b) {
    std::uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
    return (static_cast<std::uint64_t>(lo) << 32) | hi;
}
} // namespace

bool is_watertight(const Mesh& mesh) {
    // Matches trimesh's actual mesh.is_watertight semantics (what this
    // codebase relies on throughout, e.g. solver3d/geometry.py): every
    // edge is shared by exactly two triangles, regardless of their
    // relative winding direction. This is deliberately more lenient than
    // a strict consistently-oriented-2-manifold check (that's closer to
    // trimesh's separate is_volume property, which nothing here reads) --
    // in particular, cap_open_boundaries's fan caps are not guaranteed to
    // wind opposite the boundary edge they close, only to face outward
    // from the mesh centroid, so a stricter directed check would reject
    // perfectly good caps this app already treats as watertight.
    if (mesh.triangles.empty()) return false;
    std::unordered_map<std::uint64_t, int> undirected_count;
    for (const auto& tri : mesh.triangles) {
        undirected_count[edge_key(tri[0], tri[1])]++;
        undirected_count[edge_key(tri[1], tri[2])]++;
        undirected_count[edge_key(tri[2], tri[0])]++;
    }
    for (const auto& [key, count] : undirected_count) {
        if (count != 2) return false;
    }
    return true;
}

std::vector<BoundaryLoop> boundary_loops(const Mesh& mesh) {
    std::unordered_map<std::uint64_t, int> undirected_count;
    std::unordered_map<std::uint64_t, std::pair<std::uint32_t, std::uint32_t>> last_directed;

    auto visit_edge = [&](std::uint32_t a, std::uint32_t b) {
        std::uint64_t uk = edge_key(a, b);
        undirected_count[uk]++;
        last_directed[uk] = {a, b};
    };

    for (const auto& tri : mesh.triangles) {
        visit_edge(tri[0], tri[1]);
        visit_edge(tri[1], tri[2]);
        visit_edge(tri[2], tri[0]);
    }

    std::unordered_map<std::uint32_t, std::uint32_t> next;
    std::unordered_set<std::uint32_t> boundary_verts;
    for (const auto& [uk, count] : undirected_count) {
        if (count != 1) continue; // boundary edge: used by exactly one triangle
        auto [a, b] = last_directed[uk];
        next[a] = b;
        boundary_verts.insert(a);
        boundary_verts.insert(b);
    }

    std::vector<BoundaryLoop> loops;
    std::unordered_set<std::uint32_t> visited;
    for (std::uint32_t start : boundary_verts) {
        if (visited.count(start)) continue;
        BoundaryLoop loop;
        std::uint32_t cur = start;
        while (!visited.count(cur)) {
            visited.insert(cur);
            loop.vertices.push_back(cur);
            auto it = next.find(cur);
            if (it == next.end()) break; // dangling/malformed boundary, stop this loop here
            cur = it->second;
        }
        if (loop.vertices.size() >= 3) loops.push_back(std::move(loop));
    }
    return loops;
}

Mesh cap_open_boundaries(const Mesh& mesh) {
    if (is_watertight(mesh)) return mesh;

    std::vector<BoundaryLoop> loops = boundary_loops(mesh);
    if (loops.empty()) return mesh;

    Vec3 centroid{0, 0, 0};
    for (const auto& v : mesh.vertices) centroid = centroid + v;
    if (!mesh.vertices.empty()) centroid = centroid * (1.0 / static_cast<double>(mesh.vertices.size()));

    Mesh result = mesh;
    for (const auto& loop : loops) {
        if (loop.vertices.size() < 3) continue;
        std::uint32_t anchor = loop.vertices[0];
        for (std::size_t i = 1; i + 1 < loop.vertices.size(); ++i) {
            std::array<std::uint32_t, 3> tri = {anchor, loop.vertices[i], loop.vertices[i + 1]};
            const Vec3& v0 = mesh.vertices[tri[0]];
            const Vec3& v1 = mesh.vertices[tri[1]];
            const Vec3& v2 = mesh.vertices[tri[2]];
            Vec3 normal = cross(v1 - v0, v2 - v0);
            Vec3 face_center = (v0 + v1 + v2) * (1.0 / 3.0);
            if (dot(normal, face_center - centroid) < 0) std::swap(tri[1], tri[2]);
            result.triangles.push_back(tri);
        }
    }
    return result;
}

} // namespace cfd::mesh
