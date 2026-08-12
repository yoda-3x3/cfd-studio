#include <catch_amalgamated.hpp>

#include <cmath>

#include "solvers/voxelizer.hpp"

using namespace cfd::solvers;
using namespace cfd::mesh;

namespace {
// Axis-aligned box mesh, 12 triangles, outward winding.
Mesh make_box(double sx, double sy, double sz) {
    Mesh mesh;
    mesh.vertices = {
        {0, 0, 0}, {sx, 0, 0}, {sx, sy, 0}, {0, sy, 0},
        {0, 0, sz}, {sx, 0, sz}, {sx, sy, sz}, {0, sy, sz},
    };
    auto quad = [&](int a, int b, int c, int d) {
        mesh.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(c)});
        mesh.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(c), static_cast<std::uint32_t>(d)});
    };
    quad(0, 3, 2, 1); // bottom (z=0), outward = -z
    quad(4, 5, 6, 7); // top (z=sz), outward = +z
    quad(0, 1, 5, 4); // y=0
    quad(3, 7, 6, 2); // y=sy
    quad(0, 4, 7, 3); // x=0
    quad(1, 2, 6, 5); // x=sx
    return mesh;
}

// Hollow square tube (duct), a thin-walled shell open at both x-ends --
// outer wall only (no inner wall, no end caps), exercising the "mesh
// directly bounds the lumen" interpretation once capped.
//
// `domain_side` is the Ly=Lz the caller intends to voxelize against;
// voxelize_internal_to_grid samples the domain as [0,Lx]x[0,Ly]x[0,Lz], so
// the mesh must be positioned in that same non-negative frame (centered
// within it), not centered at the origin.
Mesh make_open_square_tube(double length, double side, double domain_side) {
    Mesh mesh;
    double h = side / 2;
    double c = domain_side / 2;
    mesh.vertices = {
        {0, c - h, c - h}, {0, c + h, c - h}, {0, c + h, c + h}, {0, c - h, c + h},
        {length, c - h, c - h}, {length, c + h, c - h}, {length, c + h, c + h}, {length, c - h, c + h},
    };
    auto quad = [&](int a, int b, int c, int d) {
        mesh.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(c)});
        mesh.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(c), static_cast<std::uint32_t>(d)});
    };
    quad(0, 4, 5, 1);
    quad(1, 5, 6, 2);
    quad(2, 6, 7, 3);
    quad(3, 7, 4, 0);
    return mesh;
}

// Double-walled square tube: an outer wall AND an inner wall (both capped
// at the ends into one watertight shell bounding the wall MATERIAL only,
// not the lumen) -- exercises the flood-fill "enclosed cavity" path
// directly, without needing cap_open_boundaries first.
//
// `domain_side` is the Ly=Lz the caller intends to voxelize against, kept
// strictly larger than outer_side so there's genuine "true exterior" space
// around the outer wall for the flood-fill boundary-touching test to
// distinguish from the enclosed lumen cavity.
Mesh make_walled_square_tube(double length, double outer_side, double inner_side, double domain_side) {
    Mesh mesh;
    double ho = outer_side / 2, hi = inner_side / 2;
    double c = domain_side / 2;
    // Outer ring x=0 (0-3), outer ring x=length (4-7),
    // inner ring x=0 (8-11), inner ring x=length (12-15).
    mesh.vertices = {
        {0, c - ho, c - ho}, {0, c + ho, c - ho}, {0, c + ho, c + ho}, {0, c - ho, c + ho},
        {length, c - ho, c - ho}, {length, c + ho, c - ho}, {length, c + ho, c + ho}, {length, c - ho, c + ho},
        {0, c - hi, c - hi}, {0, c + hi, c - hi}, {0, c + hi, c + hi}, {0, c - hi, c + hi},
        {length, c - hi, c - hi}, {length, c + hi, c - hi}, {length, c + hi, c + hi}, {length, c - hi, c + hi},
    };
    auto quad = [&](int a, int b, int c, int d) {
        mesh.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(c)});
        mesh.triangles.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(c), static_cast<std::uint32_t>(d)});
    };
    // Outer wall, outward-facing.
    quad(0, 4, 5, 1); quad(1, 5, 6, 2); quad(2, 6, 7, 3); quad(3, 7, 4, 0);
    // Inner wall, facing inward toward the lumen (reversed winding vs outer).
    quad(8, 9, 13, 12); quad(9, 10, 14, 13); quad(10, 11, 15, 14); quad(11, 8, 12, 15);
    // End caps: annulus between outer and inner ring at each x-end.
    quad(0, 1, 9, 8); quad(1, 2, 10, 9); quad(2, 3, 11, 10); quad(3, 0, 8, 11);
    quad(5, 4, 12, 13); quad(6, 5, 13, 14); quad(7, 6, 14, 15); quad(4, 7, 15, 12);
    return mesh;
}
} // namespace

