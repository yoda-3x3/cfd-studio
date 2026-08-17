#include "io/run_cache.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "core/hashing.hpp"
#include "json.hpp"

namespace cfd::io {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {
std::string format_g6(double x) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", x);
    return buf;
}
} // namespace

std::string mesh_signature(const cfd::mesh::Mesh& mesh) {
    std::vector<std::uint8_t> buf(
        mesh.vertices.size() * 3 * sizeof(double) + mesh.triangles.size() * 3 * sizeof(std::int64_t));
    std::size_t offset = 0;
    for (const auto& v : mesh.vertices) {
        double xyz[3] = {v.x, v.y, v.z};
        std::memcpy(buf.data() + offset, xyz, sizeof(xyz));
        offset += sizeof(xyz);
    }
    for (const auto& tri : mesh.triangles) {
        std::int64_t idx[3] = {tri[0], tri[1], tri[2]};
        std::memcpy(buf.data() + offset, idx, sizeof(idx));
        offset += sizeof(idx);
    }
    return cfd::core::hash_hex(buf.data(), buf.size(), 24);
}

std::string geometry_key(
    const std::string& mesh_sig, int nx, int ny, int nz,
    double inflow_gap, double wake_gap, double lateral_gap, const std::string& domain_mode,
    bool ground_effect, double altitude_gap) {
    std::string payload = mesh_sig + "|" + std::to_string(nx) + "|" + std::to_string(ny) + "|" + std::to_string(nz)
        + "|" + format_g6(inflow_gap) + "|" + format_g6(wake_gap) + "|" + format_g6(lateral_gap) + "|" + domain_mode
        + "|" + (ground_effect ? "1" : "0") + "|" + format_g6(altitude_gap);
    return cfd::core::hash_hex(payload.data(), payload.size(), 24);
}

std::string run_key(const std::string& geom_key, double Re, double U_in, int n_steps, int output_every) {
    std::string payload = geom_key + "|" + format_g6(Re) + "|" + format_g6(U_in) + "|"
        + std::to_string(n_steps) + "|" + std::to_string(output_every);
    return cfd::core::hash_hex(payload.data(), payload.size(), 24);
}

RunCache::RunCache(std::string cache_root) : cache_root_(std::move(cache_root)) {
    fs::create_directories(fs::path(cache_root_) / "geometry");
}

std::string RunCache::manifest_path() const {
    return (fs::path(cache_root_) / "run_manifest.json").string();
}

std::string RunCache::geometry_path(const std::string& geom_key) const {
    return (fs::path(cache_root_) / "geometry" / (geom_key + ".bin")).string();
}

std::optional<GeometryCacheEntry> RunCache::load_geometry(const std::string& geom_key) const {
    std::ifstream f(geometry_path(geom_key), std::ios::binary);
    if (!f) return std::nullopt;

    char magic[4];
    f.read(magic, 4);
    if (!f || std::memcmp(magic, "CFDG", 4) != 0) return std::nullopt;

    GeometryCacheEntry entry;
    std::int32_t nx = 0, ny = 0, nz = 0;
    f.read(reinterpret_cast<char*>(&nx), sizeof(nx));
    f.read(reinterpret_cast<char*>(&ny), sizeof(ny));
    f.read(reinterpret_cast<char*>(&nz), sizeof(nz));
    f.read(reinterpret_cast<char*>(&entry.Lx), sizeof(entry.Lx));
    f.read(reinterpret_cast<char*>(&entry.Ly), sizeof(entry.Ly));
    f.read(reinterpret_cast<char*>(&entry.Lz), sizeof(entry.Lz));
    if (!f) return std::nullopt;

    entry.nx = nx; entry.ny = ny; entry.nz = nz;
    entry.solid.resize(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz));
    f.read(reinterpret_cast<char*>(entry.solid.data()), static_cast<std::streamsize>(entry.solid.size()));
    if (!f) return std::nullopt; // treat a truncated/corrupt cache entry as a cache miss, not a crash

    return entry;
}

void RunCache::save_geometry(const std::string& geom_key, const GeometryCacheEntry& entry) const {
    std::ofstream f(geometry_path(geom_key), std::ios::binary);
    f.write("CFDG", 4);
    std::int32_t nx = entry.nx, ny = entry.ny, nz = entry.nz;
    f.write(reinterpret_cast<const char*>(&nx), sizeof(nx));
    f.write(reinterpret_cast<const char*>(&ny), sizeof(ny));
    f.write(reinterpret_cast<const char*>(&nz), sizeof(nz));
    f.write(reinterpret_cast<const char*>(&entry.Lx), sizeof(entry.Lx));
    f.write(reinterpret_cast<const char*>(&entry.Ly), sizeof(entry.Ly));
    f.write(reinterpret_cast<const char*>(&entry.Lz), sizeof(entry.Lz));
    f.write(reinterpret_cast<const char*>(entry.solid.data()), static_cast<std::streamsize>(entry.solid.size()));
}

std::optional<std::string> RunCache::load_run(const std::string& key) const {
    std::ifstream f(manifest_path());
    if (!f) return std::nullopt;
    json manifest;
    try {
        f >> manifest;
    } catch (const json::parse_error&) {
        return std::nullopt; // a corrupt manifest is a cache miss, not a crash
    }
    if (!manifest.contains(key)) return std::nullopt;
    if (!manifest[key].contains("output_path")) return std::nullopt;
    std::string output_path = manifest[key]["output_path"].get<std::string>();
    if (!output_path.empty() && fs::is_regular_file(output_path)) return output_path;
    return std::nullopt;
}

void RunCache::save_run(const std::string& key, const std::string& output_path, const std::string& mesh_name) const {
    json manifest = json::object();
    std::ifstream in(manifest_path());
    if (in) {
        try {
            in >> manifest;
        } catch (const json::parse_error&) {
            manifest = json::object();
        }
    }

    json entry;
    entry["output_path"] = output_path;
    entry["mesh_name"] = mesh_name;
    entry["created_at"] = static_cast<double>(std::time(nullptr));
    manifest[key] = entry;

    std::ofstream out(manifest_path());
    out << manifest.dump(2);
}

} // namespace cfd::io
