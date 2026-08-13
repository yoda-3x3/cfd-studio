#include "tab_3d.hpp"

#include <algorithm>
#include <string>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "mesh/mesh.hpp"
#include "orientation_dialog.hpp"
#include "paraview_launcher.hpp"
#include "solvers/kernel_backend.hpp"
#include "solvers/materials.hpp"
#include "solvers/performance_presets_3d.hpp"
#include "widgets/plot_widget.hpp"

ThreeDPanel::ThreeDPanel(QWidget* parent) : QWidget(parent), settings_("CFDStudio", "CFDStudio") {
    buildUi();
}

void ThreeDPanel::setTheme(const Theme& theme) {
    currentTheme_ = theme;
    xyVelocityPlot_->setTheme(theme);
    xyPressurePlot_->setTheme(theme);
    xzVelocityPlot_->setTheme(theme);
    residualPlot_->setTheme(theme);
}

void ThreeDPanel::shutdown() {
    if (worker_) worker_->requestStop();
    if (thread_ && thread_->isRunning()) {
        thread_->quit();
        thread_->wait(3000);
    }
}

void ThreeDPanel::buildUi() {
    auto* root = new QHBoxLayout(this);

    auto* leftPanel = new QWidget(this);
    leftPanel->setFixedWidth(320);
    auto* leftLayout = new QVBoxLayout(leftPanel);

    // ---- Geometry group ----
    auto* geoGroup = new QGroupBox("3D Geometry", leftPanel);
    auto* geoLayout = new QVBoxLayout(geoGroup);

    auto* flowModeRow = new QWidget(geoGroup);
    auto* flowModeLayout = new QVBoxLayout(flowModeRow);
    flowModeLayout->setContentsMargins(0, 0, 0, 0);
    flowModeExternalRadio_ = new QRadioButton("External (object in tunnel)", flowModeRow);
    flowModeExternalRadio_->setChecked(true);
    flowModeInternalRadio_ = new QRadioButton("Internal (pipe/duct)", flowModeRow);
    flowModeLayout->addWidget(flowModeExternalRadio_);
    flowModeLayout->addWidget(flowModeInternalRadio_);
    geoLayout->addWidget(flowModeRow);

    uploadButton_ = new QPushButton("Upload 3D File...", geoGroup);
    reorientButton_ = new QPushButton("Re-check Orientation...", geoGroup);
    reorientButton_->setEnabled(false);
    geoLayout->addWidget(uploadButton_);
    geoLayout->addWidget(reorientButton_);
    connect(uploadButton_, &QPushButton::clicked, this, &ThreeDPanel::onBrowseMeshFile);
    connect(reorientButton_, &QPushButton::clicked, this, &ThreeDPanel::onReopenOrientationDialog);

    meshInfoLabel_ = new QLabel("No mesh loaded.", geoGroup);
    meshInfoLabel_->setObjectName("description");
    meshInfoLabel_->setWordWrap(true);
    geoLayout->addWidget(meshInfoLabel_);

    auto* gapForm = new QFormLayout();
    inflowGapSpin_ = new QDoubleSpinBox(geoGroup);
    inflowGapSpin_->setRange(0, 20);
    inflowGapSpin_->setValue(1.5);
    wakeGapSpin_ = new QDoubleSpinBox(geoGroup);
    wakeGapSpin_->setRange(0, 40);
    wakeGapSpin_->setValue(4.0);
    lateralGapSpin_ = new QDoubleSpinBox(geoGroup);
    lateralGapSpin_->setRange(0.2, 20);
    lateralGapSpin_->setValue(1.5);
    gapForm->addRow("Inflow gap:", inflowGapSpin_);
    gapForm->addRow("Wake gap:", wakeGapSpin_);
    gapForm->addRow("Lateral gap:", lateralGapSpin_);
    geoLayout->addLayout(gapForm);

    leftLayout->addWidget(geoGroup);

    // ---- Case setup group ----
    auto* caseGroup = new QGroupBox("Case Setup", leftPanel);
    auto* caseForm = new QFormLayout(caseGroup);

    perfPresetCombo_ = new QComboBox(caseGroup);
    for (const auto& key : cfd::solvers::performance_preset_keys_3d()) {
        perfPresetCombo_->addItem(QString::fromStdString(key), QString::fromStdString(key));
    }
    perfPresetCombo_->setCurrentText(QString::fromStdString(cfd::solvers::kDefaultPerformancePreset3D));
    caseForm->addRow("Performance preset:", perfPresetCombo_);
    connect(perfPresetCombo_, &QComboBox::currentIndexChanged, this, &ThreeDPanel::onPerformancePresetChanged);

    perfPresetInfoLabel_ = new QLabel(caseGroup);
    perfPresetInfoLabel_->setObjectName("description");
    perfPresetInfoLabel_->setWordWrap(true);
    caseForm->addRow(perfPresetInfoLabel_);

    auto* gridRow = new QWidget(caseGroup);
    auto* gridLayout = new QHBoxLayout(gridRow);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    nxSpin_ = new QSpinBox(gridRow);
    nxSpin_->setRange(8, 300);
    nxSpin_->setMinimumWidth(60);
    nySpin_ = new QSpinBox(gridRow);
    nySpin_->setRange(8, 300);
    nySpin_->setMinimumWidth(60);
    nzSpin_ = new QSpinBox(gridRow);
    nzSpin_->setRange(8, 300);
    nzSpin_->setMinimumWidth(60);
    gridLayout->addWidget(nxSpin_);
    gridLayout->addWidget(new QLabel("x", gridRow));
    gridLayout->addWidget(nySpin_);
    gridLayout->addWidget(new QLabel("x", gridRow));
    gridLayout->addWidget(nzSpin_);
    caseForm->addRow("Grid:", gridRow);

    auto* reModeRow = new QWidget(caseGroup);
    auto* reModeLayout = new QHBoxLayout(reModeRow);
    reModeLayout->setContentsMargins(0, 0, 0, 0);
    reModeDirectRadio_ = new QRadioButton("Directly", reModeRow);
    reModeDirectRadio_->setChecked(true);
    reModePhysicalRadio_ = new QRadioButton("From physical properties", reModeRow);
    reModeLayout->addWidget(reModeDirectRadio_);
    reModeLayout->addWidget(reModePhysicalRadio_);
    caseForm->addRow("Reynolds number:", reModeRow);
    connect(reModeDirectRadio_, &QRadioButton::toggled, this, &ThreeDPanel::onReModeToggled);

    reSpin_ = new QDoubleSpinBox(caseGroup);
    reSpin_->setRange(1, 10000000);
    reSpin_->setValue(200.0);
    caseForm->addRow("Re:", reSpin_);

    materialPanel_ = new QWidget(caseGroup);
    auto* materialForm = new QFormLayout(materialPanel_);
    materialForm->setContentsMargins(0, 0, 0, 0);
    materialCombo_ = new QComboBox(materialPanel_);
    for (const auto& key : cfd::solvers::material_preset_keys()) {
        const auto& preset = cfd::solvers::material_preset(key);
        materialCombo_->addItem(QString::fromStdString(preset.label), QString::fromStdString(key));
    }
    charLengthSpin_ = new QDoubleSpinBox(materialPanel_);
    charLengthSpin_->setRange(0.001, 1000);
    charLengthSpin_->setValue(1.0);
    charLengthSpin_->setDecimals(4);
    velocitySpin_ = new QDoubleSpinBox(materialPanel_);
    velocitySpin_->setRange(0.001, 1000);
    velocitySpin_->setValue(1.0);
    velocitySpin_->setDecimals(4);
    materialForm->addRow("Fluid:", materialCombo_);
    materialForm->addRow("Char. length (m):", charLengthSpin_);
    materialForm->addRow("Velocity (m/s):", velocitySpin_);
    caseForm->addRow(materialPanel_);
    materialPanel_->setVisible(false);
    connect(materialCombo_, &QComboBox::currentIndexChanged, this, &ThreeDPanel::recomputePhysicalRe);
    connect(charLengthSpin_, &QDoubleSpinBox::valueChanged, this, &ThreeDPanel::recomputePhysicalRe);
    connect(velocitySpin_, &QDoubleSpinBox::valueChanged, this, &ThreeDPanel::recomputePhysicalRe);

    uSpin_ = new QDoubleSpinBox(caseGroup);
    uSpin_->setRange(0.001, 1000);
    uSpin_->setValue(1.0);
    caseForm->addRow("Inflow speed:", uSpin_);

    int maxThreads = std::max(1, cfd::solvers::threaded_backend().max_thread_count());
    threadsSpin_ = new QSpinBox(caseGroup);
    threadsSpin_->setRange(1, maxThreads);
    threadsSpin_->setValue(std::min(8, maxThreads));
    caseForm->addRow(QString("CPU threads (max %1):").arg(maxThreads), threadsSpin_);

    auto* threadsNote = new QLabel(
        "The pressure solve is parallelized across cores, but this kind of "
        "stencil computation is memory-bandwidth-bound: expect a real but "
        "modest speedup (often best around 4-8 threads) rather than linear "
        "scaling to every core.",
        caseGroup);
    threadsNote->setObjectName("description");
    threadsNote->setWordWrap(true);
    caseForm->addRow(threadsNote);

    leftLayout->addWidget(caseGroup);

    // ---- Run group ----
    auto* runGroup = new QGroupBox("Run", leftPanel);
    auto* runForm = new QFormLayout(runGroup);
    stepsSpin_ = new QSpinBox(runGroup);
    stepsSpin_->setRange(1, 1000000);
    stepsSpin_->setValue(1500);
    outputEverySpin_ = new QSpinBox(runGroup);
    outputEverySpin_->setRange(1, 5000);
    outputEverySpin_->setValue(25);
    runForm->addRow("Steps:", stepsSpin_);
    runForm->addRow("Output every:", outputEverySpin_);

    auto* outDirRow = new QWidget(runGroup);
    auto* outDirLayout = new QHBoxLayout(outDirRow);
    outDirLayout->setContentsMargins(0, 0, 0, 0);
    outputDirEdit_ = new QLineEdit(outDirRow);
    auto* browseButton = new QPushButton("Browse...", outDirRow);
    outDirLayout->addWidget(outputDirEdit_);
    outDirLayout->addWidget(browseButton);
    runForm->addRow("Output folder:", outDirRow);
    connect(browseButton, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Output Folder", outputDirEdit_->text());
        if (!dir.isEmpty()) outputDirEdit_->setText(dir);
    });

    forceRerunCheckbox_ = new QCheckBox("Force re-run (ignore cache)", runGroup);
    runForm->addRow(forceRerunCheckbox_);

    auto* buttonRow = new QWidget(runGroup);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    runButton_ = new QPushButton("Run Simulation", buttonRow);
    stopButton_ = new QPushButton("Stop", buttonRow);
    stopButton_->setObjectName("stopButton");
    stopButton_->setEnabled(false);
    buttonLayout->addWidget(runButton_);
    buttonLayout->addWidget(stopButton_);
    runForm->addRow(buttonRow);
    connect(runButton_, &QPushButton::clicked, this, &ThreeDPanel::onRunClicked);
    connect(stopButton_, &QPushButton::clicked, this, &ThreeDPanel::onStopClicked);

    progressBar_ = new QProgressBar(runGroup);
    progressBar_->setRange(0, 100);
    runForm->addRow(progressBar_);

    statusLabel_ = new QLabel("Idle.", runGroup);
    statusLabel_->setWordWrap(true);
    runForm->addRow(statusLabel_);

    paraviewButton_ = new QPushButton("Open Latest Result in ParaView", runGroup);
    paraviewButton_->setObjectName("paraviewButton");
    paraviewButton_->setEnabled(false);
    runForm->addRow(paraviewButton_);
    connect(paraviewButton_, &QPushButton::clicked, this, &ThreeDPanel::onOpenInParaView);

    leftLayout->addWidget(runGroup);
    leftLayout->addStretch();

    // ---- Right panel: 2x2 plot grid ----
    auto* rightPanel = new QWidget(this);
    auto* rightGrid = new QVBoxLayout(rightPanel);
    auto* row1 = new QHBoxLayout();
    auto* row2 = new QHBoxLayout();
    xyVelocityPlot_ = new PlotWidget(PlotWidget::Mode::Heatmap, rightPanel);
    xyPressurePlot_ = new PlotWidget(PlotWidget::Mode::Heatmap, rightPanel);
    xzVelocityPlot_ = new PlotWidget(PlotWidget::Mode::Heatmap, rightPanel);
    residualPlot_ = new PlotWidget(PlotWidget::Mode::LogLine, rightPanel);
    row1->addWidget(xyVelocityPlot_);
    row1->addWidget(xyPressurePlot_);
    row2->addWidget(xzVelocityPlot_);
    row2->addWidget(residualPlot_);
    rightGrid->addLayout(row1);
    rightGrid->addLayout(row2);

    root->addWidget(leftPanel);
    root->addWidget(rightPanel, 1);

    onPerformancePresetChanged();
    onReModeToggled();
}

