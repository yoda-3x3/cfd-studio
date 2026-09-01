#include "io/results_cache_writer.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "json.hpp"

namespace cfd::io {

namespace fs = std::filesystem;
using nlohmann::json;

ResultsCacheWriter::ResultsCacheWriter(
    std::string output_dir, int nx, int ny, int nz, double dx, double dy, double dz, std::string domain_mode)
    : output_dir_(std::move(output_dir)), nx_(nx), ny_(ny), nz_(nz), dx_(dx), dy_(dy), dz_(dz),
      domain_mode_(std::move(domain_mode)) {
    fs::create_directories(cache_dir());
}

std::string ResultsCacheWriter::cache_dir() const {
    return (fs::path(output_dir_) / "resultsCache").string();
}

std::string ResultsCacheWriter::manifest_path() const {
    return (fs::path(cache_dir()) / "manifest.json").string();
}

std::string ResultsCacheWriter::write_frame(
    int index, double time, const std::vector<double>& velocity_u, const std::vector<double>& velocity_v,
    const std::vector<double>& velocity_w, const std::vector<double>& velocity_magnitude,
    const std::vector<double>& pressure, const std::vector<float>& obstacle) {
    std::size_t n = static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_) * static_cast<std::size_t>(nz_);
    if (velocity_u.size() != n || velocity_v.size() != n || velocity_w.size() != n || velocity_magnitude.size() != n
        || pressure.size() != n || obstacle.size() != n) {
        throw std::invalid_argument("ResultsCacheWriter::write_frame: field size doesn't match nx*ny*nz");
    }

    char filename[32];
    std::snprintf(filename, sizeof(filename), "frame_%06d.bin", index);
    std::string path = (fs::path(cache_dir()) / filename).string();

    std::ofstream f(path, std::ios::binary);
    f.write("CFDR", 4);
    f.write(reinterpret_cast<const char*>(&time), sizeof(time));
    f.write(reinterpret_cast<const char*>(velocity_u.data()), static_cast<std::streamsize>(n * sizeof(double)));
    f.write(reinterpret_cast<const char*>(velocity_v.data()), static_cast<std::streamsize>(n * sizeof(double)));
    f.write(reinterpret_cast<const char*>(velocity_w.data()), static_cast<std::streamsize>(n * sizeof(double)));
    f.write(
        reinterpret_cast<const char*>(velocity_magnitude.data()), static_cast<std::streamsize>(n * sizeof(double)));
    f.write(reinterpret_cast<const char*>(pressure.data()), static_cast<std::streamsize>(n * sizeof(double)));
    f.write(reinterpret_cast<const char*>(obstacle.data()), static_cast<std::streamsize>(n * sizeof(float)));

    entries_.emplace_back(time, std::string(filename));
    write_manifest();
    return path;
}

void ResultsCacheWriter::write_manifest() const {
    json manifest;
    manifest["nx"] = nx_;
    manifest["ny"] = ny_;
    manifest["nz"] = nz_;
    manifest["dx"] = dx_;
    manifest["dy"] = dy_;
    manifest["dz"] = dz_;
    manifest["domain_mode"] = domain_mode_;

    json frames = json::array();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        json frame;
        frame["index"] = static_cast<int>(i);
        frame["time"] = entries_[i].first;
        frame["filename"] = entries_[i].second;
        frames.push_back(frame);
    }
    manifest["frames"] = frames;

    std::ofstream out(manifest_path());
    out << manifest.dump(2);
}

} // namespace cfd::io
