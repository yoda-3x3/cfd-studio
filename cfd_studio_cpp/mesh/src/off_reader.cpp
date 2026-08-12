#include "mesh/off_reader.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cfd::mesh {

Mesh read_off(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("read_off: cannot open file: " + path);

    std::string magic;
    f >> magic;
    // Accept "OFF" and the "COFF"/"NOFF" variants (color/normal-annotated)
    // by just checking the required "OFF" suffix -- extra per-vertex color/
    // normal fields are handled below by only reading the first 3 numbers
    // per vertex line and letting the rest of that line's tokens be
    // skipped naturally by the next iteration's formatted reads... actually
    // COFF/NOFF put extra columns on the *same* line, so those are read
    // and discarded explicitly per vertex below via a full getline.
    if (magic.size() < 3 || magic.substr(magic.size() - 3) != "OFF") {
        throw std::runtime_error("read_off: missing OFF header: " + path);
    }

    long n_vertices, n_faces, n_edges;
    f >> n_vertices >> n_faces >> n_edges;
    if (!f || n_vertices < 0 || n_faces < 0) {
        throw std::runtime_error("read_off: malformed counts header: " + path);
    }

    Mesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(n_vertices));
    for (long i = 0; i < n_vertices; ++i) {
        double x, y, z;
        f >> x >> y >> z;
        if (!f) throw std::runtime_error("read_off: truncated vertex list: " + path);
        mesh.vertices.push_back({x, y, z});
        f.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // discard optional color/normal columns
    }

    for (long i = 0; i < n_faces; ++i) {
        long n;
        f >> n;
        if (!f || n < 3) throw std::runtime_error("read_off: malformed face record: " + path);
        std::vector<std::uint32_t> idx(static_cast<std::size_t>(n));
        for (long k = 0; k < n; ++k) {
            long v;
            f >> v;
            if (!f || v < 0 || v >= n_vertices) {
                throw std::runtime_error("read_off: face references out-of-range vertex index: " + path);
            }
            idx[static_cast<std::size_t>(k)] = static_cast<std::uint32_t>(v);
        }
        for (long k = 1; k + 1 < n; ++k) {
            mesh.triangles.push_back({idx[0], idx[static_cast<std::size_t>(k)], idx[static_cast<std::size_t>(k + 1)]});
        }
    }

    if (mesh.triangles.empty()) {
        throw std::runtime_error("read_off: no faces parsed: " + path);
    }
    return mesh;
}

} // namespace cfd::mesh
