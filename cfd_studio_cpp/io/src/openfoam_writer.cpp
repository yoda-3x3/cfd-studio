#include "io/openfoam_writer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

#include "mesh/clip.hpp"

namespace cfd::io {

namespace fs = std::filesystem;
using cfd::mesh::Mesh;
using cfd::mesh::RawTriangle;
using cfd::mesh::Vec3;

namespace {
constexpr int OUTSIDE = 0, SOLID = 1, FLUID = 2;

void write_foam_file(const std::string& path, const std::string& cls, const std::string& obj, const std::string& body) {
    std::ofstream f(path);
    f << "FoamFile\n{\n";
    f << "    version     2.0;\n";
    f << "    format      ascii;\n";
    f << "    class       " << cls << ";\n";
    f << "    object      " << obj << ";\n";
    f << "}\n\n";
    f << body;
}

std::string format_g(double x, int precision) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*g", precision, x);
    return buf;
}

std::string format_e(double x, int precision) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*e", precision, x);
    return buf;
}

inline std::size_t cell_flat(int i, int j, int k, int ny, int nz) {
    return static_cast<std::size_t>(i) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz)
         + static_cast<std::size_t>(j) * static_cast<std::size_t>(nz)
         + static_cast<std::size_t>(k);
}
} // namespace

OpenFoamCaseWriter::OpenFoamCaseWriter(
    const std::string& case_dir, int nx, int ny, int nz, double dx, double dy, double dz,
    const std::vector<std::uint8_t>& solid_mask,
    const Mesh* surface_mesh, const std::string& domain_mode)
    : case_dir_(case_dir), domain_mode_(domain_mode), nx_(nx), ny_(ny), nz_(nz), dx_(dx), dy_(dy), dz_(dz),
      surface_mesh_(surface_mesh) {
    // A writer is only ever constructed for a genuinely fresh run (run_3d's
    // cache-hit path returns before reaching here), so anything already at
    // case_dir_ is stale -- most importantly, numbered timestep directories
    // from a *previous* run at a different grid resolution, whose field
    // files carry a different cell count than the mesh this writer is
    // about to build. Left in place, ParaView tries to reconcile
    // mismatched-size field data across the old and new timesteps, which
    // is a real bug (not just a "big mesh is slow" case) -- reusing an
    // output directory across two differently-configured runs is an
    // entirely normal thing for a user to do (e.g. re-running at a
    // coarser grid for a faster preview). Wipe first, matching
    // VtkSeriesWriter::clear()'s equivalent convention for the 2D case.
    std::error_code ec;
    fs::remove_all(case_dir_, ec);

    fs::create_directories(case_dir_);
    fs::create_directories(fs::path(case_dir_) / "constant" / "polyMesh");
    fs::create_directories(fs::path(case_dir_) / "system");

    build_topology(solid_mask);
    write_points();
    write_faces_owner_neighbour_boundary();
    write_system_stubs();
    write_foam_placeholder();
}

std::int64_t OpenFoamCaseWriter::point_id(int i, int j, int k) const {
    // OpenFOAM's own point-numbering convention: i is FASTEST-varying --
    // deliberately the opposite of core::idx3's i-slowest convention used
    // elsewhere in this codebase for solver grids. Port of
    // io/openfoam_writer.py's _point_id.
    return static_cast<std::int64_t>(i)
         + static_cast<std::int64_t>(j) * (nx_ + 1)
         + static_cast<std::int64_t>(k) * (nx_ + 1) * (ny_ + 1);
}

