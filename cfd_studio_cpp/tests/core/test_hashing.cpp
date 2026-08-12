#include <catch_amalgamated.hpp>

#include "core/hashing.hpp"

using namespace cfd::core;

TEST_CASE("hashing: same bytes hash identically", "[core]") {
    const char* data = "cfd studio cache key material";
    auto h1 = hash_bytes(data, 30);
    auto h2 = hash_bytes(data, 30);
    REQUIRE(h1.hi == h2.hi);
    REQUIRE(h1.lo == h2.lo);
}

TEST_CASE("hashing: different bytes hash differently", "[core]") {
    auto h1 = hash_bytes("abc", 3);
    auto h2 = hash_bytes("abd", 3);
    REQUIRE((h1.hi != h2.hi || h1.lo != h2.lo));
}

TEST_CASE("hashing: to_hex truncates to the requested length", "[core]") {
    auto h = hash_bytes("abc", 3);
    REQUIRE(to_hex(h, 24).size() == 24);
    REQUIRE(to_hex(h, 16).size() == 16);
    REQUIRE(to_hex(h).substr(0, 16) == to_hex(h, 16));
}

TEST_CASE("hashing: hash_hex is the convenience composition", "[core]") {
    REQUIRE(hash_hex("abc", 3, 24) == to_hex(hash_bytes("abc", 3), 24));
}
