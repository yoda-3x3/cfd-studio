#include "mesh/stl_writer.hpp"

#include <fstream>

namespace cfd::mesh {

void write_stl(const Mesh& mesh, const std::string& path) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("write_stl: cannot open file for writing: " + path);

    f << "solid cfd_mesh\n";
    for (const auto& tri : mesh.triangles) {
        const Vec3& a = mesh.vertices[tri[0]];
        const Vec3& b = mesh.vertices[tri[1]];
        const Vec3& c = mesh.vertices[tri[2]];
        Vec3 n = normalize(cross(b - a, c - a));
        f << "  facet normal " << n.x << " " << n.y << " " << n.z << "\n";
        f << "    outer loop\n";
        f << "      vertex " << a.x << " " << a.y << " " << a.z << "\n";
        f << "      vertex " << b.x << " " << b.y << " " << b.z << "\n";
        f << "      vertex " << c.x << " " << c.y << " " << c.z << "\n";
        f << "    endloop\n";
        f << "  endfacet\n";
    }
    f << "endsolid cfd_mesh\n";
}

} // namespace cfd::mesh
