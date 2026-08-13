#pragma once

// Shared structural-verification helpers for an OpenFoamCaseWriter output
// directory -- factored out of test_openfoam_writer.cpp so
// tests/pipeline/test_run_3d.cpp can reuse them instead of duplicating a
// second FoamFile parser. Sufficient for validating what this app's own
// writer produces, not a general-purpose OpenFOAM reader.

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace cfd_test {

inline bool is_plain_integer(const std::string& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Skips any preamble lines before the count (points/faces/owner/neighbour
// have none; U/p field files have "dimensions ...;" and "internalField
// nonuniform List<...>" lines first) until it finds a bare integer line,
// then reads that many data lines.
inline std::vector<std::string> read_foam_body_lines(const std::string& path, int& count) {
    std::ifstream f(path);
    REQUIRE(f.is_open());
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto pos = content.find("}\n\n");
    REQUIRE(pos != std::string::npos);
    std::istringstream iss(content.substr(pos + 3));
    std::vector<std::string> all_lines;
    std::string line;
    while (std::getline(iss, line)) all_lines.push_back(line);

    std::size_t idx = 0;
    while (idx < all_lines.size() && !is_plain_integer(all_lines[idx])) ++idx;
    REQUIRE(idx < all_lines.size());
    count = std::stoi(all_lines[idx]);
    ++idx; // count line
    ++idx; // "(" line
    REQUIRE(idx + static_cast<std::size_t>(count) <= all_lines.size());
    return std::vector<std::string>(all_lines.begin() + static_cast<std::ptrdiff_t>(idx),
                                     all_lines.begin() + static_cast<std::ptrdiff_t>(idx) + count);
}

inline std::vector<std::int64_t> parse_face_points(const std::string& line) {
    auto open = line.find('(');
    auto close = line.find(')');
    std::string inside = line.substr(open + 1, close - open - 1);
    std::istringstream iss(inside);
    std::vector<std::int64_t> pts;
    std::int64_t v;
    while (iss >> v) pts.push_back(v);
    return pts;
}

// Every face (regardless of which patch/internal list it came from) must
// have >= 3 distinct point indices -- the exact class of bug found and
// fixed in the Python app's writer this session (a degenerate face crashed
// ParaView's internalMesh view).
inline void verify_no_degenerate_faces(const std::string& case_dir) {
    int n_faces = 0;
    auto face_lines =
        read_foam_body_lines((std::filesystem::path(case_dir) / "constant" / "polyMesh" / "faces").string(), n_faces);
    for (const auto& line : face_lines) {
        auto pts = parse_face_points(line);
        std::set<std::int64_t> distinct(pts.begin(), pts.end());
        REQUIRE(distinct.size() >= 3);
    }
}

inline void verify_owner_lt_neighbour(const std::string& case_dir) {
    int n_owner = 0, n_neighbour = 0;
    auto owner_lines =
        read_foam_body_lines((std::filesystem::path(case_dir) / "constant" / "polyMesh" / "owner").string(), n_owner);
    auto neighbour_lines = read_foam_body_lines(
        (std::filesystem::path(case_dir) / "constant" / "polyMesh" / "neighbour").string(), n_neighbour);
    REQUIRE(n_neighbour <= n_owner);
    for (int i = 0; i < n_neighbour; ++i) {
        int owner = std::stoi(owner_lines[static_cast<std::size_t>(i)]);
        int neighbour = std::stoi(neighbour_lines[static_cast<std::size_t>(i)]);
        REQUIRE(owner < neighbour);
    }
}

} // namespace cfd_test
