#pragma once

#include <QWidget>

class QLabel;
class QTimer;

// Port of ui/splash.py's _CowCanvas -- paints a simple vector cow that
// bobs, kicks its legs, and wags its tail on a timer, cheap enough to
// redraw at ~25fps with plain QPainter, no image assets needed.
class CowCanvas : public QWidget {
    Q_OBJECT

public:
    explicit CowCanvas(QWidget* parent = nullptr);
    void advance(double dt);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawLegs(QPainter& p, double cx, double cy, double t);
    void drawTail(QPainter& p, double cx, double cy, double t);
    void drawBody(QPainter& p, double cx, double cy);
    void drawHead(QPainter& p, double hx, double hy, double t);
    void drawNotes(QPainter& p, double cx, double cy, double t);

    double t_ = 0.0;
};

// Port of ui/splash.py's CowSplashScreen -- a frameless, always-on-top
// loading card. C++ has no multi-second import cost to cover (unlike
// Python's numpy/scipy/matplotlib/numba/trimesh chain), so main.cpp shows
// this for a short fixed duration instead of porting main.py's
// background-thread import-loader machinery.
class CowSplashScreen : public QWidget {
    Q_OBJECT

public:
    explicit CowSplashScreen(QWidget* parent = nullptr);
    void setStatus(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void centerOnScreen();

    CowCanvas* canvas_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer* timer_ = nullptr;
};