void OpenFoamCaseWriter::build_topology(const std::vector<std::uint8_t>& solid_mask) {
    int nx = nx_, ny = ny_, nz = nz_;

    std::vector<std::uint8_t> code(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz));
    for (std::size_t idx = 0; idx < code.size(); ++idx) code[idx] = solid_mask[idx] ? SOLID : FLUID;

    auto get_code = [&](int i, int j, int k) -> int {
        if (i < 0 || i >= nx || j < 0 || j >= ny || k < 0 || k >= nz) return OUTSIDE;
        return code[cell_flat(i, j, k, ny, nz)];
    };

    cell_id_.assign(code.size(), -1);
    cell_ijk_.clear();
    int next_cell = 0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                if (code[cell_flat(i, j, k, ny, nz)] == FLUID) {
                    cell_id_[cell_flat(i, j, k, ny, nz)] = next_cell;
                    cell_ijk_.push_back({i, j, k});
                    ++next_cell;
                }
            }
        }
    }
    n_cells_ = next_cell;

    auto get_cell_id = [&](int i, int j, int k) { return cell_id_[cell_flat(i, j, k, ny, nz)]; };

    struct InternalFace { int owner, neighbour; std::array<std::int64_t, 4> pts; };
    std::vector<InternalFace> internal_faces;

    const std::vector<std::string> patch_order = {
        "inlet", "outlet", "lowerWall", "upperWall", "frontWall", "backWall", "object",
    };
    std::unordered_map<std::string, std::vector<std::pair<int, std::vector<std::int64_t>>>> boundary_faces;
    for (const auto& name : patch_order) boundary_faces[name] = {};

    auto reversed = [](const std::array<std::int64_t, 4>& pts) {
        return std::vector<std::int64_t>(pts.rbegin(), pts.rend());
    };
    auto forward = [](const std::array<std::int64_t, 4>& pts) {
        return std::vector<std::int64_t>(pts.begin(), pts.end());
    };

    // ---- X-direction faces ----
    for (int i = 0; i <= nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                int m = get_code(i - 1, j, k), p = get_code(i, j, k);
                std::array<std::int64_t, 4> pts = {
                    point_id(i, j, k), point_id(i, j + 1, k), point_id(i, j + 1, k + 1), point_id(i, j, k + 1),
                }; // verified normal points in +x, matching io/openfoam_writer.py
                if (m == FLUID && p == FLUID) {
                    internal_faces.push_back({get_cell_id(i - 1, j, k), get_cell_id(i, j, k), pts});
                } else if (m == FLUID && p == OUTSIDE) {
                    boundary_faces["outlet"].push_back({get_cell_id(i - 1, j, k), forward(pts)});
                } else if (m == OUTSIDE && p == FLUID) {
                    boundary_faces["inlet"].push_back({get_cell_id(i, j, k), reversed(pts)});
                } else if (m == FLUID && p == SOLID) {
                    boundary_faces["object"].push_back({get_cell_id(i - 1, j, k), forward(pts)});
                } else if (m == SOLID && p == FLUID) {
                    boundary_faces["object"].push_back({get_cell_id(i, j, k), reversed(pts)});
                }
            }
        }
    }

    // ---- Y-direction faces ----
    for (int j = 0; j <= ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            for (int k = 0; k < nz; ++k) {
                int m = get_code(i, j - 1, k), p = get_code(i, j, k);
                std::array<std::int64_t, 4> pts = {
                    point_id(i, j, k), point_id(i, j, k + 1), point_id(i + 1, j, k + 1), point_id(i + 1, j, k),
                }; // verified normal points in +y
                if (m == FLUID && p == FLUID) {
                    internal_faces.push_back({get_cell_id(i, j - 1, k), get_cell_id(i, j, k), pts});
                } else if (m == FLUID && p == OUTSIDE) {
                    boundary_faces["upperWall"].push_back({get_cell_id(i, j - 1, k), forward(pts)});
                } else if (m == OUTSIDE && p == FLUID) {
                    boundary_faces["lowerWall"].push_back({get_cell_id(i, j, k), reversed(pts)});
                } else if (m == FLUID && p == SOLID) {
                    boundary_faces["object"].push_back({get_cell_id(i, j - 1, k), forward(pts)});
                } else if (m == SOLID && p == FLUID) {
                    boundary_faces["object"].push_back({get_cell_id(i, j, k), reversed(pts)});
                }
            }
        }
    }

    // ---- Z-direction faces ----
    for (int k = 0; k <= nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                int m = get_code(i, j, k - 1), p = get_code(i, j, k);
                std::array<std::int64_t, 4> pts = {
                    point_id(i, j, k), point_id(i + 1, j, k), point_id(i + 1, j + 1, k), point_id(i, j + 1, k),
                }; // verified normal points in +z
                if (m == FLUID && p == FLUID) {
                    internal_faces.push_back({get_cell_id(i, j, k - 1), get_cell_id(i, j, k), pts});
                } else if (m == FLUID && p == OUTSIDE) {
                    boundary_faces["backWall"].push_back({get_cell_id(i, j, k - 1), forward(pts)});
                } else if (m == OUTSIDE && p == FLUID) {
                    boundary_faces["frontWall"].push_back({get_cell_id(i, j, k), reversed(pts)});
                } else if (m == FLUID && p == SOLID) {
                    boundary_faces["object"].push_back({get_cell_id(i, j, k - 1), forward(pts)});
                } else if (m == SOLID && p == FLUID) {
                    boundary_faces["object"].push_back({get_cell_id(i, j, k), reversed(pts)});
                }
            }
        }
    }

    // sort internal faces by (owner, neighbour) as OpenFOAM expects
    std::sort(internal_faces.begin(), internal_faces.end(), [](const InternalFace& a, const InternalFace& b) {
        return std::tie(a.owner, a.neighbour) < std::tie(b.owner, b.neighbour);
    });

    faces_.clear();
    for (auto& f : internal_faces) {
        FaceRecord fr;
        fr.owner = f.owner;
        fr.neighbour = f.neighbour;
        fr.points.assign(f.pts.begin(), f.pts.end());
        faces_.push_back(std::move(fr));
    }
    n_internal_faces_ = static_cast<int>(faces_.size());

    if (surface_mesh_ != nullptr && !boundary_faces["object"].empty()) {
        boundary_faces["object"] = refine_object_patch(boundary_faces["object"]);
    }

    static const std::unordered_map<std::string, std::string> patch_types = {
        {"inlet", "patch"}, {"outlet", "patch"}, {"lowerWall", "wall"}, {"upperWall", "wall"},
        {"frontWall", "wall"}, {"backWall", "wall"}, {"object", "wall"},
    };
    patches_.clear();
    for (const auto& name : patch_order) {
        auto& faces = boundary_faces[name];
        if (faces.empty()) continue;
        int start = static_cast<int>(faces_.size());
        for (auto& [owner, pts] : faces) {
            FaceRecord fr;
            fr.owner = owner;
            fr.neighbour = -1;
            fr.points = std::move(pts);
            faces_.push_back(std::move(fr));
        }
        patches_.push_back({name, patch_types.at(name), start, static_cast<int>(faces.size())});
    }
}

