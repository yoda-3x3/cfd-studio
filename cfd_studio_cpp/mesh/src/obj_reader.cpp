#include "mesh/obj_reader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace cfd::mesh {

namespace {

// Parses one face-vertex token ("5", "5/2", "5/2/1", "5//1") into a 0-based
// vertex index, resolving OBJ's 1-based-or-negative-relative indexing
// against the vertex count parsed so far.
std::uint32_t parse_face_vertex_index(const std::string& token, std::size_t vertex_count) {
    std::string index_part = token.substr(0, token.find('/'));
    long idx = std::stol(index_part);
    if (idx > 0) return static_cast<std::uint32_t>(idx - 1);
    if (idx < 0) return static_cast<std::uint32_t>(static_cast<long>(vertex_count) + idx);
    throw std::runtime_error("read_obj: face index 0 is invalid (OBJ indices are 1-based)");
}

} // namespace

Mesh read_obj(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("read_obj: cannot open file: " + path);

    Mesh mesh;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;

        if (tag == "v") {
            double x, y, z;
            ls >> x >> y >> z;
            mesh.vertices.push_back({x, y, z});
        } else if (tag == "f") {
            std::vector<std::uint32_t> face_indices;
            std::string token;
            while (ls >> token) {
                face_indices.push_back(parse_face_vertex_index(token, mesh.vertices.size()));
            }
            for (auto idx : face_indices) {
                if (idx >= mesh.vertices.size()) {
                    throw std::runtime_error("read_obj: face references out-of-range vertex index in: " + path);
                }
            }
            // Fan-triangulate n-gons (n > 3) from the first vertex.
            for (std::size_t i = 1; i + 1 < face_indices.size(); ++i) {
                mesh.triangles.push_back({face_indices[0], face_indices[i], face_indices[i + 1]});
            }
        }
        // vt/vn/g/o/mtllib/usemtl/# comments/etc. are ignored -- geometry only.
    }

    if (mesh.triangles.empty()) {
        throw std::runtime_error("read_obj: no faces parsed: " + path);
    }
    return mesh;
}

} // namespace cfd::mesh