void ThreeDPanel::onPerformancePresetChanged() {
    QString key = perfPresetCombo_->currentData().toString();
    if (key.isEmpty()) return;
    std::string keyStd = key.toStdString();
    const auto& preset = cfd::solvers::performance_preset_3d(keyStd);
    nxSpin_->setValue(preset.nx);
    nySpin_->setValue(preset.ny);
    nzSpin_->setValue(preset.nz);
    stepsSpin_->setValue(preset.steps);
    outputEverySpin_->setValue(preset.output_every);
    perfPresetInfoLabel_->setText(
        QString("Grid %1x%2x%3, %4 steps, output every %5").arg(preset.nx).arg(preset.ny).arg(preset.nz).arg(preset.steps).arg(preset.output_every));
}

void ThreeDPanel::onReModeToggled() {
    bool physical = reModePhysicalRadio_->isChecked();
    reSpin_->setReadOnly(physical);
    materialPanel_->setVisible(physical);
    if (physical) recomputePhysicalRe();
}

void ThreeDPanel::recomputePhysicalRe() {
    if (!reModePhysicalRadio_->isChecked()) return;
    QString key = materialCombo_->currentData().toString();
    if (key.isEmpty()) return;
    std::string keyStd = key.toStdString();
    const auto& material = cfd::solvers::material_preset(keyStd);
    double re = cfd::solvers::reynolds_number(velocitySpin_->value(), charLengthSpin_->value(), material.nu);
    reSpin_->setValue(re);
}

