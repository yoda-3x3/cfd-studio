#include "orientation_dialog.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

OrientationDialog::OrientationDialog(const cfd::mesh::Mesh& mesh, const QString& meshName, const Theme& theme,
                                      QWidget* parent)
    : QDialog(parent), mesh_(mesh), meshName_(meshName), theme_(theme) {
    candidates_ = cfd::solvers::analyze_orientation(mesh_);
    buildUi();
}

void OrientationDialog::buildUi() {
    setWindowTitle("Confirm Flow Orientation");
    resize(880, 620);

    auto* root = new QHBoxLayout(this);

    preview_ = new MeshPreviewWidget(this);
    preview_->setMesh(mesh_);
    preview_->setTheme(theme_);
    root->addWidget(preview_, 1);
    connect(preview_, &MeshPreviewWidget::pointPicked, this, &OrientationDialog::onPointPicked);

    auto* side = new QWidget(this);
    side->setFixedWidth(300);
    auto* sideLayout = new QVBoxLayout(side);

    auto* titleLabel = new QLabel(meshName_, side);
    titleLabel->setStyleSheet("font-weight: 600;");
    sideLayout->addWidget(titleLabel);

    auto* modeRow = new QWidget(side);
    auto* modeLayout = new QHBoxLayout(modeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    autoModeRadio_ = new QRadioButton("Auto-suggest", modeRow);
    autoModeRadio_->setChecked(true);
    pickModeRadio_ = new QRadioButton("Pick leading edge on mesh", modeRow);
    modeLayout->addWidget(autoModeRadio_);
    modeLayout->addWidget(pickModeRadio_);
    sideLayout->addWidget(modeRow);
    connect(autoModeRadio_, &QRadioButton::toggled, this, &OrientationDialog::onModeToggled);
    connect(pickModeRadio_, &QRadioButton::toggled, this, &OrientationDialog::onModeToggled);

    autoPanel_ = new QWidget(side);
    auto* autoLayout = new QVBoxLayout(autoPanel_);
    autoLayout->setContentsMargins(0, 0, 0, 0);
    auto* disclaimer = new QLabel(
        "Suggested orientations are based on a simple geometric heuristic (smallest projected frontal area). "
        "Verify before running.",
        autoPanel_);
    disclaimer->setWordWrap(true);
    disclaimer->setObjectName("description");
    autoLayout->addWidget(disclaimer);

    candidateGroup_ = new QButtonGroup(this);
    for (int i = 0; i < 3; ++i) {
        const auto& c = candidates_[static_cast<std::size_t>(i)];
        QString text =
            QString("%1 — projected area %2").arg(QString::fromStdString(c.label)).arg(c.projected_area, 0, 'g', 4);
        auto* radio = new QRadioButton(text, autoPanel_);
        if (i == 0) radio->setChecked(true);
        candidateGroup_->addButton(radio, i);
        autoLayout->addWidget(radio);
    }
    connect(candidateGroup_, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (checked) onCandidateSelected(id);
    });

    warningLabel_ = new QLabel(autoPanel_);
    warningLabel_->setStyleSheet(QString("color: %1;").arg(theme_.danger));
    warningLabel_->setWordWrap(true);
    autoLayout->addWidget(warningLabel_);
    if (candidates_[0].projected_area > 0.0 &&
        candidates_[1].projected_area / candidates_[0].projected_area < 1.15) {
        warningLabel_->setText("⚠ The top two candidates are close in projected area — double check before running.");
    }

    sideLayout->addWidget(autoPanel_);

    pickPanel_ = new QWidget(side);
    auto* pickLayout = new QVBoxLayout(pickPanel_);
    pickLayout->setContentsMargins(0, 0, 0, 0);
    auto* pickDisclaimer = new QLabel(
        "Click a point on the mesh's leading edge (e.g. the nose or front tip) — flow direction is derived "
        "from that point toward the mesh's center.",
        pickPanel_);
    pickDisclaimer->setWordWrap(true);
    pickDisclaimer->setObjectName("description");
    pickLayout->addWidget(pickDisclaimer);
    pickStatusLabel_ = new QLabel("No point picked yet.", pickPanel_);
    pickLayout->addWidget(pickStatusLabel_);
    clearPickButton_ = new QPushButton("Clear Pick", pickPanel_);
    pickLayout->addWidget(clearPickButton_);
    connect(clearPickButton_, &QPushButton::clicked, this, &OrientationDialog::onClearPick);
    pickPanel_->setVisible(false);
    sideLayout->addWidget(pickPanel_);

    reverseCheckbox_ = new QCheckBox("Reverse flow direction (flip front/back)", side);
    connect(reverseCheckbox_, &QCheckBox::toggled, this, &OrientationDialog::onReverseToggled);
    sideLayout->addWidget(reverseCheckbox_);

    sideLayout->addStretch();

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, side);
    buttonBox->button(QDialogButtonBox::Ok)->setText("Use This Orientation");
    connect(buttonBox, &QDialogButtonBox::accepted, this, &OrientationDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    sideLayout->addWidget(buttonBox);

    root->addWidget(side);

    updatePreview();
}

