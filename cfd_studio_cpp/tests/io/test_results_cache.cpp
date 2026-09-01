#include <catch_amalgamated.hpp>

#include <filesystem>
#include <stdexcept>

#include "io/results_cache_reader.hpp"
#include "io/results_cache_writer.hpp"

using namespace cfd::io;
namespace fs = std::filesystem;

namespace {
std::string temp_output_dir(const std::string& name) {
    auto dir = fs::temp_directory_path() / ("cfd_results_cache_test_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir.string();
}

// A tiny 2x2x2 grid's worth of distinct, easily-checkable values.
struct SyntheticFrame {
    std::vector<double> u, v, w, mag, p;
    std::vector<float> obstacle;
};

SyntheticFrame make_synthetic_frame(double offset) {
    SyntheticFrame f;
    for (int i = 0; i < 8; ++i) {
        f.u.push_back(offset + i * 0.1);
        f.v.push_back(offset + i * 0.2);
        f.w.push_back(offset + i * 0.3);
        f.mag.push_back(offset + i * 0.4);
        f.p.push_back(offset + i * 0.5);
        f.obstacle.push_back(static_cast<float>(i % 2));
    }
    return f;
}
} // namespace

TEST_CASE("ResultsCacheWriter/Reader: round-trips dims, spacing, and per-frame field data exactly",
          "[io][results_cache]") {
    std::string output_dir = temp_output_dir("roundtrip");
    ResultsCacheWriter writer(output_dir, 2, 2, 2, 0.1, 0.2, 0.3, "external");

    SyntheticFrame f0 = make_synthetic_frame(0.0);
    SyntheticFrame f1 = make_synthetic_frame(100.0);
    writer.write_frame(0, 0.0, f0.u, f0.v, f0.w, f0.mag, f0.p, f0.obstacle);
    writer.write_frame(1, 0.05, f1.u, f1.v, f1.w, f1.mag, f1.p, f1.obstacle);

    ResultsCacheReader reader(writer.cache_dir());
    REQUIRE(reader.nx() == 2);
    REQUIRE(reader.ny() == 2);
    REQUIRE(reader.nz() == 2);
    REQUIRE(reader.dx() == Catch::Approx(0.1));
    REQUIRE(reader.dy() == Catch::Approx(0.2));
    REQUIRE(reader.dz() == Catch::Approx(0.3));
    REQUIRE(reader.domain_mode() == "external");
    REQUIRE(reader.frame_count() == 2);
    REQUIRE(reader.frame_time(0) == Catch::Approx(0.0));
    REQUIRE(reader.frame_time(1) == Catch::Approx(0.05));

    ResultsFrame loaded0 = reader.load_frame(0);
    REQUIRE(loaded0.time == Catch::Approx(0.0));
    REQUIRE(loaded0.velocity_u == f0.u);
    REQUIRE(loaded0.velocity_v == f0.v);
    REQUIRE(loaded0.velocity_w == f0.w);
    REQUIRE(loaded0.velocity_magnitude == f0.mag);
    REQUIRE(loaded0.pressure == f0.p);
    REQUIRE(loaded0.obstacle == f0.obstacle);

    ResultsFrame loaded1 = reader.load_frame(1);
    REQUIRE(loaded1.time == Catch::Approx(0.05));
    REQUIRE(loaded1.velocity_u == f1.u);
}

TEST_CASE("ResultsCacheWriter: rejects mismatched field sizes", "[io][results_cache]") {
    std::string output_dir = temp_output_dir("badsize");
    ResultsCacheWriter writer(output_dir, 2, 2, 2, 1.0, 1.0, 1.0, "external");
    std::vector<double> wrong_size(4, 0.0); // needs 8, not 4
    std::vector<float> obstacle(8, 0.0f);
    REQUIRE_THROWS_AS(
        writer.write_frame(0, 0.0, wrong_size, wrong_size, wrong_size, wrong_size, wrong_size, obstacle),
        std::invalid_argument);
}

TEST_CASE("ResultsCacheReader: throws on a missing manifest", "[io][results_cache]") {
    std::string dir = temp_output_dir("missing");
    REQUIRE_THROWS_AS(ResultsCacheReader(dir), std::runtime_error);
}

TEST_CASE("ResultsCacheReader: throws on an out-of-range frame index", "[io][results_cache]") {
    std::string output_dir = temp_output_dir("oob");
    ResultsCacheWriter writer(output_dir, 1, 1, 1, 1.0, 1.0, 1.0, "external");
    std::vector<double> one(1, 0.0);
    std::vector<float> one_f(1, 0.0f);
    writer.write_frame(0, 0.0, one, one, one, one, one, one_f);

    ResultsCacheReader reader(writer.cache_dir());
    REQUIRE_THROWS_AS(reader.load_frame(1), std::out_of_range);
    REQUIRE_THROWS_AS(reader.frame_time(-1), std::out_of_range);
}
