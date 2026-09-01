#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cfd::io {

// One loaded frame's full 3D field data, in Fields3D's own native
// i-slowest/k-fastest (nx,ny,nz) layout -- see results_cache_writer.hpp for
// the on-disk format this reads.
struct ResultsFrame {
    double time = 0.0;
    std::vector<double> velocity_u, velocity_v, velocity_w, velocity_magnitude, pressure;
    std::vector<float> obstacle;
};

// Reads a "resultsCache" directory written by ResultsCacheWriter. Throws
// std::runtime_error on a missing/corrupt/truncated manifest or frame file
// -- unlike io::RunCache (whose corrupt-entry-as-cache-miss handling exists
// because recomputation is always a valid fallback there), a results cache
// is read immediately after a run finishes writing it with no recompute
// fallback available, so a genuine read failure should surface as an error
// rather than be silently swallowed.
class ResultsCacheReader {
public:
    explicit ResultsCacheReader(std::string cache_dir);

    [[nodiscard]] int nx() const { return nx_; }
    [[nodiscard]] int ny() const { return ny_; }
    [[nodiscard]] int nz() const { return nz_; }
    [[nodiscard]] double dx() const { return dx_; }
    [[nodiscard]] double dy() const { return dy_; }
    [[nodiscard]] double dz() const { return dz_; }
    [[nodiscard]] const std::string& domain_mode() const { return domain_mode_; }

    [[nodiscard]] int frame_count() const { return static_cast<int>(frame_files_.size()); }
    [[nodiscard]] double frame_time(int index) const;

    [[nodiscard]] ResultsFrame load_frame(int index) const;

private:
    std::string cache_dir_;
    int nx_ = 0, ny_ = 0, nz_ = 0;
    double dx_ = 0.0, dy_ = 0.0, dz_ = 0.0;
    std::string domain_mode_;
    std::vector<std::pair<double, std::string>> frame_files_; // (time, filename), manifest order
};

} // namespace cfd::io
