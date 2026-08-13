#include <catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>

#include "foam_case_verify.hpp"
#include "io/openfoam_writer.hpp"

using namespace cfd::io;
using namespace cfd::mesh;
using cfd_test::verify_no_degenerate_faces;
using cfd_test::verify_owner_lt_neighbour;
using cfd_test::parse_face_points;
using cfd_test::read_foam_body_lines;
namespace fs = std::filesystem;

namespace {
Mesh make_box(double x0, double y0, double z0, double sx, double sy, double sz) {
    Mesh mesh;
    mesh.vertices = {
        {x0, y0, z0}, {x0 + sx, y0, z0}, {x0 + sx, y0 + sy, z0}, {x0, y0 + sy, z0},
        {x0, y0, z0 + sz}, {x0 + sx, y0, z0 + sz}, {x0 + sx, y0 + sy, z0 + sz}, {x0, y0 + sy, z0 + sz},
    };
    auto quad = [&](int a, int b, int c, int d) {
        mesh.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(c)});
        mesh.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(c), static_cast<std::uint32_t>(d)});
    };
    quad(0, 3, 2, 1); quad(4, 5, 6, 7);
    quad(0, 1, 5, 4); quad(3, 7, 6, 2);
    quad(0, 4, 7, 3); quad(1, 2, 6, 5);
    return mesh;
}

std::string temp_case_dir(const std::string& name) {
    auto dir = fs::temp_directory_path() / ("cfd_io_test_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir.string();
}
} // namespace

TEST_CASE("OpenFoamCaseWriter: empty domain (no obstacle) produces a structurally valid mesh", "[io][openfoam_writer]") {
    std::string dir = temp_case_dir("no_obstacle");
    int nx = 6, ny = 5, nz = 4;
    std::vector<std::uint8_t> solid(static_cast<std::size_t>(nx) * ny * nz, 0);

    OpenFoamCaseWriter writer(dir, nx, ny, nz, 0.1, 0.1, 0.1, solid);

    REQUIRE(writer.n_cells() == nx * ny * nz);
    verify_no_degenerate_faces(dir);
    verify_owner_lt_neighbour(dir);

    int n_boundary = 0;
    read_foam_body_lines((fs::path(dir) / "constant" / "polyMesh" / "boundary").string(), n_boundary);
    REQUIRE(n_boundary == 6); // inlet/outlet/4 lateral walls, no object patch (no solid cells)
}

TEST_CASE("OpenFoamCaseWriter: obstacle without a surface mesh produces flat quad object-patch faces", "[io][openfoam_writer]") {
    std::string dir = temp_case_dir("flat_obstacle");
    int nx = 10, ny = 8, nz = 6;
    std::vector<std::uint8_t> solid(static_cast<std::size_t>(nx) * ny * nz, 0);
    for (int i = 4; i <= 5; ++i)
        for (int j = 3; j <= 4; ++j)
            for (int k = 2; k <= 3; ++k)
                solid[static_cast<std::size_t>(i) * ny * nz + static_cast<std::size_t>(j) * nz + k] = 1;

    OpenFoamCaseWriter writer(dir, nx, ny, nz, 0.1, 0.1, 0.1, solid); // no surface_mesh

    REQUIRE(writer.n_cells() == nx * ny * nz - 8); // 2x2x2 solid block removed
    REQUIRE(writer.n_cells_smoothed() == 0);
    verify_no_degenerate_faces(dir);
    verify_owner_lt_neighbour(dir);

    // The boundary file has a hierarchical per-patch structure (not the
    // flat one-line-per-entry format read_foam_body_lines expects), so
    // just search its raw text directly for the object patch.
    std::ifstream bf((fs::path(dir) / "constant" / "polyMesh" / "boundary").string());
    std::string boundary_text((std::istreambuf_iterator<char>(bf)), std::istreambuf_iterator<char>());
    REQUIRE(boundary_text.find("object") != std::string::npos);

    // Every object-patch face should be a flat quad (4 points) since no
    // surface mesh was supplied to refine them.
    int n_faces = 0;
    auto face_lines = read_foam_body_lines((fs::path(dir) / "constant" / "polyMesh" / "faces").string(), n_faces);
    int quad_count = 0;
    for (const auto& line : face_lines) {
        if (parse_face_points(line).size() == 4) ++quad_count;
    }
    REQUIRE(quad_count > 0); // at least the object-patch faces are present as quads
}

TEST_CASE("OpenFoamCaseWriter: obstacle with a matching surface mesh refines the object patch", "[io][openfoam_writer]") {
    std::string dir = temp_case_dir("refined_obstacle");
    int nx = 10, ny = 8, nz = 6;
    double dx = 0.1, dy = 0.1, dz = 0.1;
    std::vector<std::uint8_t> solid(static_cast<std::size_t>(nx) * ny * nz, 0);
    for (int i = 4; i <= 5; ++i)
        for (int j = 3; j <= 4; ++j)
            for (int k = 2; k <= 3; ++k)
                solid[static_cast<std::size_t>(i) * ny * nz + static_cast<std::size_t>(j) * nz + k] = 1;

    // A box surface mesh roughly matching the solid block's own extent,
    // but deliberately offset by a fraction of a cell so its faces don't
    // lie exactly flush with the voxel cell boundaries -- clip_mesh_to_box
    // discards any clipped triangle flush against a boundary-cell's own
    // box plane (see its doc comment), so a mesh surface that coincides
    // exactly with the voxel grid would find nothing for every cell.
    Mesh box = make_box(4 * dx + 0.3 * dx, 3 * dy + 0.3 * dy, 2 * dz + 0.3 * dz, 2 * dx, 2 * dy, 2 * dz);

    OpenFoamCaseWriter writer(dir, nx, ny, nz, dx, dy, dz, solid, &box);

    REQUIRE(writer.n_cells_smoothed() > 0);
    verify_no_degenerate_faces(dir); // the exact regression this session's Python bug fix targeted
    verify_owner_lt_neighbour(dir);
}

TEST_CASE("OpenFoamCaseWriter::write_timestep writes internalField entries matching the fluid cell count", "[io][openfoam_writer]") {
    std::string dir = temp_case_dir("timestep");
    int nx = 5, ny = 4, nz = 3;
    std::vector<std::uint8_t> solid(static_cast<std::size_t>(nx) * ny * nz, 0);
    solid[0] = 1; // one solid cell, to confirm it's excluded from the field count

    OpenFoamCaseWriter writer(dir, nx, ny, nz, 0.1, 0.1, 0.1, solid);

    std::vector<double> u(solid.size(), 1.0), v(solid.size(), 0.0), w(solid.size(), 0.0), p(solid.size(), 0.5);
    writer.write_timestep(0.0, u.data(), v.data(), w.data(), p.data());

    int n_u = 0;
    read_foam_body_lines((fs::path(dir) / "0" / "U").string(), n_u);
    REQUIRE(n_u == writer.n_cells());
    REQUIRE(writer.n_cells() == nx * ny * nz - 1);
}
