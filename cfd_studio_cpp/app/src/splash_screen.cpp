#include "splash_screen.hpp"

#include <array>
#include <cmath>

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr double kPi = 3.14159265358979323846;
double deg_to_rad(double deg) {
    return deg * kPi / 180.0;
}
} // namespace

CowCanvas::CowCanvas(QWidget* parent) : QWidget(parent) {
    setFixedSize(320, 200);
}

void CowCanvas::advance(double dt) {
    t_ += dt;
    update();
}

void CowCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    double t = t_;
    double cx = width() / 2.0, cy = height() / 2.0 + 10;
    double bob = 5 * std::sin(t * 2.4);

    double shadowW = 118 - 3 * std::sin(t * 2.4);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 35));
    p.drawEllipse(static_cast<int>(cx - shadowW / 2), static_cast<int>(cy + 62), static_cast<int>(shadowW), 14);

    double bodyCx = cx, bodyCy = cy + bob;
    drawTail(p, bodyCx, bodyCy, t);
    drawLegs(p, bodyCx, bodyCy, t);
    drawBody(p, bodyCx, bodyCy);
    drawHead(p, bodyCx - 58, bodyCy - 20 + 3 * std::sin(t * 2.4 + 0.6), t);
    drawNotes(p, bodyCx, bodyCy, t);
}

