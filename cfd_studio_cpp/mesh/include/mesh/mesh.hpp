#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace cfd::mesh {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
};

[[nodiscard]] inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

[[nodiscard]] inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }

[[nodiscard]] inline Vec3 normalize(const Vec3& a) {
    double n = norm(a);
    return n > 0.0 ? a * (1.0 / n) : a;
}

// A triangle mesh: flat vertex array + one (v0,v1,v2) index triple per
// triangle, matching trimesh's own in-memory layout closely enough that
// porting solver3d/geometry.py's and orientation.py's algorithms over is a
// close translation rather than a redesign.
class Mesh {
public:
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;

    [[nodiscard]] bool empty() const noexcept { return triangles.empty(); }

    struct Bounds {
        Vec3 min, max;
        [[nodiscard]] Vec3 extents() const { return max - min; }
        [[nodiscard]] Vec3 center() const { return (min + max) * 0.5; }
    };

    [[nodiscard]] Bounds bounds() const {
        if (vertices.empty()) {
            throw std::runtime_error("Mesh::bounds() called on a mesh with no vertices");
        }
        Bounds b{vertices[0], vertices[0]};
        for (const auto& v : vertices) {
            b.min.x = std::min(b.min.x, v.x); b.max.x = std::max(b.max.x, v.x);
            b.min.y = std::min(b.min.y, v.y); b.max.y = std::max(b.max.y, v.y);
            b.min.z = std::min(b.min.z, v.z); b.max.z = std::max(b.max.z, v.z);
        }
        return b;
    }

    void translate(const Vec3& d) {
        for (auto& v : vertices) v = v + d;
    }

    void scale(double s) {
        for (auto& v : vertices) v = v * s;
    }
};

// Load a mesh by file extension (.stl/.obj/.ply/.off, case-insensitive),
// matching solver3d/geometry.py's load_mesh() dispatch. Throws
// std::runtime_error on an unrecognized extension, a missing/unreadable
// file, or malformed content.
[[nodiscard]] Mesh load_mesh(const std::string& path);

} // namespace cfd::mesh