void ThreeDPanel::suggestOutputDir() {
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString dir = QDir::current().filePath(QString("runs/custom3d_%1").arg(timestamp));
    outputDirEdit_->setText(QDir::toNativeSeparators(dir));
}

void ThreeDPanel::onBrowseMeshFile() {
    QString path = QFileDialog::getOpenFileName(this, "Upload 3D Geometry", QString(),
                                                  "3D Mesh Files (*.stl *.obj *.ply *.off);;All Files (*.*)");
    if (path.isEmpty()) return;
    try {
        cfd::mesh::Mesh mesh = cfd::mesh::load_mesh(path.toStdString());
        meshPath_ = path;
        rawMesh_ = mesh;
        reorientButton_->setEnabled(true);
        runOrientationDialog(mesh, path);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Load Failed", QString("Could not load mesh: %1").arg(e.what()));
    }
}

void ThreeDPanel::onReopenOrientationDialog() {
    if (!rawMesh_) return;
    runOrientationDialog(*rawMesh_, meshPath_);
}

void ThreeDPanel::runOrientationDialog(const cfd::mesh::Mesh& mesh, const QString& path) {
    OrientationDialog dlg(mesh, QFileInfo(path).fileName(), currentTheme_, this);
    if (dlg.exec() == QDialog::Accepted && dlg.confirmedMesh()) {
        orientedMesh_ = dlg.confirmedMesh();
    } else {
        orientedMesh_ = mesh; // cancel => fall back to un-rotated mesh, matches ui/tab_3d.py
    }
    auto bounds = orientedMesh_->bounds();
    auto ext = bounds.extents();
    meshInfoLabel_->setText(QString("%1 — %2 triangles, extents (%3, %4, %5)")
                                 .arg(QFileInfo(path).fileName())
                                 .arg(orientedMesh_->triangles.size())
                                 .arg(ext.x, 0, 'g', 3)
                                 .arg(ext.y, 0, 'g', 3)
                                 .arg(ext.z, 0, 'g', 3));
    suggestOutputDir();
}

