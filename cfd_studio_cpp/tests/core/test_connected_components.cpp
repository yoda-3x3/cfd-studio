#include <catch_amalgamated.hpp>

#include "core/connected_components.hpp"

using cfd::core::label_components_6connectivity;

TEST_CASE("connected_components: single foreground block is one component", "[core]") {
    // 3x3x3 grid, all foreground.
    std::vector<std::uint8_t> mask(27, 1);
    auto result = label_components_6connectivity(mask, 3, 3, 3);
    REQUIRE(result.num_components == 1);
    for (auto l : result.labels) REQUIRE(l == 1);
}

TEST_CASE("connected_components: two diagonally-separated cells are two components", "[core]") {
    // 2x2x2 grid: only (0,0,0) and (1,1,1) are foreground -- not
    // 6-connected (only face-adjacency counts, not corner-adjacency).
    std::vector<std::uint8_t> mask(8, 0);
    mask[0] = 1;                       // (0,0,0)
    mask[1 * 2 * 2 + 1 * 2 + 1] = 1;   // (1,1,1)
    auto result = label_components_6connectivity(mask, 2, 2, 2);
    REQUIRE(result.num_components == 2);
}

TEST_CASE("connected_components: all-background grid has zero components", "[core]") {
    std::vector<std::uint8_t> mask(8, 0);
    auto result = label_components_6connectivity(mask, 2, 2, 2);
    REQUIRE(result.num_components == 0);
    for (auto l : result.labels) REQUIRE(l == 0);
}

TEST_CASE("connected_components: two face-adjacent cells merge into one component", "[core]") {
    // 2x1x1 grid, both foreground -- face-adjacent along x, must merge.
    std::vector<std::uint8_t> mask = {1, 1};
    auto result = label_components_6connectivity(mask, 2, 1, 1);
    REQUIRE(result.num_components == 1);
    REQUIRE(result.labels[0] == result.labels[1]);
}
