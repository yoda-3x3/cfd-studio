#include "mesh/primitives.hpp"

#include <cmath>
#include <stdexcept>

namespace cfd::mesh {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;

void push_quad(Mesh& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    mesh.triangles.push_back({a, b, c});
    mesh.triangles.push_back({a, c, d});
}

std::uint32_t add_ring(Mesh& mesh, double x, double radius, int segments) {
    auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (int i = 0; i < segments; ++i) {
        double theta = kTwoPi * static_cast<double>(i) / static_cast<double>(segments);
        mesh.vertices.push_back({x, radius * std::cos(theta), radius * std::sin(theta)});
    }
    return base;
}
} // namespace

Mesh make_box(const Vec3& size) {
    Mesh mesh;
    double sx = size.x, sy = size.y, sz = size.z;
    mesh.vertices = {
        {0, 0, 0}, {sx, 0, 0}, {sx, sy, 0}, {0, sy, 0},
        {0, 0, sz}, {sx, 0, sz}, {sx, sy, sz}, {0, sy, sz},
    };
    push_quad(mesh, 0, 3, 2, 1); // bottom (z=0), outward -z
    push_quad(mesh, 4, 5, 6, 7); // top (z=sz), outward +z
    push_quad(mesh, 0, 1, 5, 4); // y=0
    push_quad(mesh, 3, 7, 6, 2); // y=sy
    push_quad(mesh, 0, 4, 7, 3); // x=0
    push_quad(mesh, 1, 2, 6, 5); // x=sx
    return mesh;
}

Mesh make_tube(double length, double outer_radius, double wall_thickness, int radial_segments, bool capped) {
    if (radial_segments < 3) throw std::invalid_argument("make_tube: radial_segments must be >= 3");

    Mesh mesh;
    bool walled = wall_thickness > 0.0 && wall_thickness < outer_radius;

    if (walled) {
        double inner_radius = outer_radius - wall_thickness;
        std::uint32_t outer0 = add_ring(mesh, 0.0, outer_radius, radial_segments);
        std::uint32_t outer1 = add_ring(mesh, length, outer_radius, radial_segments);
        std::uint32_t inner0 = add_ring(mesh, 0.0, inner_radius, radial_segments);
        std::uint32_t inner1 = add_ring(mesh, length, inner_radius, radial_segments);

        for (int i = 0; i < radial_segments; ++i) {
            auto ui = static_cast<std::uint32_t>(i);
            auto uj = static_cast<std::uint32_t>((i + 1) % radial_segments);
            // Outer wall, outward-facing.
            push_quad(mesh, outer0 + ui, outer1 + ui, outer1 + uj, outer0 + uj);
            // Inner wall, facing inward toward the lumen (reversed winding vs outer).
            push_quad(mesh, inner0 + ui, inner0 + uj, inner1 + uj, inner1 + ui);
        }
        if (capped) {
            for (int i = 0; i < radial_segments; ++i) {
                auto ui = static_cast<std::uint32_t>(i);
                auto uj = static_cast<std::uint32_t>((i + 1) % radial_segments);
                // x=0 end annulus (outward -x).
                push_quad(mesh, outer0 + ui, outer0 + uj, inner0 + uj, inner0 + ui);
                // x=length end annulus (outward +x).
                push_quad(mesh, outer1 + uj, outer1 + ui, inner1 + ui, inner1 + uj);
            }
        }
    } else {
        std::uint32_t ring0 = add_ring(mesh, 0.0, outer_radius, radial_segments);
        std::uint32_t ring1 = add_ring(mesh, length, outer_radius, radial_segments);

        for (int i = 0; i < radial_segments; ++i) {
            auto ui = static_cast<std::uint32_t>(i);
            auto uj = static_cast<std::uint32_t>((i + 1) % radial_segments);
            push_quad(mesh, ring0 + ui, ring1 + ui, ring1 + uj, ring0 + uj);
        }
        if (capped) {
            auto c0 = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back({0.0, 0.0, 0.0});
            auto c1 = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back({length, 0.0, 0.0});
            for (int i = 0; i < radial_segments; ++i) {
                auto ui = static_cast<std::uint32_t>(i);
                auto uj = static_cast<std::uint32_t>((i + 1) % radial_segments);
                mesh.triangles.push_back({c0, ring0 + uj, ring0 + ui}); // x=0 cap, outward -x
                mesh.triangles.push_back({c1, ring1 + ui, ring1 + uj}); // x=length cap, outward +x
            }
        }
    }
    return mesh;
}

} // namespace cfd::mesh
