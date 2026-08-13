#include "main_window.hpp"

#include <cmath>

#include <QHBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), settings_("CFDStudio", "CFDStudio") {
    setWindowTitle("CFD Studio");
    resize(1280, 820);

    applyTheme(settings_.value("theme", kDefaultThemeKey).toString());

    // TEMPORARY 6.4 smoke test: real main window content (2D tab etc.)
    // lands in Phase 6.5 -- this just proves PlotWidget actually paints
    // correctly before building the rest of the app on top of it.
    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    auto* heatmap = new PlotWidget(PlotWidget::Mode::Heatmap, central);
    auto* logLine = new PlotWidget(PlotWidget::Mode::LogLine, central);
    layout->addWidget(heatmap);
    layout->addWidget(logLine);
    setCentralWidget(central);

    int nx = 60, ny = 40;
    std::vector<double> values(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny));
    std::vector<float> obstacle(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny), 0.0f);
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            double x = static_cast<double>(i) / nx, y = static_cast<double>(j) / ny;
            std::size_t k = static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(i);
            values[k] = std::sin(x * 6.28) * std::cos(y * 6.28) + 0.3 * x;
            // A circular "obstacle" in the middle, to check the overlay.
            if ((x - 0.5) * (x - 0.5) + (y - 0.5) * (y - 0.5) < 0.02) {
                obstacle[k] = 1.0f;
            }
        }
    }
    heatmap->setHeatmapData(values, nx, ny, "velocity_magnitude");
    heatmap->setObstacleMask(obstacle);

    for (int step = 1; step <= 200; ++step) {
        logLine->appendResidualPoint(step, 1.0 * std::exp(-step / 40.0) + 1e-6);
    }
}

void MainWindow::applyTheme(const QString& key) {
    const Theme& theme = theme_by_key(key);
    setStyleSheet(build_stylesheet(theme));
    currentThemeKey_ = theme.key;
    settings_.setValue("theme", theme.key);
}