std::vector<std::pair<int, std::vector<std::int64_t>>> OpenFoamCaseWriter::refine_object_patch(
    const std::vector<std::pair<int, std::vector<std::int64_t>>>& object_faces) {
    // Replace the flat, staircased voxel faces on the object patch with
    // the actual uploaded surface geometry: for each boundary cell, clip
    // the real uploaded mesh's own triangles directly against that cell's
    // box (mesh::clip_mesh_to_box) -- no watertightness/volume
    // requirement, so thin or open features (e.g. a wing skin) are
    // preserved instead of silently degrading to a flat quad. Falls back
    // to the original flat quad as a last resort -- geometry-only: cell
    // classification, cell count, and field values are completely
    // unaffected.
    //
    // Deliberately NOT ported: io/openfoam_writer.py's intermediate
    // per-cell solid-boolean-difference fallback tier (only meaningful
    // when the surface mesh is a proper watertight volume, itself already
    // a narrower edge case than the primary clip path finding nothing).
    // That tier needs a full mesh boolean-CSG library (the Python app
    // uses the `manifold3d` engine); adding one is a substantial new
    // dependency this port doesn't take on yet. If a real uploaded mesh
    // is ever found to need it, the Manifold C++ library (what
    // manifold3d itself binds) is the natural next step -- see the
    // architecture plan's library-selection table.
    std::unordered_map<int, std::vector<std::vector<std::int64_t>>> by_owner;
    for (const auto& [owner, pts] : object_faces) by_owner[owner].push_back(pts);

    double dx = dx_, dy = dy_, dz = dz_;
    double tol = 1e-6 * std::min({dx, dy, dz});
    std::vector<std::pair<int, std::vector<std::int64_t>>> refined;
    std::int64_t next_point_id =
        static_cast<std::int64_t>(nx_ + 1) * (ny_ + 1) * (nz_ + 1) + static_cast<std::int64_t>(extra_points_.size());

    // Weld coincident points instead of minting a fresh point ID per
    // triangle-corner (see io/openfoam_writer.py's _weld_point for the
    // full rationale -- fixes disconnected-island/blocky rendering).
    // Quantizing to a scaled-integer key (round(coord * 1e9)) is many
    // orders of magnitude looser than float64 round-off at these
    // coordinate scales, but far tighter than any real surface feature.
    std::map<std::array<std::int64_t, 3>, std::int64_t> point_id_by_coord;
    auto weld_point = [&](const Vec3& coord) -> std::int64_t {
        std::array<std::int64_t, 3> key = {
            static_cast<std::int64_t>(std::llround(coord.x * 1e9)),
            static_cast<std::int64_t>(std::llround(coord.y * 1e9)),
            static_cast<std::int64_t>(std::llround(coord.z * 1e9)),
        };
        auto it = point_id_by_coord.find(key);
        if (it != point_id_by_coord.end()) return it->second;
        std::int64_t pid = next_point_id++;
        point_id_by_coord[key] = pid;
        extra_points_.push_back(coord);
        return pid;
    };

    for (auto& [owner, quads] : by_owner) {
        const auto& ijk = cell_ijk_[static_cast<std::size_t>(owner)];
        int i = ijk[0], j = ijk[1], k = ijk[2];
        double cx = (i + 0.5) * dx, cy = (j + 0.5) * dy, cz = (k + 0.5) * dz;
        double xmin = cx - dx / 2, ymin = cy - dy / 2, zmin = cz - dz / 2;
        double xmax = cx + dx / 2, ymax = cy + dy / 2, zmax = cz + dz / 2;

        std::vector<RawTriangle> surface_faces =
            cfd::mesh::clip_mesh_to_box(*surface_mesh_, xmin, xmax, ymin, ymax, zmin, zmax, tol);

        if (surface_faces.empty()) {
            n_cells_fallback_++;
            for (const auto& pts : quads) refined.push_back({owner, pts});
            continue;
        }

        Vec3 center{cx, cy, cz};
        std::vector<std::pair<int, std::vector<std::int64_t>>> cell_faces;
        for (const auto& tri : surface_faces) {
            std::array<std::int64_t, 3> point_ids = {weld_point(tri[0]), weld_point(tri[1]), weld_point(tri[2])};
            if (point_ids[0] == point_ids[1] || point_ids[1] == point_ids[2] || point_ids[0] == point_ids[2]) {
                continue; // slicing produced a sliver that welding collapsed onto <3 distinct points
            }
            Vec3 normal = cross(tri[1] - tri[0], tri[2] - tri[0]);
            if (dot(normal, tri[0] - center) < 0) std::swap(point_ids[1], point_ids[2]);
            cell_faces.push_back({owner, std::vector<std::int64_t>(point_ids.begin(), point_ids.end())});
        }

        if (cell_faces.empty()) {
            n_cells_fallback_++;
            for (const auto& pts : quads) refined.push_back({owner, pts});
            continue;
        }

        n_cells_smoothed_++;
        for (auto& cf : cell_faces) refined.push_back(std::move(cf));
    }

    return refined;
}