void CowCanvas::drawLegs(QPainter& p, double cx, double cy, double t) {
    QPen pen(QColor("#2b2b2b"));
    pen.setWidth(10);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);

    double swing = 22 * std::sin(t * 4.2);
    struct Leg {
        double ax, ay, sign;
    };
    std::array<Leg, 4> legs = {{
        {cx - 40, cy + 18, +1},
        {cx - 15, cy + 20, -1},
        {cx + 18, cy + 20, -1},
        {cx + 42, cy + 18, +1},
    }};
    for (const auto& leg : legs) {
        double angle = deg_to_rad(leg.sign * swing);
        double length = 34;
        double ex = leg.ax + length * std::sin(angle);
        double ey = leg.ay + length * std::cos(angle);
        p.drawLine(static_cast<int>(leg.ax), static_cast<int>(leg.ay), static_cast<int>(ex), static_cast<int>(ey));
        p.setBrush(QColor("#1a1a1a"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(static_cast<int>(ex) - 6, static_cast<int>(ey) - 4, 12, 9);
        p.setPen(pen);
    }
}

void CowCanvas::drawTail(QPainter& p, double cx, double cy, double t) {
    double sway = 18 * std::sin(t * 3.1);
    QPainterPath path;
    double startX = cx + 62, startY = cy - 5;
    path.moveTo(startX, startY);
    path.quadTo(startX + 22, startY + 20, startX + 14 + sway, startY + 44);
    QPen pen(QColor("#2b2b2b"));
    pen.setWidth(5);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#2b2b2b"));
    p.drawEllipse(static_cast<int>(startX + 14 + sway) - 6, static_cast<int>(startY + 44) - 6, 12, 12);
}

void CowCanvas::drawBody(QPainter& p, double cx, double cy) {
    p.setPen(QPen(QColor("#2b2b2b"), 2.5));
    p.setBrush(QColor("#fffaf5"));
    p.drawEllipse(static_cast<int>(cx - 65), static_cast<int>(cy - 32), 130, 68);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#dc2626"));
    std::array<std::array<double, 4>, 4> spots = {{
        {-38, -12, 30, 22},
        {10, 8, 26, 20},
        {36, -14, 22, 18},
        {-8, -20, 20, 14},
    }};
    for (const auto& s : spots) {
        double dx = s[0], dy = s[1], w = s[2], h = s[3];
        p.drawEllipse(static_cast<int>(cx + dx - w / 2), static_cast<int>(cy + dy - h / 2), static_cast<int>(w),
                       static_cast<int>(h));
    }
}

void CowCanvas::drawHead(QPainter& p, double hx, double hy, double t) {
    double flap = 6 * std::sin(t * 4.2);
    p.setPen(QPen(QColor("#2b2b2b"), 2));
    p.setBrush(QColor("#fffaf5"));
    p.drawEllipse(static_cast<int>(hx - 26), static_cast<int>(hy - 30 + flap), 16, 20);
    p.drawEllipse(static_cast<int>(hx + 12), static_cast<int>(hy - 30 - flap), 16, 20);

    p.setBrush(QColor("#fffaf5"));
    p.drawEllipse(static_cast<int>(hx - 26), static_cast<int>(hy - 24), 52, 48);

    p.setBrush(QColor("#dc2626"));
    p.drawEllipse(static_cast<int>(hx - 20), static_cast<int>(hy - 20), 18, 14);

    p.setBrush(QColor("#f9c9c9"));
    p.drawEllipse(static_cast<int>(hx - 16), static_cast<int>(hy + 8), 34, 20);
    p.setBrush(QColor("#7a4a4a"));
    p.drawEllipse(static_cast<int>(hx - 7), static_cast<int>(hy + 15), 5, 5);
    p.drawEllipse(static_cast<int>(hx + 5), static_cast<int>(hy + 15), 5, 5);

    bool blink = std::fmod(t, 2.6) > 2.45;
    p.setBrush(QColor("#1a1a1a"));
    if (blink) {
        QPen pen(QColor("#1a1a1a"), 2);
        p.setPen(pen);
        p.drawLine(static_cast<int>(hx - 4), static_cast<int>(hy - 2), static_cast<int>(hx + 4),
                   static_cast<int>(hy - 2));
        p.drawLine(static_cast<int>(hx + 20), static_cast<int>(hy - 2), static_cast<int>(hx + 28),
                   static_cast<int>(hy - 2));
    } else {
        p.setPen(Qt::NoPen);
        p.drawEllipse(static_cast<int>(hx - 6), static_cast<int>(hy - 6), 7, 7);
        p.drawEllipse(static_cast<int>(hx + 20), static_cast<int>(hy - 6), 7, 7);
    }
}

void CowCanvas::drawNotes(QPainter& p, double cx, double cy, double t) {
    QFont font;
    font.setPointSize(14);
    p.setFont(font);
    QString notes = QString::fromUtf8("\xE2\x99\xAA\xE2\x99\xAB"); // "♪♫"
    for (int i = 0; i < notes.size(); ++i) {
        double phase = t * 1.6 + i * kPi;
        double local = std::fmod(phase, 2 * kPi);
        if (local > kPi) continue; // only visible for half the cycle -- rest is the "reset"
        double rise = local / kPi;
        int alpha = static_cast<int>(255 * std::sin(local));
        double x = cx - 95 - i * 14 + 10 * std::sin(t * 2 + i);
        double y = cy - 40 - rise * 34;
        p.setPen(QColor(37, 99, 235, alpha));
        p.drawText(static_cast<int>(x), static_cast<int>(y), notes.mid(i, 1));
    }
}

CowSplashScreen::CowSplashScreen(QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint) {
    setAttribute(Qt::WA_TranslucentBackground);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 22);
    root->setSpacing(4);

    auto* title = new QLabel("Venturi CFD", this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 20px; font-weight: 700; color: #1f2937;");
    root->addWidget(title);

    canvas_ = new CowCanvas(this);
    root->addWidget(canvas_, 0, Qt::AlignCenter);

    statusLabel_ = new QLabel("Starting...", this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setStyleSheet("font-size: 12px; color: #6b7280;");
    root->addWidget(statusLabel_);

    setFixedSize(sizeHint());
    centerOnScreen();

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this]() { canvas_->advance(0.04); });
    timer_->start(40);
}

void CowSplashScreen::setStatus(const QString& text) {
    statusLabel_->setText(text);
}

void CowSplashScreen::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 235));
    p.drawRoundedRect(rect(), 18, 18);
    p.setPen(QPen(QColor(0, 0, 0, 25), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 18, 18);
}

void CowSplashScreen::centerOnScreen() {
    QScreen* screen = QApplication::primaryScreen();
    if (!screen) return;
    QRect geo = screen->availableGeometry();
    move(geo.center().x() - width() / 2, geo.center().y() - height() / 2);
}
