#pragma once

#include "mesh/mesh.hpp"

namespace cfd::mesh {

// An axis-aligned box spanning [0, size.x] x [0, size.y] x [0, size.z],
// outward-facing triangle winding -- the same shape the hand-built test
// fixtures across tests/solvers/test_voxelizer.cpp and
// tests/io/test_openfoam_writer.cpp already construct ad hoc, promoted to a
// shared, reusable primitive.
[[nodiscard]] Mesh make_box(const Vec3& size);

// A cylinder centered on the x-axis, spanning x in [0, length].
//
// wall_thickness > 0 (and < outer_radius) builds a double-walled shell
// (outer + inner cylinder wall) bounding the wall MATERIAL only -- a real
// pipe's cross-section, matching the "duct" shape a mesh needs for internal
// domain-mode flow-through testing. wall_thickness <= 0 builds a single
// thin-walled outer cylinder only.
//
// `capped` adds end caps closing the mesh into a watertight shell: an
// annulus at each end for a walled tube, or a flat disk for a thin-walled
// one. Uncapped, either shape is open at both x-ends. Index/winding pattern
// mirrors the already-tested square-cross-section tube helpers in
// tests/solvers/test_voxelizer.cpp, generalized from 4 sides to
// radial_segments sides.
[[nodiscard]] Mesh make_tube(double length, double outer_radius, double wall_thickness,
                              int radial_segments, bool capped);

} // namespace cfd::mesh