void OpenFoamCaseWriter::write_points() const {
    int nx = nx_, ny = ny_, nz = nz_;
    std::size_t n_grid_points = static_cast<std::size_t>(nx + 1) * static_cast<std::size_t>(ny + 1) * static_cast<std::size_t>(nz + 1);
    std::vector<Vec3> coords(n_grid_points);
    for (int k = 0; k <= nz; ++k) {
        for (int j = 0; j <= ny; ++j) {
            for (int i = 0; i <= nx; ++i) {
                coords[static_cast<std::size_t>(point_id(i, j, k))] = Vec3{i * dx_, j * dy_, k * dz_};
            }
        }
    }

    std::ostringstream body;
    body << "\n" << (coords.size() + extra_points_.size()) << "\n(\n";
    for (const auto& c : coords) {
        body << "(" << format_g(c.x, 8) << " " << format_g(c.y, 8) << " " << format_g(c.z, 8) << ")\n";
    }
    for (const auto& c : extra_points_) {
        body << "(" << format_g(c.x, 8) << " " << format_g(c.y, 8) << " " << format_g(c.z, 8) << ")\n";
    }
    body << ")\n";

    write_foam_file((fs::path(case_dir_) / "constant" / "polyMesh" / "points").string(), "vectorField", "points", body.str());
}

void OpenFoamCaseWriter::write_faces_owner_neighbour_boundary() const {
    auto polymesh = fs::path(case_dir_) / "constant" / "polyMesh";

    std::ostringstream face_body;
    face_body << "\n" << faces_.size() << "\n(\n";
    for (const auto& f : faces_) {
        face_body << f.points.size() << "(";
        for (std::size_t idx = 0; idx < f.points.size(); ++idx) {
            if (idx) face_body << " ";
            face_body << f.points[idx];
        }
        face_body << ")\n";
    }
    face_body << ")\n";
    write_foam_file((polymesh / "faces").string(), "faceList", "faces", face_body.str());

    std::ostringstream owner_body;
    owner_body << "\n" << faces_.size() << "\n(\n";
    for (const auto& f : faces_) owner_body << f.owner << "\n";
    owner_body << ")\n";
    write_foam_file((polymesh / "owner").string(), "labelList", "owner", owner_body.str());

    std::ostringstream nb_body;
    nb_body << "\n" << n_internal_faces_ << "\n(\n";
    for (int i = 0; i < n_internal_faces_; ++i) nb_body << faces_[static_cast<std::size_t>(i)].neighbour << "\n";
    nb_body << ")\n";
    write_foam_file((polymesh / "neighbour").string(), "labelList", "neighbour", nb_body.str());

    std::ostringstream b_body;
    b_body << "\n" << patches_.size() << "\n(\n";
    for (const auto& p : patches_) {
        b_body << "    " << p.name << "\n    {\n";
        b_body << "        type            " << p.type << ";\n";
        b_body << "        nFaces          " << p.n_faces << ";\n";
        b_body << "        startFace       " << p.start_face << ";\n";
        b_body << "    }\n";
    }
    b_body << ")\n";
    write_foam_file((polymesh / "boundary").string(), "polyBoundaryMesh", "boundary", b_body.str());
}

