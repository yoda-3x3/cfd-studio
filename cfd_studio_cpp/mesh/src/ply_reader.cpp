#include "mesh/ply_reader.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace cfd::mesh {

namespace {

enum class PlyFormat { Ascii, BinaryLittleEndian, BinaryBigEndian };

struct PlyProperty {
    std::string name;
    bool is_list = false;
    std::string count_type; // only meaningful if is_list
    std::string value_type;
};

struct PlyElement {
    std::string name;
    std::size_t count = 0;
    std::vector<PlyProperty> properties;
};

std::size_t type_size(const std::string& t) {
    if (t == "char" || t == "int8" || t == "uchar" || t == "uint8") return 1;
    if (t == "short" || t == "int16" || t == "ushort" || t == "uint16") return 2;
    if (t == "int" || t == "int32" || t == "uint" || t == "uint32" || t == "float" || t == "float32") return 4;
    if (t == "double" || t == "float64") return 8;
    throw std::runtime_error("read_ply: unrecognized property type: " + t);
}

bool is_signed_int_type(const std::string& t) {
    return t == "char" || t == "int8" || t == "short" || t == "int16" || t == "int" || t == "int32";
}
bool is_float_type(const std::string& t) {
    return t == "float" || t == "float32" || t == "double" || t == "float64";
}

double read_binary_scalar(std::ifstream& f, const std::string& type, bool big_endian) {
    std::size_t sz = type_size(type);
    unsigned char raw[8];
    f.read(reinterpret_cast<char*>(raw), static_cast<std::streamsize>(sz));
    if (!f) throw std::runtime_error("read_ply: truncated binary data");
    if (big_endian) std::reverse(raw, raw + sz);

    if (is_float_type(type)) {
        if (sz == 4) { float v; std::memcpy(&v, raw, 4); return static_cast<double>(v); }
        double v; std::memcpy(&v, raw, 8); return v;
    }
    if (is_signed_int_type(type)) {
        if (sz == 1) return static_cast<double>(static_cast<std::int8_t>(raw[0]));
        if (sz == 2) { std::int16_t v; std::memcpy(&v, raw, 2); return static_cast<double>(v); }
        std::int32_t v; std::memcpy(&v, raw, 4); return static_cast<double>(v);
    }
    // unsigned int types
    if (sz == 1) return static_cast<double>(raw[0]);
    if (sz == 2) { std::uint16_t v; std::memcpy(&v, raw, 2); return static_cast<double>(v); }
    std::uint32_t v; std::memcpy(&v, raw, 4); return static_cast<double>(v);
}

PlyFormat parse_header(std::ifstream& f, std::vector<PlyElement>& elements) {
    std::string line;
    std::getline(f, line);
    if (line.substr(0, 3) != "ply") throw std::runtime_error("read_ply: missing 'ply' magic header");

    PlyFormat format = PlyFormat::Ascii;
    bool format_seen = false;

    while (std::getline(f, line)) {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;

        if (tag == "format") {
            std::string fmt;
            ls >> fmt;
            if (fmt == "ascii") format = PlyFormat::Ascii;
            else if (fmt == "binary_little_endian") format = PlyFormat::BinaryLittleEndian;
            else if (fmt == "binary_big_endian") format = PlyFormat::BinaryBigEndian;
            else throw std::runtime_error("read_ply: unrecognized format: " + fmt);
            format_seen = true;
        } else if (tag == "element") {
            PlyElement el;
            ls >> el.name;
            long count;
            ls >> count;
            el.count = static_cast<std::size_t>(count);
            elements.push_back(std::move(el));
        } else if (tag == "property") {
            if (elements.empty()) throw std::runtime_error("read_ply: property before any element");
            std::string kind_or_type;
            ls >> kind_or_type;
            PlyProperty prop;
            if (kind_or_type == "list") {
                prop.is_list = true;
                ls >> prop.count_type >> prop.value_type >> prop.name;
            } else {
                prop.is_list = false;
                prop.value_type = kind_or_type;
                ls >> prop.name;
            }
            elements.back().properties.push_back(std::move(prop));
        } else if (tag == "end_header") {
            break;
        }
        // comment/obj_info/etc. lines are ignored.
    }

    if (!format_seen) throw std::runtime_error("read_ply: missing 'format' line");
    return format;
}

} // namespace

