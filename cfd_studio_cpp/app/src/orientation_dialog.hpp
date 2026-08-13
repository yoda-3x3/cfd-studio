#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <QDialog>
#include <QString>

#include "mesh/mesh.hpp"
#include "solvers/orientation.hpp"
#include "theme.hpp"
#include "widgets/mesh_preview_widget.hpp"

class QRadioButton;
class QButtonGroup;
class QLabel;
class QCheckBox;
class QPushButton;

// Port of ui/orientation_dialog.py's OrientationDialog -- lets the user
// accept one of the 3 auto-suggested candidate flow axes (optionally
// reversed), or click a point directly on the mesh to derive the flow
// axis from a picked leading edge instead. Unlike the Python original,
// the preview is a real QOpenGLWidget (MeshPreviewWidget) at full mesh
// resolution with real ray-cast picking, not a decimated mplot3d view
// with a pick_event/proj3d hack.
class OrientationDialog : public QDialog {
    Q_OBJECT

public:
    OrientationDialog(const cfd::mesh::Mesh& mesh, const QString& meshName, const Theme& theme,
                       QWidget* parent = nullptr);

    // Valid only after exec() returns QDialog::Accepted -- the reoriented
    // mesh, at full resolution.
    [[nodiscard]] const std::optional<cfd::mesh::Mesh>& confirmedMesh() const { return confirmedMesh_; }

private slots:
    void onModeToggled();
    void onCandidateSelected(int index);
    void onReverseToggled(bool checked);
    void onPointPicked(cfd::mesh::Vec3 point, std::uint32_t triangleIndex);
    void onClearPick();
    void onAccept();

private:
    void buildUi();
    void updatePreview();
    [[nodiscard]] std::optional<cfd::solvers::OrientationCandidate> currentCandidate() const;

    cfd::mesh::Mesh mesh_;
    QString meshName_;
    Theme theme_;
    std::array<cfd::solvers::OrientationCandidate, 3> candidates_;
    int selectedCandidateIndex_ = 0;
    bool pickMode_ = false;
    bool reversed_ = false;
    std::optional<cfd::mesh::Vec3> pickedPoint_;

    std::optional<cfd::mesh::Mesh> confirmedMesh_;

    MeshPreviewWidget* preview_ = nullptr;
    QRadioButton* autoModeRadio_ = nullptr;
    QRadioButton* pickModeRadio_ = nullptr;
    QWidget* autoPanel_ = nullptr;
    QWidget* pickPanel_ = nullptr;
    QButtonGroup* candidateGroup_ = nullptr;
    QLabel* warningLabel_ = nullptr;
    QLabel* pickStatusLabel_ = nullptr;
    QPushButton* clearPickButton_ = nullptr;
    QCheckBox* reverseCheckbox_ = nullptr;
};
