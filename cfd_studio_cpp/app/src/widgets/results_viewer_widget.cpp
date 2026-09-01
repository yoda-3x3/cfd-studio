#include "results_viewer_widget.hpp"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QMouseEvent>
#include <QVector2D>
#include <QWheelEvent>

namespace {
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

// Reused verbatim from MeshPreviewWidget -- same lit single-directional-
// light shader for the object surface.
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

const char* kSliceVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uMvp;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

// Reproduces PlotWidget::colormapSample()'s exact 5-stop viridis-like LUT
// (app/src/widgets/plot_widget.cpp) via a cascading-mix chain, so the
// slice plane reads consistently with the app's existing 2D heatmaps.
// Cascading clamp(t4-i,0,1) mixes is a standard piecewise-linear
// multi-stop gradient technique: at any t4 in [i,i+1], every mix before
// the i-th has already fully applied (clamp==1) and every one after is a
// no-op (clamp==0), leaving exactly mix(c_i, c_{i+1}, frac) as the result
// -- chosen over uniform-array indexing (uStops[i]) to sidestep any
// question of dynamic uniform-array-index support on older GL 3.3
// drivers.
const char* kSliceFragmentShader = R"(
#version 330 core
in vec2 vUv;
uniform sampler2D uField;
uniform vec2 uValueRange;
out vec4 fragColor;
vec3 colormap(float t) {
    const vec3 c0 = vec3(0.26667, 0.00392, 0.32941);
    const vec3 c1 = vec3(0.23137, 0.32157, 0.54510);
    const vec3 c2 = vec3(0.12941, 0.56863, 0.54902);
    const vec3 c3 = vec3(0.36863, 0.78824, 0.38431);
    const vec3 c4 = vec3(0.99216, 0.90588, 0.14510);
    float t4 = clamp(t, 0.0, 1.0) * 4.0;
    vec3 col = c0;
    col = mix(col, c1, clamp(t4 - 0.0, 0.0, 1.0));
    col = mix(col, c2, clamp(t4 - 1.0, 0.0, 1.0));
    col = mix(col, c3, clamp(t4 - 2.0, 0.0, 1.0));
    col = mix(col, c4, clamp(t4 - 3.0, 0.0, 1.0));
    return col;
}
void main() {
    float raw = texture(uField, vUv).r;
    float range = max(uValueRange.y - uValueRange.x, 1e-12);
    float t = (raw - uValueRange.x) / range;
    fragColor = vec4(colormap(t), 1.0);
}
)";
} // namespace

ResultsViewerWidget::ResultsViewerWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setMouseTracking(true);
}

ResultsViewerWidget::~ResultsViewerWidget() {
    makeCurrent();
    meshVbo_.destroy();
    sliceVbo_.destroy();
    if (sliceTexture_ != 0) glDeleteTextures(1, &sliceTexture_);
    doneCurrent();
}

void ResultsViewerWidget::setTheme(const Theme& theme) {
    theme_ = theme;
    update();
}