Mesh read_ply(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("read_ply: cannot open file: " + path);

    std::vector<PlyElement> elements;
    PlyFormat format = parse_header(f, elements);
    bool is_binary = format != PlyFormat::Ascii;
    bool big_endian = format == PlyFormat::BinaryBigEndian;

    Mesh mesh;

    // ASCII bodies are whitespace/newline-delimited regardless of the
    // header's declared per-property types, so one token stream handles
    // every element uniformly; binary bodies need type-aware reads to
    // advance the file position correctly, so the two paths are separate
    // per property below.
    std::istringstream ascii_stream;
    if (!is_binary) {
        std::ostringstream rest;
        rest << f.rdbuf();
        ascii_stream = std::istringstream(rest.str());
    }

    auto read_scalar = [&](const std::string& type) -> double {
        if (is_binary) return read_binary_scalar(f, type, big_endian);
        double v;
        ascii_stream >> v;
        return v;
    };

    for (const auto& el : elements) {
        bool is_vertex = el.name == "vertex";
        bool is_face = el.name == "face";

        // Locate x/y/z property positions for vertex elements up front.
        int x_idx = -1, y_idx = -1, z_idx = -1;
        int list_idx = -1;
        if (is_vertex) {
            for (std::size_t p = 0; p < el.properties.size(); ++p) {
                if (el.properties[p].name == "x") x_idx = static_cast<int>(p);
                else if (el.properties[p].name == "y") y_idx = static_cast<int>(p);
                else if (el.properties[p].name == "z") z_idx = static_cast<int>(p);
            }
            if (x_idx < 0 || y_idx < 0 || z_idx < 0) {
                throw std::runtime_error("read_ply: vertex element missing x/y/z property");
            }
        } else if (is_face) {
            for (std::size_t p = 0; p < el.properties.size(); ++p) {
                if (el.properties[p].is_list &&
                    (el.properties[p].name == "vertex_indices" || el.properties[p].name == "vertex_index")) {
                    list_idx = static_cast<int>(p);
                    break;
                }
            }
        }

        for (std::size_t row = 0; row < el.count; ++row) {
            double x = 0, y = 0, z = 0;
            std::vector<std::uint32_t> face_indices;

            for (std::size_t p = 0; p < el.properties.size(); ++p) {
                const auto& prop = el.properties[p];
                if (prop.is_list) {
                    long n = static_cast<long>(read_scalar(prop.count_type));
                    bool capture = is_face && static_cast<int>(p) == list_idx;
                    for (long i = 0; i < n; ++i) {
                        double v = read_scalar(prop.value_type);
                        if (capture) face_indices.push_back(static_cast<std::uint32_t>(v));
                    }
                } else {
                    double v = read_scalar(prop.value_type);
                    if (is_vertex) {
                        if (static_cast<int>(p) == x_idx) x = v;
                        else if (static_cast<int>(p) == y_idx) y = v;
                        else if (static_cast<int>(p) == z_idx) z = v;
                    }
                }
            }

            if (is_vertex) {
                mesh.vertices.push_back({x, y, z});
            } else if (is_face && !face_indices.empty()) {
                for (std::size_t i = 1; i + 1 < face_indices.size(); ++i) {
                    mesh.triangles.push_back({face_indices[0], face_indices[i], face_indices[i + 1]});
                }
            }
        }
    }

    if (mesh.triangles.empty()) {
        throw std::runtime_error("read_ply: no faces parsed: " + path);
    }
    return mesh;
}

} // namespace cfd::mesh
