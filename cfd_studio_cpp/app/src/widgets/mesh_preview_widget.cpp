#include "mesh_preview_widget.hpp"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QMouseEvent>
#include <QVector4D>
#include <QWheelEvent>

namespace {
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

const char* kMeshVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMvp;
out vec3 vNormal;
void main() {
    vNormal = aNormal;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char* kMeshFragmentShader = R"(
#version 330 core
in vec3 vNormal;
uniform vec3 uLightDir;
uniform vec3 uColor;
out vec4 fragColor;
void main() {
    vec3 n = normalize(vNormal);
    float diff = max(dot(n, normalize(-uLightDir)), 0.0);
    vec3 color = uColor * (0.35 + 0.65 * diff);
    fragColor = vec4(color, 1.0);
}
)";

const char* kFlatVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    gl_PointSize = 10.0;
}
)";

const char* kFlatFragmentShader = R"(
#version 330 core
uniform vec3 uColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(uColor, 1.0);
}
)";
} // namespace

MeshPreviewWidget::MeshPreviewWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setMouseTracking(true);
}

MeshPreviewWidget::~MeshPreviewWidget() {
    makeCurrent();
    vbo_.destroy();
    overlayVbo_.destroy();
    doneCurrent();
}

void MeshPreviewWidget::setTheme(const Theme& theme) {
    theme_ = theme;
    update();
}

void MeshPreviewWidget::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);

    meshProgram_ = std::make_unique<QOpenGLShaderProgram>();
    meshProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kMeshVertexShader);
    meshProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kMeshFragmentShader);
    meshProgram_->link();

    flatProgram_ = std::make_unique<QOpenGLShaderProgram>();
    flatProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kFlatVertexShader);
    flatProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFlatFragmentShader);
    flatProgram_->link();

    vao_.create();
    overlayVao_.create();
    overlayVbo_.create();
}

void MeshPreviewWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void MeshPreviewWidget::setMesh(const cfd::mesh::Mesh& mesh) {
    mesh_ = mesh;
    bvh_ = std::make_unique<cfd::mesh::MeshBVH>(mesh_);
    haveMesh_ = true;
    meshDirty_ = true;

    auto bounds = mesh_.bounds();
    cfd::mesh::Vec3 c = bounds.center();
    center_ = QVector3D(static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z));
    cfd::mesh::Vec3 ext = bounds.extents();
    radius_ = static_cast<float>(std::max({ext.x, ext.y, ext.z, 1e-6})) * 0.6f;
    distance_ = radius_ * 3.0f;
    yaw_ = -45.0f;
    pitch_ = 25.0f;

    update();
}

void MeshPreviewWidget::setFlowAxis(std::optional<cfd::mesh::Vec3> axis) {
    flowAxis_ = axis;
    update();
}

void MeshPreviewWidget::setPickMode(bool enabled) {
    pickMode_ = enabled;
}

void MeshPreviewWidget::setHighlightPoint(std::optional<cfd::mesh::Vec3> point) {
    highlightPoint_ = point;
    update();
}