void ThreeDPanel::setControlsEnabled(bool running) {
    uploadButton_->setEnabled(!running);
    reorientButton_->setEnabled(!running && rawMesh_.has_value());
    flowModeExternalRadio_->setEnabled(!running);
    flowModeInternalRadio_->setEnabled(!running);
    inflowGapSpin_->setEnabled(!running);
    wakeGapSpin_->setEnabled(!running);
    lateralGapSpin_->setEnabled(!running);
    perfPresetCombo_->setEnabled(!running);
    nxSpin_->setEnabled(!running);
    nySpin_->setEnabled(!running);
    nzSpin_->setEnabled(!running);
    reModeDirectRadio_->setEnabled(!running);
    reModePhysicalRadio_->setEnabled(!running);
    reSpin_->setEnabled(!running && reModeDirectRadio_->isChecked());
    materialCombo_->setEnabled(!running);
    charLengthSpin_->setEnabled(!running);
    velocitySpin_->setEnabled(!running);
    uSpin_->setEnabled(!running);
    threadsSpin_->setEnabled(!running);
    stepsSpin_->setEnabled(!running);
    outputEverySpin_->setEnabled(!running);
    outputDirEdit_->setEnabled(!running);
    forceRerunCheckbox_->setEnabled(!running);
    runButton_->setEnabled(!running);
    stopButton_->setEnabled(running);
}

