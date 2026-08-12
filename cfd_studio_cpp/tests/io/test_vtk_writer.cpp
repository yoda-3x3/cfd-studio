#include <catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "io/vtk_writer.hpp"

using namespace cfd::io;
namespace fs = std::filesystem;

namespace {
std::string temp_dir(const std::string& name) {
    auto dir = fs::temp_directory_path() / ("cfd_vtk_test_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir.string();
}

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE("VtkSeriesWriter: write_step produces a .vti file and updates the .pvd", "[io][vtk_writer]") {
    std::string dir = temp_dir("basic");
    VtkSeriesWriter writer(dir, "mycase");

    int nx = 4, ny = 3;
    std::size_t n = static_cast<std::size_t>(nx) * ny;
    std::vector<double> u(n, 1.0), v(n, 0.0), mag(n, 1.0), vort(n, 0.0), psi(n, 0.5);
    std::vector<float> obstacle(n, 0.0f);

    std::string vti_path = writer.write_step(0, 0.0, nx, ny, 0.1, 0.1, u, v, mag, vort, psi, obstacle);

    REQUIRE(fs::exists(vti_path));
    REQUIRE(fs::exists(writer.pvd_path()));

    std::string vti_content = read_file(vti_path);
    REQUIRE(vti_content.find("<VTKFile type=\"ImageData\"") != std::string::npos);
    REQUIRE(vti_content.find("velocity_magnitude") != std::string::npos);
    REQUIRE(vti_content.find("vorticity") != std::string::npos);
    REQUIRE(vti_content.find("streamfunction") != std::string::npos);
    REQUIRE(vti_content.find("obstacle") != std::string::npos);

    std::string pvd_content = read_file(writer.pvd_path());
    REQUIRE(pvd_content.find("mycase_00000.vti") != std::string::npos);
}

TEST_CASE("VtkSeriesWriter: multiple steps all appear in the .pvd collection", "[io][vtk_writer]") {
    std::string dir = temp_dir("multistep");
    VtkSeriesWriter writer(dir, "case");

    int nx = 3, ny = 3;
    std::size_t n = static_cast<std::size_t>(nx) * ny;
    std::vector<double> zeros(n, 0.0);
    std::vector<float> obstacle(n, 0.0f);

    for (int step = 0; step < 3; ++step) {
        writer.write_step(step, step * 0.1, nx, ny, 0.1, 0.1, zeros, zeros, zeros, zeros, zeros, obstacle);
    }

    std::string pvd_content = read_file(writer.pvd_path());
    REQUIRE(pvd_content.find("case_00000.vti") != std::string::npos);
    REQUIRE(pvd_content.find("case_00001.vti") != std::string::npos);
    REQUIRE(pvd_content.find("case_00002.vti") != std::string::npos);
}

TEST_CASE("VtkSeriesWriter::clear removes all written files", "[io][vtk_writer]") {
    std::string dir = temp_dir("clear");
    VtkSeriesWriter writer(dir, "case");

    int nx = 3, ny = 3;
    std::size_t n = static_cast<std::size_t>(nx) * ny;
    std::vector<double> zeros(n, 0.0);
    std::vector<float> obstacle(n, 0.0f);
    std::string vti_path = writer.write_step(0, 0.0, nx, ny, 0.1, 0.1, zeros, zeros, zeros, zeros, zeros, obstacle);

    REQUIRE(fs::exists(vti_path));
    writer.clear();
    REQUIRE_FALSE(fs::exists(vti_path));
    REQUIRE_FALSE(fs::exists(writer.pvd_path()));
}