void OpenFoamCaseWriter::write_system_stubs() const {
    auto system = fs::path(case_dir_) / "system";
    write_foam_file((system / "controlDict").string(), "dictionary", "controlDict",
        "application     cfdStudio;\nstartFrom       startTime;\nstartTime       0;\n"
        "stopAt          endTime;\nendTime         1;\ndeltaT          1;\nwriteControl    timeStep;\n"
        "writeInterval   1;\n");
    write_foam_file((system / "fvSchemes").string(), "dictionary", "fvSchemes", "// generated by CFD Studio\n");
    write_foam_file((system / "fvSolution").string(), "dictionary", "fvSolution", "// generated by CFD Studio\n");
}

void OpenFoamCaseWriter::write_foam_placeholder() const {
    std::ofstream f(foam_path());
}

std::string OpenFoamCaseWriter::foam_path() const {
    fs::path p(case_dir_);
    std::string case_name = p.filename().string();
    if (case_name.empty()) case_name = "case";
    return (p / (case_name + ".foam")).string();
}

void OpenFoamCaseWriter::write_timestep(double time, const double* u, const double* v, const double* w, const double* p) const {
    std::string time_name = (time == 0.0) ? "0" : format_g(time, 6);
    auto time_dir = fs::path(case_dir_) / time_name;
    fs::create_directories(time_dir);

    int ny = ny_, nz = nz_;
    auto flat = [&](int i, int j, int k) { return cell_flat(i, j, k, ny, nz); };

    std::ostringstream u_body;
    u_body << "\ndimensions      [0 1 -1 0 0 0 0];\ninternalField   nonuniform List<vector>\n" << n_cells_ << "\n(\n";
    for (const auto& ijk : cell_ijk_) {
        std::size_t idx = flat(ijk[0], ijk[1], ijk[2]);
        u_body << "(" << format_e(u[idx], 6) << " " << format_e(v[idx], 6) << " " << format_e(w[idx], 6) << ")\n";
    }
    u_body << ");\n\n" << boundary_field_vector();
    write_foam_file((time_dir / "U").string(), "volVectorField", "U", u_body.str());

    std::ostringstream p_body;
    p_body << "\ndimensions      [0 2 -2 0 0 0 0];\ninternalField   nonuniform List<scalar>\n" << n_cells_ << "\n(\n";
    for (const auto& ijk : cell_ijk_) {
        p_body << format_e(p[flat(ijk[0], ijk[1], ijk[2])], 6) << "\n";
    }
    p_body << ");\n\n" << boundary_field_scalar();
    write_foam_file((time_dir / "p").string(), "volScalarField", "p", p_body.str());
}

std::string OpenFoamCaseWriter::boundary_field_vector() const {
    std::ostringstream out;
    out << "boundaryField\n{\n";
    for (const auto& p : patches_) {
        out << "    " << p.name << "\n    {\n";
        if (p.name == "inlet") {
            out << "        type            fixedValue;\n        value           uniform (1 0 0);\n";
        } else if (p.name == "outlet") {
            out << "        type            zeroGradient;\n";
        } else if (p.name == "object") {
            out << "        type            noSlip;\n";
        } else if (domain_mode_ == "internal") {
            out << "        type            noSlip;\n";
        } else {
            out << "        type            slip;\n";
        }
        out << "    }\n";
    }
    out << "}\n";
    return out.str();
}

std::string OpenFoamCaseWriter::boundary_field_scalar() const {
    std::ostringstream out;
    out << "boundaryField\n{\n";
    for (const auto& p : patches_) {
        out << "    " << p.name << "\n    {\n";
        if (p.name == "outlet") {
            out << "        type            fixedValue;\n        value           uniform 0;\n";
        } else {
            out << "        type            zeroGradient;\n";
        }
        out << "    }\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace cfd::io
