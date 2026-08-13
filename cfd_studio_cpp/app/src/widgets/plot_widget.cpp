#include "plot_widget.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <QFontMetrics>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace {
// A simplified viridis-like colormap: 5 key stops, linearly interpolated
// in RGB between them. Not the real viridis LUT (256 precise entries),
// but perceptually similar and dependency-free.
constexpr int kStopCount = 5;
constexpr std::array<std::array<int, 3>, kStopCount> kColormapStops = {{
    {{68, 1, 84}},
    {{59, 82, 139}},
    {{33, 145, 140}},
    {{94, 201, 98}},
    {{253, 231, 37}},
}};
} // namespace

PlotWidget::PlotWidget(Mode mode, QWidget* parent) : QWidget(parent), mode_(mode) {
    theme_ = theme_by_key(kDefaultThemeKey);
    setMinimumSize(200, 150);
}

void PlotWidget::setTheme(const Theme& theme) {
    theme_ = theme;
    update();
}

void PlotWidget::setHeatmapData(const std::vector<double>& values, int nx, int ny, const QString& title) {
    values_ = values;
    nx_ = nx;
    ny_ = ny;
    title_ = title;
    update();
}

void PlotWidget::setObstacleMask(const std::vector<float>& obstacle) {
    obstacle_ = obstacle;
    update();
}

void PlotWidget::appendResidualPoint(int step, double residual) {
    residualPoints_.emplace_back(step, residual);
    update();
}

void PlotWidget::clearLine() {
    residualPoints_.clear();
    update();
}

QColor PlotWidget::colormapSample(double t) const {
    t = std::clamp(t, 0.0, 1.0);
    double scaled = t * (kStopCount - 1);
    int lo = std::clamp(static_cast<int>(std::floor(scaled)), 0, kStopCount - 2);
    double frac = scaled - lo;
    const auto& a = kColormapStops[static_cast<std::size_t>(lo)];
    const auto& b = kColormapStops[static_cast<std::size_t>(lo + 1)];
    int r = static_cast<int>(a[0] + frac * (b[0] - a[0]));
    int g = static_cast<int>(a[1] + frac * (b[1] - a[1]));
    int bl = static_cast<int>(a[2] + frac * (b[2] - a[2]));
    return {r, g, bl};
}

void PlotWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(theme_.plot_bg));

    if (mode_ == Mode::Heatmap) {
        paintHeatmap(painter);
    } else {
        paintLogLine(painter);
    }
}

