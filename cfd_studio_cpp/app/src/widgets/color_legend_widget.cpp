#include "color_legend_widget.hpp"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>

#include "colormap.hpp"

ColorLegendWidget::ColorLegendWidget(QWidget* parent) : QWidget(parent) {
    theme_ = theme_by_key(kDefaultThemeKey);
    setMinimumWidth(90);
}

void ColorLegendWidget::setTheme(const Theme& theme) {
    theme_ = theme;
    update();
}

void ColorLegendWidget::setFieldName(const QString& name) {
    fieldName_ = name;
    update();
}

void ColorLegendWidget::setRange(double vmin, double vmax) {
    vmin_ = vmin;
    vmax_ = vmax;
    update();
}

void ColorLegendWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(theme_.plot_bg));

    QColor fg(theme_.plot_fg);
    QColor gridColor(theme_.plot_grid);
    painter.setPen(fg);

    constexpr int kMargin = 8;
    constexpr int kBarWidth = 24;

    QFontMetrics fm(painter.font());
    QRect titleRect(kMargin, kMargin, width() - 2 * kMargin, fm.height() * 2);
    painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, fieldName_);

    int barTop = titleRect.bottom() + kMargin;
    int barBottom = height() - kMargin - fm.height(); // leave room for the bottom (min) label
    if (barBottom <= barTop) return;
    QRect barRect(kMargin, barTop, kBarWidth, barBottom - barTop);

    QLinearGradient gradient(barRect.topLeft(), barRect.bottomLeft());
    for (int s = 0; s <= 10; ++s) {
        double t = s / 10.0;
        gradient.setColorAt(1.0 - t, colormap_sample(t)); // top of bar = max value
    }
    painter.fillRect(barRect, gradient);
    painter.setPen(gridColor);
    painter.drawRect(barRect);

    painter.setPen(fg);
    QString maxLabel = QString::number(vmax_, 'g', 3);
    QString minLabel = QString::number(vmin_, 'g', 3);
    painter.drawText(barRect.right() + 4, barRect.top() + fm.ascent(), maxLabel);
    painter.drawText(barRect.right() + 4, barRect.bottom(), minLabel);
}
