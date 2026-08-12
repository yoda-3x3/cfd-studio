#include <catch_amalgamated.hpp>

#include "mesh/mesh.hpp"
#include "mesh/mesh_bvh.hpp"

using namespace cfd::mesh;

namespace {
// Unit-right-angle tetrahedron: (0,0,0),(1,0,0),(0,1,0),(0,0,1).
Mesh make_tetrahedron() {
    Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    mesh.triangles = {{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}};
    return mesh;
}
} // namespace

TEST_CASE("MeshBVH::contains: centroid of a closed mesh is inside", "[mesh]") {
    MeshBVH bvh(make_tetrahedron());
    REQUIRE(bvh.contains(Vec3{0.2, 0.2, 0.2}));
}

TEST_CASE("MeshBVH::contains: a point far outside the mesh is outside", "[mesh]") {
    MeshBVH bvh(make_tetrahedron());
    REQUIRE_FALSE(bvh.contains(Vec3{10.0, 10.0, 10.0}));
    REQUIRE_FALSE(bvh.contains(Vec3{-1.0, -1.0, -1.0}));
}

TEST_CASE("MeshBVH::nearest_hit: ray straight into a known face returns the expected point", "[mesh]") {
    MeshBVH bvh(make_tetrahedron());
    // A ray from above straight down at (0.2, 0.2, *) first crosses the
    // slanted face (1,2,3) -- plane x+y+z=1 -- at z = 1-0.2-0.2 = 0.6,
    // which is nearer to the ray origin than the z=0 base face behind it;
    // rtcIntersect1 returns only the nearest hit.
    auto hit = bvh.nearest_hit(Vec3{0.2, 0.2, 5.0}, Vec3{0.0, 0.0, -1.0});
    REQUIRE(hit.has_value());
    REQUIRE(hit->point.x == Catch::Approx(0.2).margin(1e-4));
    REQUIRE(hit->point.y == Catch::Approx(0.2).margin(1e-4));
    REQUIRE(hit->point.z == Catch::Approx(0.6).margin(1e-4));
}

TEST_CASE("MeshBVH::nearest_hit: a ray that misses the mesh entirely returns nullopt", "[mesh]") {
    MeshBVH bvh(make_tetrahedron());
    auto hit = bvh.nearest_hit(Vec3{10.0, 10.0, 10.0}, Vec3{1.0, 0.0, 0.0});
    REQUIRE_FALSE(hit.has_value());
}
