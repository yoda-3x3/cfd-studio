#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mesh/mesh.hpp"

namespace cfd::io {

// Content-addressed signature of a mesh's actual vertex/face data (not its
// filename, so edits to a same-named file don't return a stale cached
// result). Port of solver3d/cache.py's mesh_signature -- xxHash instead of
// SHA-256 (collision-avoidance is the actual requirement here, not
// cryptographic strength; see core::hash_hex).
[[nodiscard]] std::string mesh_signature(const cfd::mesh::Mesh& mesh);

// Geometry cache key: mesh content + grid/domain settings, independent of
// Re/steps/etc (voxelization doesn't depend on those). Port of
// solver3d/cache.py's geometry_key.
[[nodiscard]] std::string geometry_key(
    const std::string& mesh_sig, int nx, int ny, int nz,
    double inflow_gap, double wake_gap, double lateral_gap, const std::string& domain_mode = "external");

// Run cache key: geometry key + physics/run settings. Port of
// solver3d/cache.py's run_key.
[[nodiscard]] std::string run_key(const std::string& geom_key, double Re, double U_in, int n_steps, int output_every);

struct GeometryCacheEntry {
    std::vector<std::uint8_t> solid; // row-major (nx,ny,nz)
    int nx = 0, ny = 0, nz = 0;
    double Lx = 0.0, Ly = 0.0, Lz = 0.0;
};

// Two-tier disk cache so re-running a file already processed doesn't redo
// work already done -- port of solver3d/cache.py.
//   1. Geometry cache: a hit skips the mesh-containment voxelization query.
//   2. Run cache: a hit means an identical simulation has already been run
//      to completion -- skip the run entirely and reuse its output.
class RunCache {
public:
    // `cache_root` is the directory cache files live under (e.g.
    // "<project_root>/cache"); a "geometry" subdirectory and
    // "run_manifest.json" are created/read inside it.
    explicit RunCache(std::string cache_root);

    [[nodiscard]] std::optional<GeometryCacheEntry> load_geometry(const std::string& geom_key) const;
    void save_geometry(const std::string& geom_key, const GeometryCacheEntry& entry) const;

    // Returns the cached output path only if a manifest entry exists AND
    // the file it points to still exists on disk (self-healing against
    // manually deleted output directories).
    [[nodiscard]] std::optional<std::string> load_run(const std::string& key) const;
    void save_run(const std::string& key, const std::string& output_path, const std::string& mesh_name) const;

private:
    [[nodiscard]] std::string manifest_path() const;
    [[nodiscard]] std::string geometry_path(const std::string& geom_key) const;

    std::string cache_root_;
};

} // namespace cfd::io
