#include "mesh/mesh.hpp"

#include <algorithm>
#include <cctype>

#include "mesh/obj_reader.hpp"
#include "mesh/off_reader.hpp"
#include "mesh/ply_reader.hpp"
#include "mesh/stl_reader.hpp"

namespace cfd::mesh {

namespace {
std::string lowercase_extension(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}
} // namespace

Mesh load_mesh(const std::string& path) {
    std::string ext = lowercase_extension(path);
    if (ext == "stl") return read_stl(path);
    if (ext == "obj") return read_obj(path);
    if (ext == "ply") return read_ply(path);
    if (ext == "off") return read_off(path);
    throw std::runtime_error("load_mesh: unrecognized file extension (expected .stl/.obj/.ply/.off): " + path);
}

} // namespace cfd::mesh
