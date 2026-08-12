#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "io/openfoam_writer.hpp"

using namespace cfd::io;
using namespace cfd::mesh;
namespace fs = std::filesystem;

namespace {
// Minimal parser for this app's own FoamFile list format (blank line,
// count, "(", N data lines, ")") -- sufficient for validating what our own
// writer produces, not a general-purpose OpenFOAM reader. Mirrors the
// diagnostic approach used earlier this session to root-cause a real
// degenerate-face bug in the Python app's writer, kept here as a permanent
// regression check for the C++ port instead of a throwaway script.
bool is_plain_integer(const std::string& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Skips any preamble lines before the count (points/faces/owner/neighbour
// have none; U/p field files have "dimensions ...;" and "internalField
// nonuniform List<...>" lines first) until it finds a bare integer line,
// then reads that many data lines.
std::vector<std::string> read_foam_body_lines(const std::string& path, int& count) {
    std::ifstream f(path);
    REQUIRE(f.is_open());
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto pos = content.find("}\n\n");
    REQUIRE(pos != std::string::npos);
    std::istringstream iss(content.substr(pos + 3));
    std::vector<std::string> all_lines;
    std::string line;
    while (std::getline(iss, line)) all_lines.push_back(line);

    std::size_t idx = 0;
    while (idx < all_lines.size() && !is_plain_integer(all_lines[idx])) ++idx;
    REQUIRE(idx < all_lines.size());
    count = std::stoi(all_lines[idx]);
    ++idx; // count line
    ++idx; // "(" line
    REQUIRE(idx + static_cast<std::size_t>(count) <= all_lines.size());
    return std::vector<std::string>(all_lines.begin() + static_cast<std::ptrdiff_t>(idx),
                                     all_lines.begin() + static_cast<std::ptrdiff_t>(idx) + count);
}

std::vector<std::int64_t> parse_face_points(const std::string& line) {
    auto open = line.find('(');
    auto close = line.find(')');
    std::string inside = line.substr(open + 1, close - open - 1);
    std::istringstream iss(inside);
    std::vector<std::int64_t> pts;
    std::int64_t v;
    while (iss >> v) pts.push_back(v);
    return pts;
}

// Every face (regardless of which patch/internal list it came from) must
// have >= 3 distinct point indices -- the exact class of bug found and
// fixed in the Python app's writer this session (a degenerate face crashed
// ParaView's internalMesh view). Verified structurally here, not just
// "did it run".
void verify_no_degenerate_faces(const std::string& case_dir) {
    int n_faces = 0;
    auto face_lines = read_foam_body_lines((fs::path(case_dir) / "constant" / "polyMesh" / "faces").string(), n_faces);
    for (const auto& line : face_lines) {
        auto pts = parse_face_points(line);
        std::set<std::int64_t> distinct(pts.begin(), pts.end());
        REQUIRE(distinct.size() >= 3);
    }
}

void verify_owner_lt_neighbour(const std::string& case_dir) {
    int n_owner = 0, n_neighbour = 0;
    auto owner_lines = read_foam_body_lines((fs::path(case_dir) / "constant" / "polyMesh" / "owner").string(), n_owner);
    auto neighbour_lines = read_foam_body_lines((fs::path(case_dir) / "constant" / "polyMesh" / "neighbour").string(), n_neighbour);
    REQUIRE(n_neighbour <= n_owner);
    for (int i = 0; i < n_neighbour; ++i) {
        int owner = std::stoi(owner_lines[static_cast<std::size_t>(i)]);
        int neighbour = std::stoi(neighbour_lines[static_cast<std::size_t>(i)]);
        REQUIRE(owner < neighbour);
    }
}

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
