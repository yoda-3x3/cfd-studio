#include <catch_amalgamated.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>

#include "mesh/mesh.hpp"
#include "mesh/obj_reader.hpp"
#include "mesh/off_reader.hpp"
#include "mesh/ply_reader.hpp"
#include "mesh/stl_reader.hpp"

#ifndef CFD_TEST_FIXTURES_DIR
#define CFD_TEST_FIXTURES_DIR "."
#endif

using namespace cfd::mesh;

namespace {
std::string fixture(const std::string& name) {
    return std::string(CFD_TEST_FIXTURES_DIR) + "/" + name;
}

// All 4 tetrahedron fixtures describe the same shape: unit-right-angle
// tetrahedron at the origin, 4 vertices, 4 triangular faces.
void check_is_unit_tetrahedron(const Mesh& mesh, std::size_t expected_vertex_count) {
    REQUIRE(mesh.vertices.size() == expected_vertex_count);
    REQUIRE(mesh.triangles.size() == 4);
    auto b = mesh.bounds();
    REQUIRE(b.min.x == Catch::Approx(0.0));
    REQUIRE(b.max.x == Catch::Approx(1.0));
}
} // namespace

TEST_CASE("read_stl: ASCII tetrahedron fixture", "[mesh]") {
    Mesh mesh = read_stl(fixture("tetrahedron.stl"));
    // STL has no shared-vertex concept -- 4 triangles * 3 corners each,
    // unwelded.
    check_is_unit_tetrahedron(mesh, 12);
}

TEST_CASE("read_stl: binary round-trip", "[mesh]") {
    std::string path = fixture("_tmp_binary.stl");
    {
        std::ofstream f(path, std::ios::binary);
        char header[80] = {};
        std::uint32_t count = 1;
        f.write(header, 80);
        f.write(reinterpret_cast<char*>(&count), sizeof(count));
        float normal[3] = {0, 0, 1};
        float verts[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        std::uint16_t attr = 0;
        f.write(reinterpret_cast<char*>(normal), sizeof(normal));
        f.write(reinterpret_cast<char*>(verts), sizeof(verts));
        f.write(reinterpret_cast<char*>(&attr), sizeof(attr));
    }
    Mesh mesh = read_stl(path);
    REQUIRE(mesh.triangles.size() == 1);
    REQUIRE(mesh.vertices.size() == 3);
    REQUIRE(mesh.vertices[1].x == Catch::Approx(1.0));
    std::remove(path.c_str());
}

TEST_CASE("read_obj: tetrahedron fixture (shared vertices)", "[mesh]") {
    Mesh mesh = read_obj(fixture("tetrahedron.obj"));
    check_is_unit_tetrahedron(mesh, 4);
}

TEST_CASE("read_ply: ASCII tetrahedron fixture", "[mesh]") {
    Mesh mesh = read_ply(fixture("tetrahedron.ply"));
    check_is_unit_tetrahedron(mesh, 4);
}

TEST_CASE("read_off: tetrahedron fixture", "[mesh]") {
    Mesh mesh = read_off(fixture("tetrahedron.off"));
    check_is_unit_tetrahedron(mesh, 4);
}

TEST_CASE("load_mesh: dispatches by extension", "[mesh]") {
    REQUIRE(load_mesh(fixture("tetrahedron.stl")).triangles.size() == 4);
    REQUIRE(load_mesh(fixture("tetrahedron.obj")).triangles.size() == 4);
    REQUIRE(load_mesh(fixture("tetrahedron.ply")).triangles.size() == 4);
    REQUIRE(load_mesh(fixture("tetrahedron.off")).triangles.size() == 4);
    REQUIRE_THROWS(load_mesh(fixture("tetrahedron.unknownext")));
}
