#include "mesh/mesh_bvh.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include <embree4/rtcore.h>

namespace cfd::mesh {

namespace {
constexpr float kInf = std::numeric_limits<float>::infinity();

// A fixed, non-axis-aligned ray direction for the containment parity test --
// deliberately not (1,0,0)/(0,1,0)/(0,0,1) to make an unlucky ray-grazes-an-
// edge-or-vertex coincidence with an axis-aligned mesh feature far less
// likely (a real risk for CAD-derived STL/OBJ files, which very often have
// faces aligned to the coordinate axes).
constexpr float kParityDirX = 0.5257311121f;
constexpr float kParityDirY = 0.8506508084f;
constexpr float kParityDirZ = 0.0f;
} // namespace

struct MeshBVH::Impl {
    RTCDevice device = nullptr;
    RTCScene scene = nullptr;

    ~Impl() {
        if (scene) rtcReleaseScene(scene);
        if (device) rtcReleaseDevice(device);
    }
};

MeshBVH::MeshBVH(const Mesh& mesh) : impl_(std::make_unique<Impl>()) {
    impl_->device = rtcNewDevice(nullptr);
    if (!impl_->device) {
        throw std::runtime_error("MeshBVH: rtcNewDevice failed (no Embree-capable device?)");
    }

    impl_->scene = rtcNewScene(impl_->device);

    RTCGeometry geom = rtcNewGeometry(impl_->device, RTC_GEOMETRY_TYPE_TRIANGLE);

    auto* verts = static_cast<float*>(rtcSetNewGeometryBuffer(
        geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 3 * sizeof(float), mesh.vertices.size()));
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        verts[3 * i + 0] = static_cast<float>(mesh.vertices[i].x);
        verts[3 * i + 1] = static_cast<float>(mesh.vertices[i].y);
        verts[3 * i + 2] = static_cast<float>(mesh.vertices[i].z);
    }

    auto* idx = static_cast<unsigned*>(rtcSetNewGeometryBuffer(
        geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3 * sizeof(unsigned), mesh.triangles.size()));
    for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
        idx[3 * i + 0] = mesh.triangles[i][0];
        idx[3 * i + 1] = mesh.triangles[i][1];
        idx[3 * i + 2] = mesh.triangles[i][2];
    }

    rtcCommitGeometry(geom);
    rtcAttachGeometry(impl_->scene, geom);
    rtcReleaseGeometry(geom); // scene keeps its own reference after attach
    rtcCommitScene(impl_->scene);
}

MeshBVH::~MeshBVH() = default;
MeshBVH::MeshBVH(MeshBVH&&) noexcept = default;
MeshBVH& MeshBVH::operator=(MeshBVH&&) noexcept = default;

bool MeshBVH::contains(const Vec3& point) const {
    RTCRayHit rayhit{};
    rayhit.ray.org_x = static_cast<float>(point.x);
    rayhit.ray.org_y = static_cast<float>(point.y);
    rayhit.ray.org_z = static_cast<float>(point.z);
    rayhit.ray.dir_x = kParityDirX;
    rayhit.ray.dir_y = kParityDirY;
    rayhit.ray.dir_z = kParityDirZ;
    rayhit.ray.tnear = 0.0f;
    rayhit.ray.tfar = kInf;
    rayhit.ray.mask = 0xFFFFFFFF;
    rayhit.ray.flags = 0;

    int crossings = 0;
    // Advance past each hit and keep casting until the ray exits the scene's
    // bounds entirely -- rtcIntersect1 only ever returns the nearest hit
    // ahead of tnear, so counting total crossings needs this loop rather
    // than a single call.
    while (true) {
        rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
        rayhit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
        rtcIntersect1(impl_->scene, &rayhit);
        if (rayhit.hit.geomID == RTC_INVALID_GEOMETRY_ID) break;

        ++crossings;
        // Nudge tnear just past this hit (relative epsilon, since tfar
        // reports the hit distance along a direction Embree already
        // normalized internally) so the next iteration doesn't re-find the
        // same triangle.
        float next_tnear = rayhit.ray.tfar * (1.0f + 1e-4f) + 1e-6f;
        rayhit.ray.tnear = next_tnear;
        rayhit.ray.tfar = kInf;
    }

    return (crossings % 2) == 1;
}

std::optional<RayHit> MeshBVH::nearest_hit(const Vec3& origin, const Vec3& direction) const {
    double len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len <= 0.0) return std::nullopt;

    RTCRayHit rayhit{};
    rayhit.ray.org_x = static_cast<float>(origin.x);
    rayhit.ray.org_y = static_cast<float>(origin.y);
    rayhit.ray.org_z = static_cast<float>(origin.z);
    rayhit.ray.dir_x = static_cast<float>(direction.x / len);
    rayhit.ray.dir_y = static_cast<float>(direction.y / len);
    rayhit.ray.dir_z = static_cast<float>(direction.z / len);
    rayhit.ray.tnear = 0.0f;
    rayhit.ray.tfar = kInf;
    rayhit.ray.mask = 0xFFFFFFFF;
    rayhit.ray.flags = 0;
    rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rayhit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;

    rtcIntersect1(impl_->scene, &rayhit);
    if (rayhit.hit.geomID == RTC_INVALID_GEOMETRY_ID) return std::nullopt;

    RayHit result;
    result.distance = static_cast<double>(rayhit.ray.tfar);
    result.triangle_index = rayhit.hit.primID;
    result.point = Vec3{
        origin.x + direction.x / len * result.distance,
        origin.y + direction.y / len * result.distance,
        origin.z + direction.z / len * result.distance,
    };
    return result;
}

} // namespace cfd::mesh
