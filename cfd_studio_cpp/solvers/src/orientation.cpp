#include "solvers/orientation.hpp"

#include <algorithm>
#include <cmath>

#define EIGEN_MPL2_ONLY
#include <Eigen/Eigenvalues>

#include "mesh/convex_hull2d.hpp"

namespace cfd::solvers {

using cfd::mesh::cross;
using cfd::mesh::dot;
using cfd::mesh::Mesh;
using cfd::mesh::norm;
using cfd::mesh::normalize;
using cfd::mesh::Vec3;

std::array<Vec3, 3> principal_axes(const Mesh& mesh) {
    Vec3 mean{0, 0, 0};
    for (const auto& v : mesh.vertices) mean = mean + v;
    if (!mesh.vertices.empty()) mean = mean * (1.0 / static_cast<double>(mesh.vertices.size()));

    Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
    for (const auto& v : mesh.vertices) {
        Eigen::Vector3d d(v.x - mean.x, v.y - mean.y, v.z - mean.z);
        cov += d * d.transpose();
    }

    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(cov);
    const Eigen::Matrix3d& eigvecs = solver.eigenvectors(); // columns are unit eigenvectors

    std::array<Vec3, 3> axes;
    for (int i = 0; i < 3; ++i) {
        axes[static_cast<std::size_t>(i)] = Vec3{eigvecs(0, i), eigvecs(1, i), eigvecs(2, i)};
    }
    return axes;
}

double projected_silhouette_area(const std::vector<Vec3>& vertices, const Vec3& axis_in) {
    Vec3 axis = normalize(axis_in);
    Vec3 helper = std::fabs(axis.x) < 0.9 ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 1.0, 0.0};
    Vec3 u = normalize(cross(axis, helper));
    Vec3 v = cross(axis, u);

    std::vector<cfd::mesh::Vec2> proj;
    proj.reserve(vertices.size());
    for (const auto& p : vertices) proj.push_back({dot(p, u), dot(p, v)});

    double area = cfd::mesh::convex_hull_area_2d(proj);
    if (area > 0.0) return area;

    // Degenerate (near-planar/collinear) projection -- fall back to a
    // bounding-box extent product, matching orientation.py's except-branch.
    if (proj.empty()) return 0.0;
    double min_u = proj[0].x, max_u = proj[0].x, min_v = proj[0].y, max_v = proj[0].y;
    for (const auto& p : proj) {
        min_u = std::min(min_u, p.x); max_u = std::max(max_u, p.x);
        min_v = std::min(min_v, p.y); max_v = std::max(max_v, p.y);
    }
    return (max_u - min_u) * (max_v - min_v);
}

OrientationCandidate candidate_from_flow_axis(
    const Mesh& mesh, Vec3 flow_axis_in, const std::string& label, int rank,
    const std::array<Vec3, 3>* reference_axes) {
    Vec3 flow_axis = normalize(flow_axis_in);
    std::array<Vec3, 3> axes = reference_axes ? *reference_axes : principal_axes(mesh);

    std::vector<Vec3> remaining;
    for (const auto& a : axes) {
        double d = std::fabs(dot(normalize(a), flow_axis));
        if (std::fabs(d - 1.0) >= 1e-6) remaining.push_back(a);
    }

    Vec3 up_axis;
    if (remaining.size() < 2) {
        // numerical fallback: build an arbitrary perpendicular basis
        Vec3 helper = std::fabs(flow_axis.x) < 0.9 ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 1.0, 0.0};
        up_axis = normalize(cross(flow_axis, helper));
    } else {
        up_axis = normalize(remaining[0]);
    }
    Vec3 span_axis = normalize(cross(flow_axis, up_axis));
    // re-orthogonalize up_axis to guarantee a clean right-handed basis
    up_axis = normalize(cross(span_axis, flow_axis));

    OrientationCandidate c;
    c.label = label;
    c.flow_axis = flow_axis;
    c.up_axis = up_axis;
    c.span_axis = span_axis;
    c.projected_area = projected_silhouette_area(mesh.vertices, flow_axis);
    c.rank = rank;
    return c;
}

std::array<OrientationCandidate, 3> analyze_orientation(const Mesh& mesh) {
    std::array<Vec3, 3> axes = principal_axes(mesh);

    std::array<std::pair<double, Vec3>, 3> scored;
    for (int i = 0; i < 3; ++i) {
        Vec3 axis = normalize(axes[static_cast<std::size_t>(i)]);
        scored[static_cast<std::size_t>(i)] = {projected_silhouette_area(mesh.vertices, axis), axis};
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    std::array<OrientationCandidate, 3> candidates;
    for (int rank = 0; rank < 3; ++rank) {
        std::string label = "Option " + std::to_string(rank + 1) + (rank == 0 ? " (suggested)" : "");
        candidates[static_cast<std::size_t>(rank)] =
            candidate_from_flow_axis(mesh, scored[static_cast<std::size_t>(rank)].second, label, rank, &axes);
    }
    return candidates;
}

std::array<Vec3, 3> rotation_matrix_for(const OrientationCandidate& candidate) {
    Vec3 flow = candidate.flow_axis, up = candidate.up_axis, span = candidate.span_axis;
    double det = dot(flow, cross(up, span));
    if (det < 0) span = span * -1.0;
    return {flow, up, span};
}

Mesh apply_orientation(const Mesh& mesh_in, const OrientationCandidate& candidate) {
    Mesh mesh = mesh_in;
    Vec3 mean{0, 0, 0};
    for (const auto& v : mesh.vertices) mean = mean + v;
    if (!mesh.vertices.empty()) mean = mean * (1.0 / static_cast<double>(mesh.vertices.size()));
    mesh.translate(mean * -1.0);

    std::array<Vec3, 3> basis = rotation_matrix_for(candidate);
    for (auto& v : mesh.vertices) {
        v = Vec3{dot(basis[0], v), dot(basis[1], v), dot(basis[2], v)};
    }
    return mesh;
}

} // namespace cfd::solvers
