#include "io/results_cache_reader.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "json.hpp"

namespace cfd::io {

namespace fs = std::filesystem;
using nlohmann::json;

ResultsCacheReader::ResultsCacheReader(std::string cache_dir) : cache_dir_(std::move(cache_dir)) {
    std::string manifest_path = (fs::path(cache_dir_) / "manifest.json").string();
    std::ifstream f(manifest_path);
    if (!f) throw std::runtime_error("ResultsCacheReader: manifest not found: " + manifest_path);

    json manifest;
    try {
        f >> manifest;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("ResultsCacheReader: corrupt manifest " + manifest_path + ": " + e.what());
    }

    nx_ = manifest.at("nx").get<int>();
    ny_ = manifest.at("ny").get<int>();
    nz_ = manifest.at("nz").get<int>();
    dx_ = manifest.at("dx").get<double>();
    dy_ = manifest.at("dy").get<double>();
    dz_ = manifest.at("dz").get<double>();
    domain_mode_ = manifest.at("domain_mode").get<std::string>();

    for (const auto& frame : manifest.at("frames")) {
        frame_files_.emplace_back(frame.at("time").get<double>(), frame.at("filename").get<std::string>());
    }
}

double ResultsCacheReader::frame_time(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= frame_files_.size()) {
        throw std::out_of_range("ResultsCacheReader::frame_time: index out of range");
    }
    return frame_files_[static_cast<std::size_t>(index)].first;
}

ResultsFrame ResultsCacheReader::load_frame(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= frame_files_.size()) {
        throw std::out_of_range("ResultsCacheReader::load_frame: index out of range");
    }
    std::string path = (fs::path(cache_dir_) / frame_files_[static_cast<std::size_t>(index)].second).string();
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("ResultsCacheReader: frame file not found: " + path);

    char magic[4];
    f.read(magic, 4);
    if (!f || std::memcmp(magic, "CFDR", 4) != 0) {
        throw std::runtime_error("ResultsCacheReader: bad magic in frame file: " + path);
    }

    ResultsFrame frame;
    f.read(reinterpret_cast<char*>(&frame.time), sizeof(frame.time));

    std::size_t n = static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_) * static_cast<std::size_t>(nz_);
    auto read_doubles = [&](std::vector<double>& dst) {
        dst.resize(n);
        f.read(reinterpret_cast<char*>(dst.data()), static_cast<std::streamsize>(n * sizeof(double)));
    };
    read_doubles(frame.velocity_u);
    read_doubles(frame.velocity_v);
    read_doubles(frame.velocity_w);
    read_doubles(frame.velocity_magnitude);
    read_doubles(frame.pressure);

    frame.obstacle.resize(n);
    f.read(reinterpret_cast<char*>(frame.obstacle.data()), static_cast<std::streamsize>(n * sizeof(float)));

    if (!f) throw std::runtime_error("ResultsCacheReader: truncated frame file: " + path);
    return frame;
}

} // namespace cfd::io
