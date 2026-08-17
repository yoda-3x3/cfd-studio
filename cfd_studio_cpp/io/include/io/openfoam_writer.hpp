#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mesh/mesh.hpp"

namespace cfd::io {

struct Patch {
    std::string name, type;
    int start_face = 0, n_faces = 0;
};

// Writes the 3D solver's Cartesian grid + solid mask out as a real OpenFOAM
// case: a polyMesh (points/faces/owner/neighbour/boundary) plus per-
// timestep field files (U, p), openable in ParaView via a `.foam`
// placeholder file. Port of io/openfoam_writer.py's OpenFoamCaseWriter.
//
// Unlike a VTK ImageData export (which just carries a solid/fluid scalar
// flag on every point), this carves the immersed obstacle out of the mesh
// entirely -- solid cells are not part of the mesh at all, and every face
// between a fluid cell and a solid cell becomes a real boundary face on an
// "object" patch, exactly like a body-fitted OpenFOAM case would have.
//
// Mesh topology never changes step to step, so it's written once at
// construction under constant/polyMesh/; each write_timestep() call only
// writes new U/p field files.
class OpenFoamCaseWriter {
public:
    // `solid_mask` is row-major (nx,ny,nz), i slowest/k fastest, true=solid.
    // `surface_mesh`, if provided, is used to refine the flat staircased
    // "object" patch faces to the actual uploaded surface geometry (see
    // refine_object_patch()) -- same coordinate frame as the grid.
    OpenFoamCaseWriter(
        const std::string& case_dir, int nx, int ny, int nz, double dx, double dy, double dz,
        const std::vector<std::uint8_t>& solid_mask,
        const cfd::mesh::Mesh* surface_mesh = nullptr,
        const std::string& domain_mode = "external",
        bool ground_effect = false);

    // Writes U/p field files for one timestep. `u`,`v`,`w`,`p` are each a
    // full row-major (nx,ny,nz) array (including solid cells, whose values
    // are ignored) -- selection down to fluid-only cells uses the writer's
    // own fluid_cell_ijk index list (built once during topology
    // construction), not a dense-array boolean-mask select coincidentally
    // matching cell id assignment order (the order-invariant risk flagged
    // and fixed during this port, relative to the Python original).
    void write_timestep(double time, const double* u, const double* v, const double* w, const double* p) const;

    [[nodiscard]] std::string foam_path() const;
    [[nodiscard]] int n_cells_smoothed() const { return n_cells_smoothed_; }
    [[nodiscard]] int n_cells_fallback() const { return n_cells_fallback_; }
    [[nodiscard]] int n_cells() const { return n_cells_; }

private:
    struct FaceRecord {
        int owner = -1, neighbour = -1; // neighbour == -1 for boundary faces
        std::vector<std::int64_t> points;
    };

    void build_topology(const std::vector<std::uint8_t>& solid_mask);
    [[nodiscard]] std::vector<std::pair<int, std::vector<std::int64_t>>> refine_object_patch(
        const std::vector<std::pair<int, std::vector<std::int64_t>>>& object_faces);
    void write_points() const;
    void write_faces_owner_neighbour_boundary() const;
    void write_system_stubs() const;
    void write_foam_placeholder() const;
    [[nodiscard]] std::string boundary_field_vector() const;
    [[nodiscard]] std::string boundary_field_scalar() const;
    [[nodiscard]] std::int64_t point_id(int i, int j, int k) const;

    std::string case_dir_;
    std::string domain_mode_;
    bool ground_effect_ = false; // External-only: lowerWall exported as noSlip instead of slip
    int nx_, ny_, nz_;
    double dx_, dy_, dz_;
    const cfd::mesh::Mesh* surface_mesh_; // not owned, must outlive the writer

    int n_cells_ = 0;
    std::vector<int> cell_id_;               // row-major (nx,ny,nz), -1 or fluid-cell index
    std::vector<std::array<int, 3>> cell_ijk_; // cell_ijk_[cell_id] -> (i,j,k)

    std::vector<FaceRecord> faces_; // internal faces first, then boundary faces per patch
    int n_internal_faces_ = 0;
    std::vector<Patch> patches_;
    std::vector<cfd::mesh::Vec3> extra_points_; // welded points added by refine_object_patch

    int n_cells_smoothed_ = 0;
    int n_cells_fallback_ = 0;
};

} // namespace cfd::io
