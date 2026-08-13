#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>

#include "mesh/mesh.hpp"
#include "mesh/mesh_bvh.hpp"

// Hardware-rasterized 3D mesh preview + ray-cast picking -- replaces the
// Python app's mplot3d-based orientation-dialog view, which needed mesh
// decimation (capped at 1500 faces) to stay interactive under
// matplotlib's per-frame CPU reprojection, plus a manual
// pick_event/proj3d depth-tiebreak hack for picking. QOpenGLWidget
// renders the mesh at full resolution directly (520k triangles, no
// decimation needed) and cfd::mesh::MeshBVH::nearest_hit gives a real
// ray cast for picking instead of that hack.
class MeshPreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit MeshPreviewWidget(QWidget* parent = nullptr);
    ~MeshPreviewWidget() override;

    // Uploads `mesh` to the GPU and (re)builds its MeshBVH for picking.
    // Resets the camera to frame the mesh's bounding box.
    void setMesh(const cfd::mesh::Mesh& mesh);

    // Draws an arrow from the mesh's centroid along `axis` (unit vector).
    // std::nullopt hides it.
    void setFlowAxis(std::optional<cfd::mesh::Vec3> axis);

    // While true, a left-click picks a point on the mesh (emitting
    // pointPicked) instead of starting a camera-orbit drag.
    void setPickMode(bool enabled);

    // A single highlighted point (e.g. the currently picked vertex).
    // std::nullopt clears it.
    void setHighlightPoint(std::optional<cfd::mesh::Vec3> point);

signals:
    void pointPicked(cfd::mesh::Vec3 point, std::uint32_t triangleIndex);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void rebuildGeometry();
    [[nodiscard]] QMatrix4x4 viewMatrix() const;
    [[nodiscard]] QMatrix4x4 projectionMatrix() const;
    void pickAt(const QPoint& pos);
    void drawOverlays(const QMatrix4x4& mvp);

    cfd::mesh::Mesh mesh_;
    std::unique_ptr<cfd::mesh::MeshBVH> bvh_;
    bool haveMesh_ = false;
    bool meshDirty_ = false;

    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vao_;
    int vertexCount_ = 0;
    std::unique_ptr<QOpenGLShaderProgram> meshProgram_;

    QOpenGLBuffer overlayVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject overlayVao_;
    std::unique_ptr<QOpenGLShaderProgram> flatProgram_;

    QVector3D center_{0, 0, 0};
    float radius_ = 1.0f;

    float yaw_ = -45.0f, pitch_ = 25.0f, distance_ = 3.0f;
    QPoint lastMousePos_;
    bool dragging_ = false;

    bool pickMode_ = false;
    std::optional<cfd::mesh::Vec3> flowAxis_;
    std::optional<cfd::mesh::Vec3> highlightPoint_;
};
