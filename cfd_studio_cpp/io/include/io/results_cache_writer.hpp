#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cfd::io {

// Writes a lightweight, purpose-built time series of full 3D field snapshots
// for the in-app results visualizer -- deliberately NOT a round-trip through
// the OpenFOAM case export (OpenFoamCaseWriter is write-only, and its point
// numbering is i-fastest, the opposite convention from Fields3D's own
// i-slowest/k-fastest layout everywhere else in this codebase). Frame files
// use Fields3D's native layout directly, so no reader-side conversion is
// ever needed.
//
// Layout, in a "resultsCache" subdirectory of the run's output_dir (a
// sibling of the OpenFOAM case's own constant/0/system dirs, named to avoid
// colliding with them):
//   manifest.json    -- grid dims/spacing/domain_mode + {index, time,
//                        filename} per frame, rewritten in full after every
//                        frame (not just at the end) so a stopped/crashed
//                        run still leaves a valid, openable manifest.
//   frame_NNNNNN.bin -- magic "CFDR" + double time, then velocity_u/v/w,
//                        velocity_magnitude, pressure as contiguous
//                        (nx*ny*nz) double arrays, then obstacle as a
//                        contiguous (nx*ny*nz) float array.
class ResultsCacheWriter {
public:
    ResultsCacheWriter(
        std::string output_dir, int nx, int ny, int nz, double dx, double dy, double dz, std::string domain_mode);

    // Writes one frame file for this step and rewrites manifest.json.
    // `velocity_u/v/w`, `velocity_magnitude`, `pressure` are each a
    // contiguous (nx*ny*nz) array in Fields3D's own i-slowest/k-fastest
    // layout; `obstacle` likewise. Returns the frame file's path.
    std::string write_frame(
        int index, double time, const std::vector<double>& velocity_u, const std::vector<double>& velocity_v,
        const std::vector<double>& velocity_w, const std::vector<double>& velocity_magnitude,
        const std::vector<double>& pressure, const std::vector<float>& obstacle);

    [[nodiscard]] std::string cache_dir() const;
    [[nodiscard]] std::string manifest_path() const;

private:
    void write_manifest() const;

    std::string output_dir_;
    int nx_, ny_, nz_;
    double dx_, dy_, dz_;
    std::string domain_mode_;
    std::vector<std::pair<double, std::string>> entries_; // (time, filename)
};

} // namespace cfd::io
