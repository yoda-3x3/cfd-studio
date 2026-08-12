#include "io/vtk_writer.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cfd::io {

namespace fs = std::filesystem;

namespace {
std::string format_e(double x, int precision = 6) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*e", precision, x);
    return buf;
}

void write_vti(
    const std::string& filepath, int nx, int ny, double dx, double dy,
    const std::vector<std::pair<std::string, const std::vector<double>*>>& scalars,
    const std::string& vector_name, const std::vector<double>& u, const std::vector<double>& v) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\"?>\n";
    out << "<VTKFile type=\"ImageData\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    out << "  <ImageData WholeExtent=\"0 " << (nx - 1) << " 0 " << (ny - 1) << " 0 0\" "
        << "Origin=\"0 0 0\" Spacing=\"" << format_e(dx, 8) << " " << format_e(dy, 8) << " 1.0\">\n";
    out << "    <Piece Extent=\"0 " << (nx - 1) << " 0 " << (ny - 1) << " 0 0\">\n";
    std::string first_scalar = scalars.empty() ? "" : scalars.front().first;
    out << "      <PointData Scalars=\"" << first_scalar << "\" Vectors=\"" << vector_name << "\">\n";
    for (const auto& [name, arr] : scalars) {
        out << "        <DataArray type=\"Float32\" Name=\"" << name << "\" format=\"ascii\">\n";
        out << "          ";
        for (std::size_t i = 0; i < arr->size(); ++i) {
            if (i) out << " ";
            out << format_e((*arr)[i]);
        }
        out << "\n        </DataArray>\n";
    }
    out << "        <DataArray type=\"Float32\" Name=\"" << vector_name
        << "\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    out << "          ";
    for (std::size_t i = 0; i < u.size(); ++i) {
        if (i) out << " ";
        out << format_e(u[i]) << " " << format_e(v[i]) << " " << format_e(0.0);
    }
    out << "\n        </DataArray>\n";
    out << "      </PointData>\n";
    out << "      <CellData/>\n";
    out << "    </Piece>\n";
    out << "  </ImageData>\n";
    out << "</VTKFile>";

    std::ofstream f(filepath);
    f << out.str();
}
} // namespace

VtkSeriesWriter::VtkSeriesWriter(std::string output_dir, std::string case_name)
    : output_dir_(std::move(output_dir)), case_name_(std::move(case_name)) {
    fs::create_directories(output_dir_);
}

std::string VtkSeriesWriter::pvd_path() const {
    return (fs::path(output_dir_) / (case_name_ + ".pvd")).string();
}

std::string VtkSeriesWriter::write_step(
    int step_index, double time, int nx, int ny, double dx, double dy,
    const std::vector<double>& velocity_u, const std::vector<double>& velocity_v,
    const std::vector<double>& velocity_magnitude, const std::vector<double>& vorticity,
    const std::vector<double>& streamfunction, const std::vector<float>& obstacle) {
    char name_buf[64];
    std::snprintf(name_buf, sizeof(name_buf), "%s_%05d.vti", case_name_.c_str(), step_index);
    std::string fname = name_buf;
    std::string fpath = (fs::path(output_dir_) / fname).string();

    // obstacle is float32 already; the writer's scalar helper takes
    // double* arrays uniformly, so widen it once here rather than
    // templating write_vti for one field.
    std::vector<double> obstacle_d(obstacle.begin(), obstacle.end());

    std::vector<std::pair<std::string, const std::vector<double>*>> scalars = {
        {"velocity_magnitude", &velocity_magnitude},
        {"vorticity", &vorticity},
        {"streamfunction", &streamfunction},
        {"obstacle", &obstacle_d},
    };
    write_vti(fpath, nx, ny, dx, dy, scalars, "velocity", velocity_u, velocity_v);

    entries_.emplace_back(time, fname);
    write_pvd();
    return fpath;
}

void VtkSeriesWriter::write_pvd() const {
    std::ostringstream out;
    out << "<?xml version=\"1.0\"?>\n<VTKFile type=\"Collection\" version=\"0.1\">\n  <Collection>\n";
    for (const auto& [t, fname] : entries_) {
        out << "    <DataSet timestep=\"" << format_e(t, 8) << "\" group=\"\" part=\"0\" file=\"" << fname << "\"/>\n";
    }
    out << "  </Collection>\n</VTKFile>";

    std::ofstream f(pvd_path());
    f << out.str();
}

void VtkSeriesWriter::clear() {
    for (const auto& [t, fname] : entries_) {
        fs::path fpath = fs::path(output_dir_) / fname;
        std::error_code ec;
        fs::remove(fpath, ec);
    }
    std::error_code ec;
    fs::remove(pvd_path(), ec);
    entries_.clear();
}

} // namespace cfd::io
