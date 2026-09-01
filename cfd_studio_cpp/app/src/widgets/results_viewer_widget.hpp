#pragma once

#include <memory>

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>

#include "io/results_cache_reader.hpp"
#include "mesh/mesh.hpp"
#include "theme.hpp"

// Renders the object mesh plus one movable, scalar-field-colored slice
// plane through a loaded results-cache frame -- Phase 1 ("walking
// skeleton") of the in-app results visualizer, so a completed run can be
// inspected without launching external ParaView. Same architectural
// pattern as MeshPreviewWidget (orbit camera, hand-rolled GLSL,
// construction-time Theme injection -- not live-updating across a theme
// switch, an accepted limitation matching how OrientationDialog already
// works): reuses its lit mesh shader unchanged and adds a new
// textured-quad shader for the slice.
class ResultsViewerWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    enum class ScalarField { VelocityMagnitude, Pressure, VelocityU, VelocityV, VelocityW };
    enum class Axis { X, Y, Z };

    explicit ResultsViewerWidget(QWidget* parent = nullptr);
    ~ResultsViewerWidget() override;

    void setTheme(const Theme& theme);

    // Uploads the object mesh (unchanged across frames/fields/slices).
    // Resets the camera to frame its bounding box -- same contract as
    // MeshPreviewWidget::setMesh.
    void setMesh(const cfd::mesh::Mesh& mesh);

    // Points this widget at an already-opened results cache. The caller
    // is expected to have constructed `reader` itself (its constructor
    // can throw on a missing/corrupt cache) so a failure can be reported
    // before this widget is even shown, rather than surfaced from inside
    // a setter here. Defaults to frame 0.
    void setResultsCache(std::shared_ptr<cfd::io::ResultsCacheReader> reader);

    [[nodiscard]] int frameCount() const;
    [[nodiscard]] double frameTime(int index) const;

    void setFrame(int index);
    void setScalarField(ScalarField field);
    void setSliceAxis(Axis axis);
    void setSlicePosition(double t); // 0..1, normalized along the slice axis

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void rebuildMeshGeometry();
    void rebuildSlice(); // re-extracts the CPU slice buffer, re-uploads the texture, recomputes the quad corners
    [[nodiscard]] QMatrix4x4 viewMatrix() const;
    [[nodiscard]] QMatrix4x4 projectionMatrix() const;

    Theme theme_ = theme_by_key(kDefaultThemeKey);

    cfd::mesh::Mesh mesh_;
    bool haveMesh_ = false;
    bool meshDirty_ = false;
    QOpenGLBuffer meshVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject meshVao_;
    int meshVertexCount_ = 0;
    std::unique_ptr<QOpenGLShaderProgram> meshProgram_;

    QOpenGLBuffer sliceVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject sliceVao_;
    std::unique_ptr<QOpenGLShaderProgram> sliceProgram_;
    unsigned int sliceTexture_ = 0;
    bool sliceDirty_ = false;
    bool haveSlice_ = false;

    std::shared_ptr<cfd::io::ResultsCacheReader> reader_;
    cfd::io::ResultsFrame currentFrame_;
    bool haveFrame_ = false;
    int frameIndex_ = 0;
    ScalarField field_ = ScalarField::VelocityMagnitude;
    Axis axis_ = Axis::Y;
    double slicePosition_ = 0.5;

    QVector3D center_{0, 0, 0};
    float radius_ = 1.0f;
    float yaw_ = -45.0f, pitch_ = 25.0f, distance_ = 3.0f;
    QPoint lastMousePos_;
    bool dragging_ = false;
};