void PlotWidget::paintHeatmap(QPainter& painter) {
    QColor fg(theme_.plot_fg);
    QColor gridColor(theme_.plot_grid);
    painter.setPen(fg);

    int titleHeight = title_.isEmpty() ? 0 : 22;
    if (!title_.isEmpty()) {
        painter.drawText(QRect(0, 2, width(), titleHeight), Qt::AlignHCenter | Qt::AlignTop, title_);
    }

    constexpr int kColorbarWidth = 24;
    constexpr int kColorbarLabelWidth = 60;
    constexpr int kMargin = 8;

    QRect plotRect(kMargin, titleHeight + kMargin, width() - 2 * kMargin - kColorbarWidth - kColorbarLabelWidth,
                    height() - titleHeight - 2 * kMargin);
    QRect colorbarRect(plotRect.right() + kMargin, plotRect.top(), kColorbarWidth, plotRect.height());

    if (nx_ <= 0 || ny_ <= 0 || values_.empty() || plotRect.width() <= 0 || plotRect.height() <= 0) {
        painter.setPen(gridColor);
        painter.drawRect(plotRect);
        return;
    }

    bool haveObstacle = obstacle_.size() == values_.size();

    double minVal = std::numeric_limits<double>::infinity();
    double maxVal = -std::numeric_limits<double>::infinity();
    for (std::size_t k = 0; k < values_.size(); ++k) {
        if (haveObstacle && obstacle_[k] != 0.0f) continue;
        double v = values_[k];
        if (!std::isfinite(v)) continue;
        minVal = std::min(minVal, v);
        maxVal = std::max(maxVal, v);
    }
    if (!std::isfinite(minVal) || !std::isfinite(maxVal)) {
        minVal = 0.0;
        maxVal = 1.0;
    }
    double range = (maxVal - minVal);
    if (range <= 0.0) range = 1.0;

    QColor obstacleColor(theme_.border);
    QImage image(nx_, ny_, QImage::Format_RGB32);
    for (int j = 0; j < ny_; ++j) {
        // Flip vertically: field row j=0 is the domain's y=0 (bottom),
        // matching the usual CFD/plotting convention of y increasing
        // upward, but QImage rows go top-to-bottom.
        int imgRow = ny_ - 1 - j;
        for (int i = 0; i < nx_; ++i) {
            std::size_t k = static_cast<std::size_t>(j) * static_cast<std::size_t>(nx_) + static_cast<std::size_t>(i);
            QColor color;
            if (haveObstacle && obstacle_[k] != 0.0f) {
                color = obstacleColor;
            } else {
                double v = std::isfinite(values_[k]) ? values_[k] : minVal;
                color = colormapSample((v - minVal) / range);
            }
            image.setPixelColor(i, imgRow, color);
        }
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(plotRect, image);
    painter.setPen(gridColor);
    painter.drawRect(plotRect);

    QLinearGradient gradient(colorbarRect.topLeft(), colorbarRect.bottomLeft());
    for (int s = 0; s <= 10; ++s) {
        double t = s / 10.0;
        gradient.setColorAt(1.0 - t, colormapSample(t)); // top of bar = max value
    }
    painter.fillRect(colorbarRect, gradient);
    painter.setPen(gridColor);
    painter.drawRect(colorbarRect);

    painter.setPen(fg);
    QFontMetrics fm(painter.font());
    QString maxLabel = QString::number(maxVal, 'g', 3);
    QString minLabel = QString::number(minVal, 'g', 3);
    painter.drawText(colorbarRect.right() + 4, colorbarRect.top() + fm.ascent(), maxLabel);
    painter.drawText(colorbarRect.right() + 4, colorbarRect.bottom(), minLabel);
}

void PlotWidget::paintLogLine(QPainter& painter) {
    QColor fg(theme_.plot_fg);
    QColor gridColor(theme_.plot_grid);
    QColor lineColor(theme_.accent);

    constexpr int kMarginLeft = 55;
    constexpr int kMarginRight = 12;
    constexpr int kMarginTop = 12;
    constexpr int kMarginBottom = 28;
    QRect plotRect(kMarginLeft, kMarginTop, width() - kMarginLeft - kMarginRight,
                    height() - kMarginTop - kMarginBottom);

    painter.setPen(gridColor);
    painter.drawRect(plotRect);

    if (residualPoints_.empty() || plotRect.width() <= 0 || plotRect.height() <= 0) return;

    constexpr double kEpsilon = 1e-300;
    int minStep = residualPoints_.front().first, maxStep = residualPoints_.back().first;
    double minLog = std::numeric_limits<double>::infinity();
    double maxLog = -std::numeric_limits<double>::infinity();
    for (const auto& [step, residual] : residualPoints_) {
        double clamped = std::max(residual, kEpsilon);
        double lg = std::log10(clamped);
        minLog = std::min(minLog, lg);
        maxLog = std::max(maxLog, lg);
        minStep = std::min(minStep, step);
        maxStep = std::max(maxStep, step);
    }
    if (maxLog - minLog < 1.0) {
        // Pad a near-flat residual history so the plot isn't a flat line
        // pinned to one edge.
        double mid = (maxLog + minLog) / 2.0;
        minLog = mid - 0.5;
        maxLog = mid + 0.5;
    }
    int stepSpan = std::max(maxStep - minStep, 1);

    auto toPixel = [&](int step, double residual) {
        double lg = std::log10(std::max(residual, kEpsilon));
        double xFrac = static_cast<double>(step - minStep) / stepSpan;
        double yFrac = (lg - minLog) / (maxLog - minLog);
        double x = plotRect.left() + xFrac * plotRect.width();
        double y = plotRect.bottom() - yFrac * plotRect.height();
        return QPointF(x, y);
    };

    // Horizontal gridlines + labels at each whole decade within range.
    painter.setPen(gridColor);
    QFontMetrics fm(painter.font());
    for (int decade = static_cast<int>(std::floor(minLog)); decade <= static_cast<int>(std::ceil(maxLog)); ++decade) {
        if (decade < minLog || decade > maxLog) continue;
        double yFrac = (decade - minLog) / (maxLog - minLog);
        int y = plotRect.bottom() - static_cast<int>(yFrac * plotRect.height());
        painter.drawLine(plotRect.left(), y, plotRect.right(), y);
        painter.setPen(fg);
        QString label = QString("1e%1").arg(decade);
        painter.drawText(plotRect.left() - fm.horizontalAdvance(label) - 6, y + fm.ascent() / 2, label);
        painter.setPen(gridColor);
    }

    painter.setPen(fg);
    painter.drawText(plotRect.left(), plotRect.bottom() + fm.height(), QString::number(minStep));
    QString maxStepLabel = QString::number(maxStep);
    painter.drawText(plotRect.right() - fm.horizontalAdvance(maxStepLabel), plotRect.bottom() + fm.height(),
                      maxStepLabel);

    QPainterPath path;
    path.moveTo(toPixel(residualPoints_.front().first, residualPoints_.front().second));
    for (std::size_t k = 1; k < residualPoints_.size(); ++k) {
        path.lineTo(toPixel(residualPoints_[k].first, residualPoints_[k].second));
    }
    painter.setPen(QPen(lineColor, 2));
    painter.drawPath(path);
}