void MeshPreviewWidget::rebuildGeometry() {
    std::vector<float> vertexData;
    vertexData.reserve(mesh_.triangles.size() * 3 * 6);
    for (const auto& tri : mesh_.triangles) {
        const auto& a = mesh_.vertices[tri[0]];
        const auto& b = mesh_.vertices[tri[1]];
        const auto& c = mesh_.vertices[tri[2]];
        cfd::mesh::Vec3 normal = cfd::mesh::normalize(cfd::mesh::cross(b - a, c - a));
        for (const cfd::mesh::Vec3* v : {&a, &b, &c}) {
            vertexData.push_back(static_cast<float>(v->x));
            vertexData.push_back(static_cast<float>(v->y));
            vertexData.push_back(static_cast<float>(v->z));
            vertexData.push_back(static_cast<float>(normal.x));
            vertexData.push_back(static_cast<float>(normal.y));
            vertexData.push_back(static_cast<float>(normal.z));
        }
    }
    vertexCount_ = static_cast<int>(mesh_.triangles.size() * 3);

    vao_.bind();
    if (!vbo_.isCreated()) vbo_.create();
    vbo_.bind();
    vbo_.allocate(vertexData.data(), static_cast<int>(vertexData.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    vbo_.release();
    vao_.release();

    meshDirty_ = false;
}

QMatrix4x4 MeshPreviewWidget::viewMatrix() const {
    float yawRad = yaw_ * kDegToRad;
    float pitchRad = pitch_ * kDegToRad;
    QVector3D eye(center_.x() + distance_ * std::cos(pitchRad) * std::sin(yawRad),
                  center_.y() + distance_ * std::sin(pitchRad),
                  center_.z() + distance_ * std::cos(pitchRad) * std::cos(yawRad));
    QMatrix4x4 view;
    view.lookAt(eye, center_, QVector3D(0, 1, 0));
    return view;
}

QMatrix4x4 MeshPreviewWidget::projectionMatrix() const {
    QMatrix4x4 proj;
    float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    proj.perspective(45.0f, aspect, std::max(0.01f, radius_ * 0.01f), std::max(radius_ * 100.0f, 10.0f));
    return proj;
}

void MeshPreviewWidget::paintGL() {
    QColor clearColor(theme_.plot_bg);
    glClearColor(static_cast<float>(clearColor.redF()), static_cast<float>(clearColor.greenF()),
                 static_cast<float>(clearColor.blueF()), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!haveMesh_) return;
    if (meshDirty_) rebuildGeometry();

    QMatrix4x4 mvp = projectionMatrix() * viewMatrix();

    QColor meshColor(theme_.accent);
    meshProgram_->bind();
    meshProgram_->setUniformValue("uMvp", mvp);
    meshProgram_->setUniformValue("uLightDir", QVector3D(-0.4f, -1.0f, -0.3f));
    meshProgram_->setUniformValue("uColor", QVector3D(static_cast<float>(meshColor.redF()),
                                                        static_cast<float>(meshColor.greenF()),
                                                        static_cast<float>(meshColor.blueF())));
    vao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    vao_.release();
    meshProgram_->release();

    drawOverlays(mvp);
}

void MeshPreviewWidget::drawOverlays(const QMatrix4x4& mvp) {
    if (!flowAxis_ && !highlightPoint_) return;

    std::vector<float> lineVerts;
    std::vector<float> pointVerts;

    if (flowAxis_) {
        QVector3D axis(static_cast<float>(flowAxis_->x), static_cast<float>(flowAxis_->y),
                        static_cast<float>(flowAxis_->z));
        QVector3D tip = center_ + axis * radius_ * 1.6f;
        // Main shaft.
        lineVerts.insert(lineVerts.end(), {center_.x(), center_.y(), center_.z(), tip.x(), tip.y(), tip.z()});
        // A small two-legged arrowhead, roughly perpendicular to the shaft.
        QVector3D perp = QVector3D::crossProduct(axis, QVector3D(0, 1, 0));
        if (perp.lengthSquared() < 1e-6f) perp = QVector3D::crossProduct(axis, QVector3D(1, 0, 0));
        perp.normalize();
        float headSize = radius_ * 0.15f;
        QVector3D back = tip - axis * headSize * 2.0f;
        QVector3D leg1 = back + perp * headSize;
        QVector3D leg2 = back - perp * headSize;
        lineVerts.insert(lineVerts.end(), {tip.x(), tip.y(), tip.z(), leg1.x(), leg1.y(), leg1.z()});
        lineVerts.insert(lineVerts.end(), {tip.x(), tip.y(), tip.z(), leg2.x(), leg2.y(), leg2.z()});
    }
    if (highlightPoint_) {
        pointVerts.insert(pointVerts.end(), {static_cast<float>(highlightPoint_->x),
                                              static_cast<float>(highlightPoint_->y),
                                              static_cast<float>(highlightPoint_->z)});
    }

    QColor axisColor(theme_.plot_fg);
    QColor highlightColor(theme_.danger);

    flatProgram_->bind();
    flatProgram_->setUniformValue("uMvp", mvp);
    overlayVao_.bind();
    overlayVbo_.bind();
    glEnable(GL_PROGRAM_POINT_SIZE);

    if (!lineVerts.empty()) {
        overlayVbo_.allocate(lineVerts.data(), static_cast<int>(lineVerts.size() * sizeof(float)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
        flatProgram_->setUniformValue("uColor", QVector3D(static_cast<float>(axisColor.redF()),
                                                            static_cast<float>(axisColor.greenF()),
                                                            static_cast<float>(axisColor.blueF())));
        glDrawArrays(GL_LINES, 0, static_cast<int>(lineVerts.size() / 3));
    }
    if (!pointVerts.empty()) {
        overlayVbo_.allocate(pointVerts.data(), static_cast<int>(pointVerts.size() * sizeof(float)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
        flatProgram_->setUniformValue("uColor", QVector3D(static_cast<float>(highlightColor.redF()),
                                                            static_cast<float>(highlightColor.greenF()),
                                                            static_cast<float>(highlightColor.blueF())));
        glDrawArrays(GL_POINTS, 0, static_cast<int>(pointVerts.size() / 3));
    }

    overlayVbo_.release();
    overlayVao_.release();
    flatProgram_->release();
}

void MeshPreviewWidget::pickAt(const QPoint& pos) {
    if (!bvh_) return;
    float x = width() > 0 ? (2.0f * static_cast<float>(pos.x())) / static_cast<float>(width()) - 1.0f : 0.0f;
    float y = height() > 0 ? 1.0f - (2.0f * static_cast<float>(pos.y())) / static_cast<float>(height()) : 0.0f;

    QMatrix4x4 vp = projectionMatrix() * viewMatrix();
    bool invertible = false;
    QMatrix4x4 inv = vp.inverted(&invertible);
    if (!invertible) return;

    QVector4D nearPoint = inv * QVector4D(x, y, -1.0f, 1.0f);
    QVector4D farPoint = inv * QVector4D(x, y, 1.0f, 1.0f);
    if (qFuzzyIsNull(nearPoint.w()) || qFuzzyIsNull(farPoint.w())) return;
    nearPoint /= nearPoint.w();
    farPoint /= farPoint.w();

    QVector3D origin = nearPoint.toVector3D();
    QVector3D dir = (farPoint.toVector3D() - origin).normalized();

    cfd::mesh::Vec3 o{origin.x(), origin.y(), origin.z()};
    cfd::mesh::Vec3 d{dir.x(), dir.y(), dir.z()};
    auto hit = bvh_->nearest_hit(o, d);
    if (hit) {
        emit pointPicked(hit->point, hit->triangle_index);
    }
}

void MeshPreviewWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (pickMode_) {
            pickAt(event->pos());
        } else {
            dragging_ = true;
            lastMousePos_ = event->pos();
        }
    }
}

void MeshPreviewWidget::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->pos() - lastMousePos_;
        lastMousePos_ = event->pos();
        yaw_ += static_cast<float>(delta.x()) * 0.4f;
        pitch_ = std::clamp(pitch_ - static_cast<float>(delta.y()) * 0.4f, -89.0f, 89.0f);
        update();
    }
}

void MeshPreviewWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) dragging_ = false;
}

void MeshPreviewWidget::wheelEvent(QWheelEvent* event) {
    float factor = event->angleDelta().y() > 0 ? 0.9f : 1.1f;
    distance_ = std::clamp(distance_ * factor, radius_ * 0.5f, radius_ * 20.0f);
    update();
    event->accept();
}
