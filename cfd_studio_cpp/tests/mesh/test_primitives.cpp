#include <catch_amalgamated.hpp>

#include <cstdio>
#include <filesystem>

#include "mesh/cap_boundaries.hpp"
#include "mesh/primitives.hpp"
#include "mesh/stl_writer.hpp"

using namespace cfd::mesh;
namespace fs = std::filesystem;

TEST_CASE("make_box: produces a closed 6-face box with the expected bounds", "[mesh][primitives]") {
    Mesh box = make_box({2.0, 1.0, 0.5});
    REQUIRE(box.vertices.size() == 8);
    REQUIRE(box.triangles.size() == 12);

    auto b = box.bounds();
    REQUIRE(b.min.x == Catch::Approx(0.0));
    REQUIRE(b.max.x == Catch::Approx(2.0));
    REQUIRE(b.max.y == Catch::Approx(1.0));
    REQUIRE(b.max.z == Catch::Approx(0.5));
    REQUIRE(is_watertight(box));
}

TEST_CASE("make_tube: walled + capped produces a watertight pipe with a distinct lumen", "[mesh][primitives]") {
    Mesh tube = make_tube(4.0, 0.5, 0.1, 16, /*capped=*/true);
    REQUIRE(is_watertight(tube));

    auto b = tube.bounds();
    REQUIRE(b.min.x == Catch::Approx(0.0));
    REQUIRE(b.max.x == Catch::Approx(4.0));
    // Outer radius 0.5 -> y/z span [-0.5, 0.5].
    REQUIRE(b.min.y == Catch::Approx(-0.5).margin(1e-9));
    REQUIRE(b.max.y == Catch::Approx(0.5).margin(1e-9));
}

TEST_CASE("make_tube: walled + uncapped is open (not watertight)", "[mesh][primitives]") {
    Mesh tube = make_tube(4.0, 0.5, 0.1, 16, /*capped=*/false);
    REQUIRE_FALSE(is_watertight(tube));
}

TEST_CASE("make_tube: thin-walled (no wall_thickness) capped is a closed single-shell cylinder", "[mesh][primitives]") {
    Mesh tube = make_tube(2.0, 0.3, 0.0, 12, /*capped=*/true);
    REQUIRE(is_watertight(tube));
}

TEST_CASE("make_tube: rejects fewer than 3 radial segments", "[mesh][primitives]") {
    REQUIRE_THROWS(make_tube(1.0, 0.5, 0.1, 2, true));
}

TEST_CASE("write_stl + read_stl round-trip preserves triangle count and bounds", "[mesh][primitives]") {
    Mesh box = make_box({1.5, 2.5, 0.75});
    std::string path = (fs::temp_directory_path() / "cfd_primitives_roundtrip.stl").string();
    write_stl(box, path);

    Mesh reloaded = load_mesh(path);
    REQUIRE(reloaded.triangles.size() == box.triangles.size());
    // STL is non-indexed (3 fresh vertices per triangle), unlike the
    // in-memory shared-vertex box -- compare geometry via bounds, not counts.
    auto b1 = box.bounds();
    auto b2 = reloaded.bounds();
    REQUIRE(b2.min.x == Catch::Approx(b1.min.x));
    REQUIRE(b2.max.x == Catch::Approx(b1.max.x));
    REQUIRE(b2.max.y == Catch::Approx(b1.max.y));
    REQUIRE(b2.max.z == Catch::Approx(b1.max.z));

    std::remove(path.c_str());
}
