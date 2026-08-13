#include <catch_amalgamated.hpp>

#include "solvers/materials.hpp"

using namespace cfd::solvers;

TEST_CASE("material_preset: matches solver3d/materials.py", "[solvers][materials]") {
    const auto& air = material_preset("air_20c");
    REQUIRE(air.rho == Catch::Approx(1.204));
    REQUIRE(air.nu == Catch::Approx(1.516e-5));

    const auto& water = material_preset("water_20c");
    REQUIRE(water.rho == Catch::Approx(998.2));
    REQUIRE(water.nu == Catch::Approx(1.004e-6));

    REQUIRE_THROWS(material_preset("not_a_material"));
    REQUIRE(material_preset_keys().size() == 2);
}

TEST_CASE("reynolds_number: matches solver3d/materials.py, including the nu<=0 guard", "[solvers][materials]") {
    REQUIRE(reynolds_number(2.0, 1.0, 1.516e-5) == Catch::Approx(2.0 / 1.516e-5));
    REQUIRE(reynolds_number(2.0, 1.0, 0.0) == 0.0);
    REQUIRE(reynolds_number(2.0, 1.0, -1.0) == 0.0);
}