void ThreeDPanel::onRunClicked() {
    if (thread_ && thread_->isRunning()) return;
    if (!orientedMesh_) {
        statusLabel_->setText("Please upload a mesh first.");
        return;
    }
    if (outputDirEdit_->text().isEmpty()) {
        statusLabel_->setText("Please choose an output folder.");
        return;
    }

    cfd::pipeline::Run3DOptions opts;
    opts.domain_mode = flowModeInternalRadio_->isChecked() ? "internal" : "external";
    opts.nx = nxSpin_->value();
    opts.ny = nySpin_->value();
    opts.nz = nzSpin_->value();
    opts.Re = reSpin_->value();
    opts.U_in = uSpin_->value();
    opts.n_steps = stepsSpin_->value();
    opts.output_every = outputEverySpin_->value();
    opts.output_dir = outputDirEdit_->text().toStdString();
    opts.mesh_name = QFileInfo(meshPath_).fileName().toStdString();
    opts.num_threads = threadsSpin_->value();
    opts.inflow_gap = inflowGapSpin_->value();
    opts.wake_gap = wakeGapSpin_->value();
    opts.lateral_gap = lateralGapSpin_->value();
    opts.force_rerun = forceRerunCheckbox_->isChecked();

    xyVelocityPlot_->setHeatmapData({}, 0, 0);
    xyPressurePlot_->setHeatmapData({}, 0, 0);
    xzVelocityPlot_->setHeatmapData({}, 0, 0);
    residualPlot_->clearLine();
    paraviewButton_->setEnabled(false);

    worker_ = new Sim3DWorker(*orientedMesh_, opts);
    thread_ = new QThread(this);
    worker_->moveToThread(thread_);

    connect(thread_, &QThread::started, worker_, &Sim3DWorker::run);
    connect(worker_, &Sim3DWorker::progressChanged, this, &ThreeDPanel::onProgress);
    connect(worker_, &Sim3DWorker::previewReady, this, &ThreeDPanel::onPreview);
    connect(worker_, &Sim3DWorker::finished, this, &ThreeDPanel::onFinished);
    connect(worker_, &Sim3DWorker::stopped, this, &ThreeDPanel::onStopped);
    connect(worker_, &Sim3DWorker::errorOccurred, this, &ThreeDPanel::onErrorOccurred);
    connect(worker_, &Sim3DWorker::finished, thread_, &QThread::quit);
    connect(worker_, &Sim3DWorker::stopped, thread_, &QThread::quit);
    connect(worker_, &Sim3DWorker::errorOccurred, thread_, &QThread::quit);
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);

    setControlsEnabled(true);
    statusLabel_->setText("Running...");
    progressBar_->setValue(0);

    thread_->start();
}

