#pragma once

#include <optional>

#include <QSettings>
#include <QThread>
#include <QWidget>

#include "mesh/mesh.hpp"
#include "sim3d_worker.hpp"
#include "theme.hpp"

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QLabel;
class QRadioButton;
class QCheckBox;
class PlotWidget;

// Port of ui/tab_3d.py's ThreeDPanel. Two simplifications relative to
// Python, both documented as deliberate scope trims rather than gaps:
// the gap spinbox labels don't dynamically relabel between "clearance"
// (external) and "entrance/exit length" (internal) wording, and the
// physical-properties Re mode omits the "Custom fluid..." sentinel
// (material presets only) -- neither affects what a run actually does.
class ThreeDPanel : public QWidget {
    Q_OBJECT

public:
    explicit ThreeDPanel(QWidget* parent = nullptr);

    void setTheme(const Theme& theme);
    void shutdown();

private slots:
    void onBrowseMeshFile();
    void onReopenOrientationDialog();
    void onPerformancePresetChanged();
    void onReModeToggled();
    void onRunClicked();
    void onStopClicked();
    void onProgress(int step, double residual);
    void onPreview(Preview3DSnapshot snapshot);
    void onFinished(QString foamPath, bool wasCached, QString resultsCacheDir);
    void onStopped();
    void onErrorOccurred(QString message);
    void onOpenInParaView();
    void onViewResults();

private:
    void buildUi();
    void setControlsEnabled(bool running);
    void suggestOutputDir();
    void runOrientationDialog(const cfd::mesh::Mesh& mesh, const QString& path);
    void recomputePhysicalRe();

    // Geometry group
    QRadioButton* flowModeExternalRadio_ = nullptr;
    QRadioButton* flowModeInternalRadio_ = nullptr;
    QPushButton* uploadButton_ = nullptr;
    QPushButton* reorientButton_ = nullptr;
    QLabel* meshInfoLabel_ = nullptr;
    QDoubleSpinBox* inflowGapSpin_ = nullptr;
    QDoubleSpinBox* wakeGapSpin_ = nullptr;
    QDoubleSpinBox* lateralGapSpin_ = nullptr;
    QCheckBox* groundEffectCheckbox_ = nullptr;
    QDoubleSpinBox* altitudeGapSpin_ = nullptr;

    // Case setup
    QComboBox* perfPresetCombo_ = nullptr;
    QLabel* perfPresetInfoLabel_ = nullptr;
    QSpinBox* nxSpin_ = nullptr;
    QSpinBox* nySpin_ = nullptr;
    QSpinBox* nzSpin_ = nullptr;
    QRadioButton* reModeDirectRadio_ = nullptr;
    QRadioButton* reModePhysicalRadio_ = nullptr;
    QDoubleSpinBox* reSpin_ = nullptr;
    QWidget* materialPanel_ = nullptr;
    QComboBox* materialCombo_ = nullptr;
    QDoubleSpinBox* charLengthSpin_ = nullptr;
    QDoubleSpinBox* velocitySpin_ = nullptr;
    QDoubleSpinBox* uSpin_ = nullptr;
    QSpinBox* threadsSpin_ = nullptr;

    // Run group
    QSpinBox* stepsSpin_ = nullptr;
    QSpinBox* outputEverySpin_ = nullptr;
    QLineEdit* outputDirEdit_ = nullptr;
    QCheckBox* forceRerunCheckbox_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* paraviewButton_ = nullptr;
    QPushButton* viewResultsButton_ = nullptr;

    PlotWidget* xyVelocityPlot_ = nullptr;
    PlotWidget* xyPressurePlot_ = nullptr;
    PlotWidget* xzVelocityPlot_ = nullptr;
    PlotWidget* residualPlot_ = nullptr;

    QThread* thread_ = nullptr;
    Sim3DWorker* worker_ = nullptr;

    Theme currentTheme_ = theme_by_key(kDefaultThemeKey);

    QString meshPath_;
    std::optional<cfd::mesh::Mesh> rawMesh_;
    std::optional<cfd::mesh::Mesh> orientedMesh_;
    QString lastFoamPath_;
    QString lastResultsCacheDir_;
    QSettings settings_;
};
