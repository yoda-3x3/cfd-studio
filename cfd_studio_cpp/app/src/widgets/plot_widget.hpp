#pragma once

#include <utility>
#include <vector>

#include <QColor>
#include <QString>
#include <QWidget>

#include "theme.hpp"

// Hand-rolled QPainter/QImage plotting -- replaces matplotlib's role from
// the Python app (ui/main_window.py, ui/tab_3d.py both used
// FigureCanvasQTAgg) with a native, dependency-free widget. Two modes:
// a scalar-field heatmap with colorbar and obstacle-mask overlay (2D tab's
// field plot, 3D tab's 4 mid-plane-slice panels), and a log-scale residual
// line plot. No streamline overlay -- a deliberate scope cut from the
// earlier validated spike (heatmap + colorbar + log-line only); the field
// heatmap + obstacle overlay already carries the flow-direction
// information streamlines would add.
class PlotWidget : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Heatmap, LogLine };

    explicit PlotWidget(Mode mode, QWidget* parent = nullptr);

    void setTheme(const Theme& theme);

    // Heatmap mode. `values`/`obstacle` are row-major (ny, nx) -- obstacle
    // cells (nonzero) are excluded from the auto color-range computation
    // and painted with the theme's border color instead of the colormap,
    // matching the Python app's obstacle-mask overlay.
    void setHeatmapData(const std::vector<double>& values, int nx, int ny, const QString& title = QString());
    void setObstacleMask(const std::vector<float>& obstacle);

    // LogLine mode. Points accumulate across calls; clearLine() resets for
    // a new run (call at the start of each simulation, mirroring how the
    // Python app's residual axes get cla()'d before each run).
    void appendResidualPoint(int step, double residual);
    void clearLine();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void paintHeatmap(QPainter& painter);
    void paintLogLine(QPainter& painter);

    Mode mode_;
    Theme theme_;

    std::vector<double> values_;
    std::vector<float> obstacle_;
    int nx_ = 0, ny_ = 0;
    QString title_;

    std::vector<std::pair<int, double>> residualPoints_;
};