void ThreeDPanel::onStopClicked() {
    if (worker_) worker_->requestStop();
}

void ThreeDPanel::onProgress(int step, double residual) {
    int total = stepsSpin_->value();
    int pct = total > 0 ? static_cast<int>(100.0 * step / total) : 0;
    progressBar_->setValue(pct);
    statusLabel_->setText(QString("Step %1 / %2, residual %3").arg(step).arg(total).arg(residual, 0, 'e', 3));
    residualPlot_->appendResidualPoint(step, residual);
}

void ThreeDPanel::onPreview(Preview3DSnapshot snapshot) {
    if (!snapshot.slice) return;
    const auto& s = *snapshot.slice;
    xyVelocityPlot_->setHeatmapData(s.velocity_magnitude_xy, s.nx, s.ny, "XY velocity_magnitude");
    xyVelocityPlot_->setObstacleMask(s.obstacle_xy);
    xyPressurePlot_->setHeatmapData(s.pressure_xy, s.nx, s.ny, "XY pressure");
    xyPressurePlot_->setObstacleMask(s.obstacle_xy);
    xzVelocityPlot_->setHeatmapData(s.velocity_magnitude_xz, s.nx, s.nz, "XZ velocity_magnitude");
    xzVelocityPlot_->setObstacleMask(s.obstacle_xz);
}

void ThreeDPanel::onFinished(QString foamPath, bool wasCached) {
    lastFoamPath_ = foamPath;
    statusLabel_->setText(wasCached ? QString("Reused cached run: %1").arg(foamPath)
                                     : QString("Simulation complete: %1").arg(foamPath));
    setControlsEnabled(false);
    paraviewButton_->setEnabled(true);
    progressBar_->setValue(100);
}

void ThreeDPanel::onStopped() {
    statusLabel_->setText("Stopped.");
    setControlsEnabled(false);
}

void ThreeDPanel::onErrorOccurred(QString message) {
    statusLabel_->setText(QString("Error: %1").arg(message));
    setControlsEnabled(false);
}

void ThreeDPanel::onOpenInParaView() {
    if (lastFoamPath_.isEmpty()) return;
    QString exe = settings_.value("paraviewPath").toString();
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        auto candidates = paraview_launcher::findCandidates();
        if (candidates.isEmpty()) {
            QMessageBox::warning(this, "ParaView Not Found",
                                  "Could not locate ParaView automatically. Please install it and try again.");
            return;
        }
        exe = candidates.first();
        settings_.setValue("paraviewPath", exe);
    }
    if (!paraview_launcher::launch(exe, lastFoamPath_)) {
        QMessageBox::warning(this, "Launch Failed", "Failed to launch ParaView.");
    }
}
