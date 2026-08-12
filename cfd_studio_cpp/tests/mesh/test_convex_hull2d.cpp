#include <catch_amalgamated.hpp>

#include "mesh/convex_hull2d.hpp"

using namespace cfd::mesh;

TEST_CASE("convex_hull_area_2d: unit square", "[mesh]") {
    std::vector<Vec2> pts = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    REQUIRE(convex_hull_area_2d(pts) == Catch::Approx(1.0));
}

TEST_CASE("convex_hull_area_2d: interior points don't change the hull area", "[mesh]") {
    std::vector<Vec2> pts = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.5, 0.5}};
    REQUIRE(convex_hull_area_2d(pts) == Catch::Approx(1.0));
}

TEST_CASE("convex_hull_area_2d: right triangle", "[mesh]") {
    std::vector<Vec2> pts = {{0, 0}, {2, 0}, {0, 3}};
    REQUIRE(convex_hull_area_2d(pts) == Catch::Approx(3.0)); // 0.5 * 2 * 3
}

TEST_CASE("convex_hull_area_2d: fewer than 3 points is degenerate (returns 0)", "[mesh]") {
    REQUIRE(convex_hull_area_2d({}) == 0.0);
    REQUIRE(convex_hull_area_2d({{0, 0}}) == 0.0);
    REQUIRE(convex_hull_area_2d({{0, 0}, {1, 1}}) == 0.0);
}

TEST_CASE("convex_hull_area_2d: collinear points are degenerate (returns 0)", "[mesh]") {
    std::vector<Vec2> pts = {{0, 0}, {1, 0}, {2, 0}, {3, 0}};
    REQUIRE(convex_hull_area_2d(pts) == 0.0);
}
