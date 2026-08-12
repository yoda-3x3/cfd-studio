#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cfd::io {

// Minimal-dependency writer for VTK XML ImageData (.vti) time steps plus a
// .pvd time-series collection file -- so results from the 2D solver can be
// opened directly in ParaView without needing the (heavy) `vtk` package.
// Port of io/vtk_writer.py's VTKSeriesWriter. 2D-only (paired with the 2D
// vorticity-streamfunction solver specifically): fields are
// velocity_u/v/velocity_magnitude/vorticity/streamfunction/obstacle, all
// row-major (ny,nx).
class VtkSeriesWriter {
public:
    explicit VtkSeriesWriter(std::string output_dir, std::string case_name = "case");

    // Writes one .vti file for this step and rewrites the .pvd collection
    // (a full rewrite each time, not an append -- matches the run-manifest
    // pattern in run_cache.hpp). Returns the .vti file's path.
    std::string write_step(
        int step_index, double time, int nx, int ny, double dx, double dy,
        const std::vector<double>& velocity_u, const std::vector<double>& velocity_v,
        const std::vector<double>& velocity_magnitude, const std::vector<double>& vorticity,
        const std::vector<double>& streamfunction, const std::vector<float>& obstacle);

    [[nodiscard]] std::string pvd_path() const;

    // Deletes every .vti file written so far plus the .pvd file, and
    // forgets them.
    void clear();

private:
    void write_pvd() const;

    std::string output_dir_;
    std::string case_name_;
    std::vector<std::pair<double, std::string>> entries_; // (time, filename)
};

} // namespace cfd::io
