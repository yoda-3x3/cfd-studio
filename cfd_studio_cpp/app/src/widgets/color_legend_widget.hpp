#pragma once

#include <QString>
#include <QWidget>

#include "theme.hpp"

// A vertical gradient color key (field name + min/max-labeled bar) for
// ResultsViewerWidget's slice plane -- QPainter-drawn, same colorbar
// layout/technique as PlotWidget's inline one (app/src/widgets/
// plot_widget.cpp's paintHeatmap), factored out as its own small widget
// here since it sits beside an OpenGL viewport rather than inside a
// QPainter-drawn plot.
class ColorLegendWidget : public QWidget {
    Q_OBJECT

public:
    explicit ColorLegendWidget(QWidget* parent = nullptr);

    void setTheme(const Theme& theme);
    void setFieldName(const QString& name);
    void setRange(double vmin, double vmax);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Theme theme_;
    QString fieldName_;
    double vmin_ = 0.0, vmax_ = 1.0;
};