void OrientationDialog::onModeToggled() {
    pickMode_ = pickModeRadio_->isChecked();
    autoPanel_->setVisible(!pickMode_);
    pickPanel_->setVisible(pickMode_);
    preview_->setPickMode(pickMode_);
    updatePreview();
}

void OrientationDialog::onCandidateSelected(int index) {
    selectedCandidateIndex_ = index;
    updatePreview();
}

void OrientationDialog::onReverseToggled(bool checked) {
    reversed_ = checked;
    updatePreview();
}

void OrientationDialog::onPointPicked(cfd::mesh::Vec3 point, std::uint32_t) {
    pickedPoint_ = point;
    pickStatusLabel_->setText(
        QString("Picked: (%1, %2, %3)").arg(point.x, 0, 'f', 3).arg(point.y, 0, 'f', 3).arg(point.z, 0, 'f', 3));
    preview_->setHighlightPoint(point);
    updatePreview();
}

void OrientationDialog::onClearPick() {
    pickedPoint_.reset();
    pickStatusLabel_->setText("No point picked yet.");
    preview_->setHighlightPoint(std::nullopt);
    updatePreview();
}

std::optional<cfd::solvers::OrientationCandidate> OrientationDialog::currentCandidate() const {
    cfd::solvers::OrientationCandidate base;
    if (pickMode_) {
        if (!pickedPoint_) return std::nullopt;
        cfd::mesh::Vec3 sum{0, 0, 0};
        for (const auto& v : mesh_.vertices) sum = sum + v;
        cfd::mesh::Vec3 centroid = sum * (1.0 / static_cast<double>(mesh_.vertices.size()));
        cfd::mesh::Vec3 flowAxis = cfd::mesh::normalize(centroid - *pickedPoint_);
        base = cfd::solvers::candidate_from_flow_axis(mesh_, flowAxis, "Picked leading edge");
    } else {
        base = candidates_[static_cast<std::size_t>(selectedCandidateIndex_)];
    }
    if (reversed_) {
        auto referenceAxes = cfd::solvers::principal_axes(mesh_);
        cfd::mesh::Vec3 reversedAxis = base.flow_axis * -1.0;
        base = cfd::solvers::candidate_from_flow_axis(mesh_, reversedAxis, base.label + "-reversed", base.rank,
                                                        &referenceAxes);
    }
    return base;
}

void OrientationDialog::updatePreview() {
    auto candidate = currentCandidate();
    preview_->setFlowAxis(candidate ? std::optional<cfd::mesh::Vec3>(candidate->flow_axis) : std::nullopt);
}

void OrientationDialog::onAccept() {
    auto candidate = currentCandidate();
    if (!candidate) candidate = candidates_[0]; // pick mode, nothing picked: fallback, matches Python
    confirmedMesh_ = cfd::solvers::apply_orientation(mesh_, *candidate);
    accept();
}
