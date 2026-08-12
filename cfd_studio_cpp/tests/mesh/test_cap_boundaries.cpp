#include <catch_amalgamated.hpp>

#include "mesh/cap_boundaries.hpp"

using namespace cfd::mesh;

namespace {
Mesh make_closed_tetrahedron() {
    Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    mesh.triangles = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
    return mesh;
}

// A single open triangular "cap" missing -- an unclosed tetrahedron shell
// (3 faces instead of 4), leaving one open boundary loop of 3 vertices.
Mesh make_open_tetrahedron() {
    Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    mesh.triangles = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}}; // missing face (1,2,3)
    return mesh;
}

// A short square tube (hollow duct), open at both x-ends -- 8 vertices (4
// per ring), 8 side-wall triangles, two boundary loops of 4 vertices each.
Mesh make_square_tube() {
    Mesh mesh;
    mesh.vertices = {
        {0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}, // ring at x=0: 0,1,2,3
        {1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}, // ring at x=1: 4,5,6,7
    };
    auto quad = [&](int a, int b, int c, int d) {
        mesh.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(c)});
        mesh.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(c), static_cast<std::uint32_t>(d)});
    };
    // Side walls only -- outward-facing winding, no end caps.
    quad(0, 4, 5, 1);
    quad(1, 5, 6, 2);
    quad(2, 6, 7, 3);
    quad(3, 7, 4, 0);
    return mesh;
}
} // namespace

TEST_CASE("is_watertight: a closed tetrahedron is watertight", "[mesh][cap_boundaries]") {
    REQUIRE(is_watertight(make_closed_tetrahedron()));
}

TEST_CASE("is_watertight: an open shell is not watertight", "[mesh][cap_boundaries]") {
    REQUIRE_FALSE(is_watertight(make_open_tetrahedron()));
    REQUIRE_FALSE(is_watertight(make_square_tube()));
}

TEST_CASE("boundary_loops: open tetrahedron has one 3-vertex loop", "[mesh][cap_boundaries]") {
    auto loops = boundary_loops(make_open_tetrahedron());
    REQUIRE(loops.size() == 1);
    REQUIRE(loops[0].vertices.size() == 3);
}

TEST_CASE("boundary_loops: square tube has two 4-vertex loops", "[mesh][cap_boundaries]") {
    auto loops = boundary_loops(make_square_tube());
    REQUIRE(loops.size() == 2);
    for (const auto& loop : loops) REQUIRE(loop.vertices.size() == 4);
}

TEST_CASE("cap_open_boundaries: closing the open tetrahedron makes it watertight", "[mesh][cap_boundaries]") {
    Mesh capped = cap_open_boundaries(make_open_tetrahedron());
    REQUIRE(capped.triangles.size() == 4); // 3 original + 1 cap triangle (3-vertex loop needs exactly 1 fan triangle)
    REQUIRE(is_watertight(capped));
}

TEST_CASE("cap_open_boundaries: closing the square tube makes it watertight", "[mesh][cap_boundaries]") {
    Mesh capped = cap_open_boundaries(make_square_tube());
    // Each 4-vertex loop fans into 2 triangles; 8 original + 2*2 = 12.
    REQUIRE(capped.triangles.size() == 12);
    REQUIRE(is_watertight(capped));
}

TEST_CASE("cap_open_boundaries: already-watertight mesh is returned unchanged", "[mesh][cap_boundaries]") {
    Mesh mesh = make_closed_tetrahedron();
    Mesh result = cap_open_boundaries(mesh);
    REQUIRE(result.triangles.size() == mesh.triangles.size());
}