TEST_CASE("prepare_geometry: centers and scales the mesh, sizes the tunnel domain", "[solvers][voxelizer]") {
    Mesh box = make_box(2.0, 1.0, 1.0); // extents (2,1,1), max=2
    auto geo = prepare_geometry(box, 1.0, 1.5, 4.0, 1.5);

    auto b = geo.mesh.bounds();
    // Normalized to target_max_extent=1.0 -> extents become (1, 0.5, 0.5).
    REQUIRE(b.extents().x == Catch::Approx(1.0).margin(1e-9));
    REQUIRE(b.extents().y == Catch::Approx(0.5).margin(1e-9));
    REQUIRE(b.extents().z == Catch::Approx(0.5).margin(1e-9));

    double L = 1.0; // max extent after normalization
    REQUIRE(geo.Lx == Catch::Approx(1.5 * L + 1.0 + 4.0 * L));
    REQUIRE(geo.Ly == Catch::Approx(0.5 + 2 * 1.5 * L));
    REQUIRE(geo.Lz == Catch::Approx(0.5 + 2 * 1.5 * L));
    REQUIRE(geo.is_watertight);
    REQUIRE(geo.original_extents.x == Catch::Approx(2.0));
}

TEST_CASE("prepare_internal_geometry: no lateral clearance, grid hugs the pipe", "[solvers][voxelizer]") {
    Mesh tube = make_open_square_tube(4.0, 1.0, 1.0);
    auto geo = prepare_internal_geometry(tube, 1.0, 0.0, 0.0);

    auto b = geo.mesh.bounds();
    REQUIRE(geo.Ly == Catch::Approx(b.extents().y).margin(1e-9));
    REQUIRE(geo.Lz == Catch::Approx(b.extents().z).margin(1e-9));
    REQUIRE_FALSE(geo.is_watertight); // open tube, not capped by prepare_internal_geometry itself
}

TEST_CASE("voxelize_to_grid: a box mesh voxelizes to a solid block matching its footprint", "[solvers][voxelizer]") {
    Mesh box = make_box(1.0, 1.0, 1.0);
    int n = 10;
    auto mask = voxelize_to_grid(box, n, n, n, 1.0, 1.0, 1.0);

    // Center cell should be solid, a cell clearly outside the box (can't
    // happen here since the domain exactly matches the box) -- instead
    // check corners near the box edges are handled without crashing and
    // the total solid count is close to the full grid (box fills the
    // entire domain almost exactly).
    std::size_t solid_count = 0;
    for (auto v : mask) solid_count += v;
    REQUIRE(solid_count > static_cast<std::size_t>(n * n * n) / 2);
    REQUIRE(mask[static_cast<std::size_t>(n / 2) * n * n + static_cast<std::size_t>(n / 2) * n + static_cast<std::size_t>(n / 2)] == 1);
}

TEST_CASE("voxelize_internal_to_grid: double-walled tube resolves via flood-fill (interpretation a)", "[solvers][voxelizer]") {
    // domain_side (1.4) is kept strictly larger than outer_side (1.0) so
    // there's genuine margin around the outer wall -- otherwise the wall
    // would coincide with the domain edge and there'd be no "true exterior"
    // for the flood fill to distinguish from the enclosed lumen cavity.
    double domain_side = 1.4;
    Mesh tube = make_walled_square_tube(4.0, 1.0, 0.6, domain_side);
    int nx = 20, ny = 14, nz = 14; // ny=nz=14 over domain_side=1.4 -> dy=dz=0.1 exactly
    auto solid = voxelize_internal_to_grid(tube, nx, ny, nz, 4.0, domain_side, domain_side);

    auto flat = [&](int i, int j, int k) { return static_cast<std::size_t>(i) * ny * nz + static_cast<std::size_t>(j) * nz + k; };
    int cx = nx / 2;
    // Lumen center (y=z=0.7, domain center) should be fluid.
    REQUIRE(solid[flat(cx, nz / 2, nz / 2)] == 0);
    // y=0.25 (cell index 2, since dy=0.1) sits inside the outer wall band
    // [0.2, 0.4) -- should be solid (wall material).
    REQUIRE(solid[flat(cx, 2, nz / 2)] == 1);
    // y=0.05 (cell index 0) sits in the margin outside the outer wall
    // entirely -- should also be solid (true exterior, per interpretation a).
    REQUIRE(solid[flat(cx, 0, nz / 2)] == 1);
}

TEST_CASE("voxelize_internal_to_grid: single-walled tube resolves via direct-inversion fallback (interpretation b)", "[solvers][voxelizer]") {
    double domain_side = 1.0; // mesh directly bounds the lumen -- no margin needed
    Mesh tube = make_open_square_tube(4.0, 1.0, domain_side);
    int nx = 20, ny = 10, nz = 10;
    auto solid = voxelize_internal_to_grid(tube, nx, ny, nz, 4.0, domain_side, domain_side);

    int cx = nx / 2, cy = ny / 2, cz = nz / 2;
    auto flat = [&](int i, int j, int k) { return static_cast<std::size_t>(i) * ny * nz + static_cast<std::size_t>(j) * nz + k; };
    REQUIRE(solid[flat(cx, cy, cz)] == 0); // lumen center is fluid
}
