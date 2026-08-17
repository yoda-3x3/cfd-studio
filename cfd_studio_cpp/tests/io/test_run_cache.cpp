#include <catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>

#include "io/run_cache.hpp"

using namespace cfd::io;
using namespace cfd::mesh;
namespace fs = std::filesystem;

namespace {
Mesh make_tetrahedron(double scale = 1.0) {
    Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {scale, 0, 0}, {0, scale, 0}, {0, 0, scale}};
    mesh.triangles = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
    return mesh;
}

std::string temp_cache_root(const std::string& name) {
    auto dir = fs::temp_directory_path() / ("cfd_cache_test_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir.string();
}
} // namespace

TEST_CASE("mesh_signature: identical meshes hash identically, different meshes differ", "[io][run_cache]") {
    Mesh a = make_tetrahedron(1.0);
    Mesh b = make_tetrahedron(1.0);
    Mesh c = make_tetrahedron(2.0);

    REQUIRE(mesh_signature(a) == mesh_signature(b));
    REQUIRE(mesh_signature(a) != mesh_signature(c));
}

TEST_CASE("geometry_key / run_key: deterministic and sensitive to their inputs", "[io][run_cache]") {
    std::string sig = mesh_signature(make_tetrahedron());
    std::string k1 = geometry_key(sig, 10, 10, 10, 1.5, 4.0, 1.5, "external");
    std::string k2 = geometry_key(sig, 10, 10, 10, 1.5, 4.0, 1.5, "external");
    std::string k3 = geometry_key(sig, 12, 10, 10, 1.5, 4.0, 1.5, "external"); // different nx
    std::string k4 = geometry_key(sig, 10, 10, 10, 1.5, 4.0, 1.5, "internal"); // different domain_mode

    REQUIRE(k1 == k2);
    REQUIRE(k1 != k3);
    REQUIRE(k1 != k4);

    // ground_effect/altitude_gap, defaulted (false/1.5), must reproduce
    // today's exact key for every pre-existing call -- no stale-cache
    // invalidation for callers that don't know about this option yet.
    std::string k1_explicit_default = geometry_key(sig, 10, 10, 10, 1.5, 4.0, 1.5, "external", false, 1.5);
    REQUIRE(k1_explicit_default == k1);

    std::string k5 = geometry_key(sig, 10, 10, 10, 1.5, 4.0, 1.5, "external", true, 1.5);
    std::string k6 = geometry_key(sig, 10, 10, 10, 1.5, 4.0, 1.5, "external", true, 2.0);
    REQUIRE(k5 != k1); // ground_effect changes the key
    REQUIRE(k6 != k5); // altitude_gap changes the key even with ground_effect held fixed

    std::string r1 = run_key(k1, 100.0, 1.0, 1000, 20);
    std::string r2 = run_key(k1, 100.0, 1.0, 1000, 20);
    std::string r3 = run_key(k1, 200.0, 1.0, 1000, 20); // different Re
    REQUIRE(r1 == r2);
    REQUIRE(r1 != r3);
}

TEST_CASE("RunCache: geometry cache round-trips", "[io][run_cache]") {
    RunCache cache(temp_cache_root("geometry_roundtrip"));

    GeometryCacheEntry entry;
    entry.nx = 4; entry.ny = 3; entry.nz = 2;
    entry.solid = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0}; // 4*3*2 = 24 entries
    entry.Lx = 1.5; entry.Ly = 2.5; entry.Lz = 3.5;

    REQUIRE_FALSE(cache.load_geometry("missing_key").has_value());

    cache.save_geometry("mykey", entry);
    auto loaded = cache.load_geometry("mykey");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->nx == 4);
    REQUIRE(loaded->ny == 3);
    REQUIRE(loaded->nz == 2);
    REQUIRE(loaded->solid == entry.solid);
    REQUIRE(loaded->Lx == Catch::Approx(1.5));
    REQUIRE(loaded->Ly == Catch::Approx(2.5));
    REQUIRE(loaded->Lz == Catch::Approx(3.5));
}

TEST_CASE("RunCache: run cache round-trips and self-heals against deleted output", "[io][run_cache]") {
    std::string root = temp_cache_root("run_roundtrip");
    RunCache cache(root);

    REQUIRE_FALSE(cache.load_run("missing_key").has_value());

    // Point at a real file that exists, so load_run's is-file-still-there check passes.
    std::string output_path = (fs::path(root) / "fake_output.foam").string();
    { std::ofstream f(output_path); }

    cache.save_run("mykey", output_path, "mesh.stl");
    auto loaded = cache.load_run("mykey");
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == output_path);

    // Self-healing: if the referenced output file no longer exists, the
    // cache should report a miss rather than returning a dangling path.
    fs::remove(output_path);
    REQUIRE_FALSE(cache.load_run("mykey").has_value());
}

TEST_CASE("RunCache: run manifest accumulates multiple keys without clobbering each other", "[io][run_cache]") {
    std::string root = temp_cache_root("run_multi");
    RunCache cache(root);

    std::string path_a = (fs::path(root) / "a.foam").string();
    std::string path_b = (fs::path(root) / "b.foam").string();
    { std::ofstream f(path_a); }
    { std::ofstream f(path_b); }

    cache.save_run("key_a", path_a, "a.stl");
    cache.save_run("key_b", path_b, "b.stl");

    REQUIRE(*cache.load_run("key_a") == path_a);
    REQUIRE(*cache.load_run("key_b") == path_b);
}
