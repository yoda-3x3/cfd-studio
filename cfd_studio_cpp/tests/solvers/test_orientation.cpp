#include <catch_amalgamated.hpp>

#include <cmath>

#include "solvers/orientation.hpp"

using namespace cfd::solvers;
using namespace cfd::mesh;

namespace {
// Elongated box: long in x (10), thin in y/z (1 each) -- the streamlined
// axis (smallest projected frontal area) should be x.
Mesh make_elongated_box() {
    Mesh mesh;
    double sx = 10.0, sy = 1.0, sz = 1.0;
    mesh.vertices = {
        {0, 0, 0}, {sx, 0, 0}, {sx, sy, 0}, {0, sy, 0},
        {0, 0, sz}, {sx, 0, sz}, {sx, sy, sz}, {0, sy, sz},
    };
    auto quad = [&](int a, int b, int c, int d) {
        mesh.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(c)});
        mesh.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(c), static_cast<std::uint32_t>(d)});
    };
    quad(0, 3, 2, 1);
    quad(4, 5, 6, 7);
    quad(0, 1, 5, 4);
    quad(3, 7, 6, 2);
    quad(0, 4, 7, 3);
    quad(1, 2, 6, 5);
    return mesh;
}
} // namespace

TEST_CASE("projected_silhouette_area: unit square projected along its own normal is 1", "[solvers][orientation]") {
    std::vector<Vec3> verts = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    double area = projected_silhouette_area(verts, Vec3{0, 0, 1});
    REQUIRE(area == Catch::Approx(1.0));
}

TEST_CASE("principal_axes: returns 3 unit, mutually orthogonal vectors", "[solvers][orientation]") {
    Mesh box = make_elongated_box();
    auto axes = principal_axes(box);
    for (const auto& a : axes) REQUIRE(norm(a) == Catch::Approx(1.0).margin(1e-6));
    REQUIRE(std::fabs(dot(axes[0], axes[1])) < 1e-6);
    REQUIRE(std::fabs(dot(axes[0], axes[2])) < 1e-6);
    REQUIRE(std::fabs(dot(axes[1], axes[2])) < 1e-6);
}

TEST_CASE("analyze_orientation: ranks the long axis of an elongated box as smallest-area (suggested)", "[solvers][orientation]") {
    Mesh box = make_elongated_box();
    auto candidates = analyze_orientation(box);

    REQUIRE(candidates[0].rank == 0);
    REQUIRE(candidates[0].label.find("suggested") != std::string::npos);
    // Areas must be non-decreasing by rank.
    REQUIRE(candidates[0].projected_area <= candidates[1].projected_area);
    REQUIRE(candidates[1].projected_area <= candidates[2].projected_area);

    // The suggested flow axis should be closely aligned with the box's
    // long (x) axis, since that gives the smallest frontal (y-z) area.
    double alignment = std::fabs(candidates[0].flow_axis.x);
    REQUIRE(alignment > 0.99);
}

TEST_CASE("candidate_from_flow_axis: produces an orthonormal right-handed basis", "[solvers][orientation]") {
    Mesh box = make_elongated_box();
    OrientationCandidate c = candidate_from_flow_axis(box, Vec3{1, 0, 0}, "test", 0);

    REQUIRE(norm(c.flow_axis) == Catch::Approx(1.0).margin(1e-9));
    REQUIRE(norm(c.up_axis) == Catch::Approx(1.0).margin(1e-9));
    REQUIRE(norm(c.span_axis) == Catch::Approx(1.0).margin(1e-9));
    REQUIRE(std::fabs(dot(c.flow_axis, c.up_axis)) < 1e-9);
    REQUIRE(std::fabs(dot(c.flow_axis, c.span_axis)) < 1e-9);
    REQUIRE(std::fabs(dot(c.up_axis, c.span_axis)) < 1e-9);
    // Right-handed: flow . (up x span) should be +1.
    REQUIRE(dot(c.flow_axis, cross(c.up_axis, c.span_axis)) == Catch::Approx(1.0).margin(1e-9));
}

TEST_CASE("apply_orientation: maps the suggested flow axis onto +x", "[solvers][orientation]") {
    Mesh box = make_elongated_box();
    auto candidates = analyze_orientation(box);
    Mesh oriented = apply_orientation(box, candidates[0]);

    auto b = oriented.bounds();
    // After orienting the long axis onto +x, the x-extent should be the
    // largest (matches the original box's long dimension, 10), and be
    // much larger than the other two.
    Vec3 ext = b.extents();
    REQUIRE(ext.x > ext.y * 5.0);
    REQUIRE(ext.x > ext.z * 5.0);
}
