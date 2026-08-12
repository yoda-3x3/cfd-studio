"""A small splash screen shown immediately at startup, while the heavy
scientific stack (numpy/scipy/matplotlib/numba/trimesh, ~seconds of pure
import time) loads in a background thread. Purely cosmetic — its only
job is to make sure the user sees *something* responsive right away
instead of a frozen/blank window, per request: "a simple animation of a
dancing polish cow" (red-and-white spots, nodding to the Polish flag)."""
from __future__ import annotations

import math

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QBrush, QColor, QFont, QPainter, QPainterPath, QPen
from PySide6.QtWidgets import QApplication, QLabel, QVBoxLayout, QWidget


class _CowCanvas(QWidget):
    """Paints a simple vector cow that bobs, kicks its legs, and wags its
    tail on a timer -- cheap enough to redraw at ~25fps with plain
    QPainter, no image assets needed."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(320, 200)
        self._t = 0.0

    def advance(self, dt: float):
        self._t += dt
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)

        t = self._t
        cx, cy = self.width() / 2, self.height() / 2 + 10
        bob = 5 * math.sin(t * 2.4)

        # ground shadow (squashes/stretches opposite the bob, cheap "weight" cue)
        shadow_w = 118 - 3 * math.sin(t * 2.4)
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(0, 0, 0, 35))
        p.drawEllipse(int(cx - shadow_w / 2), int(cy + 62), int(shadow_w), 14)

        body_cx, body_cy = cx, cy + bob
        self._draw_tail(p, body_cx, body_cy, t)
        self._draw_legs(p, body_cx, body_cy, t)
        self._draw_body(p, body_cx, body_cy)
        self._draw_head(p, body_cx - 58, body_cy - 20 + 3 * math.sin(t * 2.4 + 0.6), t)
        self._draw_notes(p, body_cx, body_cy, t)

    # ------------------------------------------------------------------
    def _draw_legs(self, p: QPainter, cx, cy, t):
        pen = QPen(QColor("#2b2b2b"))
        pen.setWidth(10)
        pen.setCapStyle(Qt.RoundCap)
        p.setPen(pen)

        swing = 22 * math.sin(t * 4.2)
        # (attach_x, attach_y, phase_sign) -- diagonal pairs swing together, like a two-step
        legs = [
            (cx - 40, cy + 18, +1),
            (cx - 15, cy + 20, -1),
            (cx + 18, cy + 20, -1),
            (cx + 42, cy + 18, +1),
        ]
        for ax, ay, sign in legs:
            angle = math.radians(sign * swing)
            length = 34
            ex = ax + length * math.sin(angle)
            ey = ay + length * math.cos(angle)
            p.drawLine(int(ax), int(ay), int(ex), int(ey))
            p.setBrush(QColor("#1a1a1a"))
            p.setPen(Qt.NoPen)
            p.drawEllipse(int(ex) - 6, int(ey) - 4, 12, 9)
            p.setPen(pen)

    def _draw_tail(self, p: QPainter, cx, cy, t):
        sway = 18 * math.sin(t * 3.1)
        path = QPainterPath()
        start_x, start_y = cx + 62, cy - 5
        path.moveTo(start_x, start_y)
        path.quadTo(start_x + 22, start_y + 20, start_x + 14 + sway, start_y + 44)
        pen = QPen(QColor("#2b2b2b"))
        pen.setWidth(5)
        pen.setCapStyle(Qt.RoundCap)
        p.setPen(pen)
        p.setBrush(Qt.NoBrush)
        p.drawPath(path)
        p.setPen(Qt.NoPen)
        p.setBrush(QColor("#2b2b2b"))
        p.drawEllipse(int(start_x + 14 + sway) - 6, int(start_y + 44) - 6, 12, 12)

    def _draw_body(self, p: QPainter, cx, cy):
        p.setPen(QPen(QColor("#2b2b2b"), 2.5))
        p.setBrush(QColor("#fffaf5"))
        p.drawEllipse(int(cx - 65), int(cy - 32), 130, 68)

        # red spots (red + white -> a little nod to the Polish flag)
        p.setPen(Qt.NoPen)
        p.setBrush(QColor("#dc2626"))
        for dx, dy, w, h in ((-38, -12, 30, 22), (10, 8, 26, 20), (36, -14, 22, 18), (-8, -20, 20, 14)):
            p.drawEllipse(int(cx + dx - w / 2), int(cy + dy - h / 2), w, h)

    def _draw_head(self, p: QPainter, hx, hy, t):
        # ears (a little flap on the beat)
        flap = 6 * math.sin(t * 4.2)
        p.setPen(QPen(QColor("#2b2b2b"), 2))
        p.setBrush(QColor("#fffaf5"))
        p.drawEllipse(int(hx - 26), int(hy - 30 + flap), 16, 20)
        p.drawEllipse(int(hx + 12), int(hy - 30 - flap), 16, 20)

        p.setBrush(QColor("#fffaf5"))
        p.drawEllipse(int(hx - 26), int(hy - 24), 52, 48)

        p.setBrush(QColor("#dc2626"))
        p.drawEllipse(int(hx - 20), int(hy - 20), 18, 14)

        # snout
        p.setBrush(QColor("#f9c9c9"))
        p.drawEllipse(int(hx - 16), int(hy + 8), 34, 20)
        p.setBrush(QColor("#7a4a4a"))
        p.drawEllipse(int(hx - 7), int(hy + 15), 5, 5)
        p.drawEllipse(int(hx + 5), int(hy + 15), 5, 5)

        # eyes -- blink every couple of seconds
        blink = (t % 2.6) > 2.45
        p.setBrush(QColor("#1a1a1a"))
        if blink:
            pen = QPen(QColor("#1a1a1a"), 2)
            p.setPen(pen)
            p.drawLine(int(hx - 4), int(hy - 2), int(hx + 4), int(hy - 2))
            p.drawLine(int(hx + 20), int(hy - 2), int(hx + 28), int(hy - 2))
        else:
            p.setPen(Qt.NoPen)
            p.drawEllipse(int(hx - 6), int(hy - 6), 7, 7)
            p.drawEllipse(int(hx + 20), int(hy - 6), 7, 7)

    def _draw_notes(self, p: QPainter, cx, cy, t):
        font = QFont()
        font.setPointSize(14)
        p.setFont(font)
        for i, glyph in enumerate("♪♫"):
            phase = t * 1.6 + i * math.pi
            local = phase % (2 * math.pi)
            if local > math.pi:
                continue  # only visible for half the cycle -- rest is the "reset"
            rise = local / math.pi
            alpha = int(255 * math.sin(local))
            x = cx - 95 - i * 14 + 10 * math.sin(t * 2 + i)
            y = cy - 40 - rise * 34
            p.setPen(QColor(37, 99, 235, alpha))
            p.drawText(int(x), int(y), glyph)


class CowSplashScreen(QWidget):
    """Frameless, always-on-top loading card shown while the app's heavy
    dependencies import in the background."""

    def __init__(self):
        super().__init__(None, Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint)
        self.setAttribute(Qt.WA_TranslucentBackground)

        root = QVBoxLayout(self)
        root.setContentsMargins(28, 22, 28, 22)
        root.setSpacing(4)

        title = QLabel("CFD Studio")
        title.setAlignment(Qt.AlignCenter)
        title.setStyleSheet("font-size: 20px; font-weight: 700; color: #1f2937;")
        root.addWidget(title)

        self._canvas = _CowCanvas()
        root.addWidget(self._canvas, 0, Qt.AlignCenter)

        self._status_label = QLabel("Starting...")
        self._status_label.setAlignment(Qt.AlignCenter)
        self._status_label.setStyleSheet("font-size: 12px; color: #6b7280;")
        root.addWidget(self._status_label)

        self.setFixedSize(self.sizeHint())
        self._center_on_screen()

        self._timer = QTimer(self)
        self._timer.timeout.connect(lambda: self._canvas.advance(0.04))
        self._timer.start(40)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(255, 255, 255, 235))
        p.drawRoundedRect(self.rect(), 18, 18)
        p.setPen(QPen(QColor(0, 0, 0, 25), 1))
        p.setBrush(Qt.NoBrush)
        p.drawRoundedRect(self.rect().adjusted(0, 0, -1, -1), 18, 18)

    def set_status(self, text: str):
        self._status_label.setText(text)

    def _center_on_screen(self):
        screen = QApplication.primaryScreen()
        if screen is None:
            return
        geo = screen.availableGeometry()
        self.move(geo.center().x() - self.width() // 2, geo.center().y() - self.height() // 2)