void ResultsViewerWidget::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);

    meshProgram_ = std::make_unique<QOpenGLShaderProgram>();
    meshProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kMeshVertexShader);
    meshProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kMeshFragmentShader);
    meshProgram_->link();

    sliceProgram_ = std::make_unique<QOpenGLShaderProgram>();
    sliceProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kSliceVertexShader);
    sliceProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kSliceFragmentShader);
    sliceProgram_->link();

    meshVao_.create();
    sliceVao_.create();
    sliceVbo_.create();

    glGenTextures(1, &sliceTexture_);
    glBindTexture(GL_TEXTURE_2D, sliceTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void ResultsViewerWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void ResultsViewerWidget::setMesh(const cfd::mesh::Mesh& mesh) {
    mesh_ = mesh;
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

void ResultsViewerWidget::setResultsCache(std::shared_ptr<cfd::io::ResultsCacheReader> reader) {
    reader_ = std::move(reader);
    haveFrame_ = false;
    setFrame(0);
}

int ResultsViewerWidget::frameCount() const {
    return reader_ ? reader_->frame_count() : 0;
}

double ResultsViewerWidget::frameTime(int index) const {
    return reader_ ? reader_->frame_time(index) : 0.0;
}

void ResultsViewerWidget::setFrame(int index) {
    if (!reader_ || index < 0 || index >= reader_->frame_count()) return;
    // A load failure here (e.g. a truncated frame file) is treated as a
    // no-op rather than propagated -- this can be called from a slider's
    // valueChanged signal, and letting an exception cross back out through
    // Qt's event dispatch is not something to risk over one bad frame.
    try {
        currentFrame_ = reader_->load_frame(index);
    } catch (const std::exception&) {
        return;
    }
    frameIndex_ = index;
    haveFrame_ = true;
    sliceDirty_ = true;
    update();
}

void ResultsViewerWidget::setScalarField(ScalarField field) {
    field_ = field;
    sliceDirty_ = true;
    update();
}

void ResultsViewerWidget::setSliceAxis(Axis axis) {
    axis_ = axis;
    sliceDirty_ = true;
    update();
}

void ResultsViewerWidget::setSlicePosition(double t) {
    slicePosition_ = std::clamp(t, 0.0, 1.0);
    sliceDirty_ = true;
    update();
}

void ResultsViewerWidget::rebuildMeshGeometry() {
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
    meshVertexCount_ = static_cast<int>(mesh_.triangles.size() * 3);

    meshVao_.bind();
    if (!meshVbo_.isCreated()) meshVbo_.create();
    meshVbo_.bind();
    meshVbo_.allocate(vertexData.data(), static_cast<int>(vertexData.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    meshVbo_.release();
    meshVao_.release();

    meshDirty_ = false;
}

void ResultsViewerWidget::rebuildSlice() {
    sliceDirty_ = false;
    if (!haveFrame_ || !reader_) return;

    int nx = reader_->nx(), ny = reader_->ny(), nz = reader_->nz();
    double Lx = nx * reader_->dx(), Ly = ny * reader_->dy(), Lz = nz * reader_->dz();

    const std::vector<double>* field = nullptr;
    switch (field_) {
        case ScalarField::VelocityMagnitude: field = &currentFrame_.velocity_magnitude; break;
        case ScalarField::Pressure: field = &currentFrame_.pressure; break;
        case ScalarField::VelocityU: field = &currentFrame_.velocity_u; break;
        case ScalarField::VelocityV: field = &currentFrame_.velocity_v; break;
        case ScalarField::VelocityW: field = &currentFrame_.velocity_w; break;
    }

    // i-slowest/k-fastest, same convention as Fields3D everywhere else
    // (core/grid_index.hpp's idx3, unpadded variant -- see
    // pipeline/src/run_3d.cpp's detail::extract_preview_slice for the
    // exact same formula used there).
    auto idx = [ny, nz](int i, int j, int k) {
        return static_cast<std::size_t>(i) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz)
             + static_cast<std::size_t>(j) * static_cast<std::size_t>(nz) + static_cast<std::size_t>(k);
    };

    int texW = 0, texH = 0;
    std::vector<float> corners; // 4 verts * (x,y,z,u,v)
    std::vector<float> texel;   // texW*texH, row-major, row 0 = v=0

    auto extract = [&](int width, int height, auto sample) {
        texW = width;
        texH = height;
        texel.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                texel[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(col)]
                    = static_cast<float>(sample(col, row));
            }
        }
    };

    if (axis_ == Axis::X) {
        int i = std::clamp(static_cast<int>(std::lround(slicePosition_ * (nx - 1))), 0, nx - 1);
        double x = i * reader_->dx();
        extract(nz, ny, [&](int k, int j) { return (*field)[idx(i, j, k)]; }); // u<->k(nz), v<->j(ny)
        corners = {
            static_cast<float>(x), 0.0f, 0.0f, 0.0f, 0.0f,
            static_cast<float>(x), 0.0f, static_cast<float>(Lz), 1.0f, 0.0f,
            static_cast<float>(x), static_cast<float>(Ly), static_cast<float>(Lz), 1.0f, 1.0f,
            static_cast<float>(x), static_cast<float>(Ly), 0.0f, 0.0f, 1.0f,
        };
    } else if (axis_ == Axis::Y) {
        int j = std::clamp(static_cast<int>(std::lround(slicePosition_ * (ny - 1))), 0, ny - 1);
        double y = j * reader_->dy();
        extract(nx, nz, [&](int i, int k) { return (*field)[idx(i, j, k)]; }); // u<->i(nx), v<->k(nz)
        corners = {
            0.0f, static_cast<float>(y), 0.0f, 0.0f, 0.0f,
            static_cast<float>(Lx), static_cast<float>(y), 0.0f, 1.0f, 0.0f,
            static_cast<float>(Lx), static_cast<float>(y), static_cast<float>(Lz), 1.0f, 1.0f,
            0.0f, static_cast<float>(y), static_cast<float>(Lz), 0.0f, 1.0f,
        };
    } else {
        int k = std::clamp(static_cast<int>(std::lround(slicePosition_ * (nz - 1))), 0, nz - 1);
        double z = k * reader_->dz();
        extract(nx, ny, [&](int i, int j) { return (*field)[idx(i, j, k)]; }); // u<->i(nx), v<->j(ny)
        corners = {
            0.0f, 0.0f, static_cast<float>(z), 0.0f, 0.0f,
            static_cast<float>(Lx), 0.0f, static_cast<float>(z), 1.0f, 0.0f,
            static_cast<float>(Lx), static_cast<float>(Ly), static_cast<float>(z), 1.0f, 1.0f,
            0.0f, static_cast<float>(Ly), static_cast<float>(z), 0.0f, 1.0f,
        };
    }

    float vmin = texel.empty() ? 0.0f : texel.front();
    float vmax = vmin;
    for (float v : texel) {
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
    }

    sliceVao_.bind();
    sliceVbo_.bind();
    sliceVbo_.allocate(corners.data(), static_cast<int>(corners.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    sliceVbo_.release();
    sliceVao_.release();

    glBindTexture(GL_TEXTURE_2D, sliceTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, texW, texH, 0, GL_RED, GL_FLOAT, texel.data());
    sliceProgram_->bind();
    sliceProgram_->setUniformValue("uValueRange", QVector2D(vmin, vmax));
    sliceProgram_->release();

    haveSlice_ = true;
}

QMatrix4x4 ResultsViewerWidget::viewMatrix() const {
    float yawRad = yaw_ * kDegToRad;
    float pitchRad = pitch_ * kDegToRad;
    QVector3D eye(center_.x() + distance_ * std::cos(pitchRad) * std::sin(yawRad),
                  center_.y() + distance_ * std::sin(pitchRad),
                  center_.z() + distance_ * std::cos(pitchRad) * std::cos(yawRad));
    QMatrix4x4 view;
    view.lookAt(eye, center_, QVector3D(0, 1, 0));
    return view;
}

QMatrix4x4 ResultsViewerWidget::projectionMatrix() const {
    QMatrix4x4 proj;
    float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    proj.perspective(45.0f, aspect, std::max(0.01f, radius_ * 0.01f), std::max(radius_ * 100.0f, 10.0f));
    return proj;
}

void ResultsViewerWidget::paintGL() {
    QColor clearColor(theme_.plot_bg);
    glClearColor(static_cast<float>(clearColor.redF()), static_cast<float>(clearColor.greenF()),
                 static_cast<float>(clearColor.blueF()), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (meshDirty_ && haveMesh_) rebuildMeshGeometry();
    if (sliceDirty_) rebuildSlice();

    QMatrix4x4 mvp = projectionMatrix() * viewMatrix();

    if (haveMesh_) {
        QColor meshColor(theme_.accent);
        meshProgram_->bind();
        meshProgram_->setUniformValue("uMvp", mvp);
        meshProgram_->setUniformValue("uLightDir", QVector3D(-0.4f, -1.0f, -0.3f));
        meshProgram_->setUniformValue("uColor", QVector3D(static_cast<float>(meshColor.redF()),
                                                            static_cast<float>(meshColor.greenF()),
                                                            static_cast<float>(meshColor.blueF())));
        meshVao_.bind();
        glDrawArrays(GL_TRIANGLES, 0, meshVertexCount_);
        meshVao_.release();
        meshProgram_->release();
    }

    if (haveSlice_) {
        sliceProgram_->bind();
        sliceProgram_->setUniformValue("uMvp", mvp);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sliceTexture_);
        sliceProgram_->setUniformValue("uField", 0);
        sliceVao_.bind();
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        sliceVao_.release();
        sliceProgram_->release();
    }
}

void ResultsViewerWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        lastMousePos_ = event->pos();
    }
}

void ResultsViewerWidget::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->pos() - lastMousePos_;
        lastMousePos_ = event->pos();
        yaw_ += static_cast<float>(delta.x()) * 0.4f;
        pitch_ = std::clamp(pitch_ - static_cast<float>(delta.y()) * 0.4f, -89.0f, 89.0f);
        update();
    }
}

void ResultsViewerWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) dragging_ = false;
}

void ResultsViewerWidget::wheelEvent(QWheelEvent* event) {
    float factor = event->angleDelta().y() > 0 ? 0.9f : 1.1f;
    distance_ = std::clamp(distance_ * factor, radius_ * 0.5f, radius_ * 20.0f);
    update();
    event->accept();
}
