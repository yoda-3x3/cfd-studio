#pragma once

#include <array>
#include <string>

#include "mesh/mesh.hpp"

namespace cfd::solvers {

// Port of solver3d/orientation.py's OrientationCandidate.
struct OrientationCandidate {
    std::string label;
    cfd::mesh::Vec3 flow_axis; // unit vector, becomes +x
    cfd::mesh::Vec3 up_axis;   // unit vector, becomes +y
    cfd::mesh::Vec3 span_axis; // unit vector, becomes +z
    double projected_area = 0.0;
    int rank = 0;
};

// Three orthonormal candidate axes for the mesh, via PCA over the vertex
// cloud (eigenvectors of the vertex covariance matrix). Port of
// solver3d/orientation.py's _principal_axes -- this app's fallback path
// when trimesh's true mass/inertia-tensor axes aren't available; used here
// as the sole method (a deliberate, documented scope reduction: the full
// volume-integral inertia tensor needs a valid watertight volume and is
// meaningfully more code for a heuristic feature, not a correctness-
// critical one -- a wrong initial orientation is just corrected by the
// user via leading-edge picking in the GUI).
[[nodiscard]] std::array<cfd::mesh::Vec3, 3> principal_axes(const cfd::mesh::Mesh& mesh);

// Convex-hull area of the vertex cloud projected onto the plane
// perpendicular to `axis` -- a fast, reasonable proxy for frontal area.
// Falls back to a bounding-box extent product on degenerate (near-planar/
// collinear) projections, matching solver3d/orientation.py's
// _projected_silhouette_area.
[[nodiscard]] double projected_silhouette_area(const std::vector<cfd::mesh::Vec3>& vertices, const cfd::mesh::Vec3& axis);

// Builds a full OrientationCandidate (orthonormal up/span basis) from an
// arbitrary flow axis -- not necessarily one of the mesh's principal axes
// (used for both the 3 auto-suggested candidates and a user-picked
// leading-edge axis). Picks an up-axis from `reference_axes` (the mesh's
// principal axes, by default) that isn't near-parallel to `flow_axis`,
// then Gram-Schmidt orthogonalizes; falls back to an arbitrary
// perpendicular basis if every reference axis is too close to parallel.
// Port of solver3d/orientation.py's candidate_from_flow_axis.
[[nodiscard]] OrientationCandidate candidate_from_flow_axis(
    const cfd::mesh::Mesh& mesh, cfd::mesh::Vec3 flow_axis, const std::string& label, int rank = 0,
    const std::array<cfd::mesh::Vec3, 3>* reference_axes = nullptr);

// The 3 candidate flow-axis orientations, ranked smallest projected area
// first (index 0 = suggested). Port of solver3d/orientation.py's
// analyze_orientation.
[[nodiscard]] std::array<OrientationCandidate, 3> analyze_orientation(const cfd::mesh::Mesh& mesh);

// 3x3 rotation matrix (as 3 row vectors) mapping candidate.flow_axis to
// +x, up_axis to +y, span_axis to +z -- span is flipped if the resulting
// basis would be mirrored (det < 0), to guarantee a proper rotation. Port
// of solver3d/orientation.py's rotation_matrix_for.
[[nodiscard]] std::array<cfd::mesh::Vec3, 3> rotation_matrix_for(const OrientationCandidate& candidate);

// Centers `mesh` at its vertex mean and applies rotation_matrix_for(candidate).
// Port of solver3d/orientation.py's apply_orientation.
[[nodiscard]] cfd::mesh::Mesh apply_orientation(const cfd::mesh::Mesh& mesh, const OrientationCandidate& candidate);

} // namespace cfd::solvers
