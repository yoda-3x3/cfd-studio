"""
App-wide color themes: a light theme (the app's original look) and
several dark variants with different accent-color combos. A `Theme`
drives both the Qt widget stylesheet (QSS) and the matplotlib figure
styling, so plots stay legible and visually consistent with whichever
theme is active instead of staying stuck with light-mode white
backgrounds no matter what the rest of the app looks like.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass
class Theme:
    key: str
    label: str
    bg: str            # window/app background
    panel_bg: str       # group box / card background
    text: str
    muted_text: str      # "description"-style secondary labels
    border: str
    input_bg: str
    accent: str           # primary buttons, progress bar, selected tab
    accent_hover: str
    accent_text: str       # text/icon color drawn on top of `accent`
    danger: str             # stop button
    danger_hover: str
    success: str              # paraview button
    success_hover: str
    plot_bg: str               # matplotlib figure/axes background
    plot_fg: str                # matplotlib text/tick/spine color
    plot_grid: str


THEMES: dict[str, Theme] = {
    "light": Theme(
        key="light", label="Light",
        bg="#f3f4f6", panel_bg="#ffffff", text="#1f2937", muted_text="#5a5f6a", border="#d0d3d8", input_bg="#ffffff",
        accent="#2563eb", accent_hover="#1d4ed8", accent_text="#ffffff",
        danger="#dc2626", danger_hover="#b91c1c", success="#059669", success_hover="#047857",
        plot_bg="#ffffff", plot_fg="#1f2937", plot_grid="#e5e7eb",
    ),
    "dark_blue": Theme(
        key="dark_blue", label="Dark - Blue",
        bg="#0f172a", panel_bg="#1e293b", text="#e2e8f0", muted_text="#94a3b8", border="#334155", input_bg="#0f172a",
        accent="#3b82f6", accent_hover="#60a5fa", accent_text="#ffffff",
        danger="#f87171", danger_hover="#ef4444", success="#34d399", success_hover="#10b981",
        plot_bg="#1e293b", plot_fg="#e2e8f0", plot_grid="#334155",
    ),
    "dark_purple": Theme(
        key="dark_purple", label="Dark - Purple",
        bg="#161320", panel_bg="#221d33", text="#e9e4f7", muted_text="#a99fc7", border="#3c3356", input_bg="#161320",
        accent="#a78bfa", accent_hover="#c4b5fd", accent_text="#1e1533",
        danger="#f87171", danger_hover="#ef4444", success="#4ade80", success_hover="#22c55e",
        plot_bg="#221d33", plot_fg="#e9e4f7", plot_grid="#3c3356",
    ),
    "dark_teal": Theme(
        key="dark_teal", label="Dark - Teal",
        bg="#0c1a1a", panel_bg="#132727", text="#dcf5f0", muted_text="#8fbdb5", border="#254545", input_bg="#0c1a1a",
        accent="#2dd4bf", accent_hover="#5eead4", accent_text="#062a26",
        danger="#fb7185", danger_hover="#f43f5e", success="#a3e635", success_hover="#84cc16",
        plot_bg="#132727", plot_fg="#dcf5f0", plot_grid="#254545",
    ),
    "dark_amber": Theme(
        key="dark_amber", label="Dark - Amber",
        bg="#1c1712", panel_bg="#2a221a", text="#f6ecdd", muted_text="#bfa98a", border="#4a3c28", input_bg="#1c1712",
        accent="#fbbf24", accent_hover="#fcd34d", accent_text="#241c0a",
        danger="#f87171", danger_hover="#ef4444", success="#a3e635", success_hover="#84cc16",
        plot_bg="#2a221a", plot_fg="#f6ecdd", plot_grid="#4a3c28",
    ),
}

DEFAULT_THEME_KEY = "dark_blue"


def build_stylesheet(theme: Theme) -> str:
    return f"""
QMainWindow, QWidget {{ background: {theme.bg}; color: {theme.text}; }}
QGroupBox {{
    font-weight: 600;
    border: 1px solid {theme.border};
    border-radius: 8px;
    margin-top: 14px;
    background: {theme.panel_bg};
    padding-top: 6px;
    color: {theme.text};
}}
QGroupBox::title {{
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: {theme.text};
}}
QLabel {{ color: {theme.text}; background: transparent; }}
QLabel#description {{ color: {theme.muted_text}; font-style: italic; }}
QPushButton {{
    background: {theme.accent};
    color: {theme.accent_text};
    border-radius: 6px;
    padding: 7px 14px;
    font-weight: 600;
    border: none;
}}
QPushButton:hover {{ background: {theme.accent_hover}; }}
QPushButton:disabled {{ background: {theme.border}; color: {theme.muted_text}; }}
QPushButton#stopButton {{ background: {theme.danger}; }}
QPushButton#stopButton:hover {{ background: {theme.danger_hover}; }}
QPushButton#paraviewButton {{ background: {theme.success}; }}
QPushButton#paraviewButton:hover {{ background: {theme.success_hover}; }}
QProgressBar {{
    border: 1px solid {theme.border};
    border-radius: 6px;
    text-align: center;
    background: {theme.input_bg};
    color: {theme.text};
    height: 18px;
}}
QProgressBar::chunk {{ background-color: {theme.accent}; border-radius: 6px; }}
QStatusBar {{ background: {theme.panel_bg}; color: {theme.text}; }}
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {{
    background: {theme.input_bg};
    color: {theme.text};
    border: 1px solid {theme.border};
    border-radius: 4px;
    padding: 3px;
    selection-background-color: {theme.accent};
}}
QComboBox QAbstractItemView {{
    background: {theme.input_bg};
    color: {theme.text};
    selection-background-color: {theme.accent};
    selection-color: {theme.accent_text};
}}
QCheckBox, QRadioButton {{ color: {theme.text}; background: transparent; }}
QScrollArea {{ background: transparent; border: none; }}
QTabWidget::pane {{ border: 1px solid {theme.border}; background: {theme.bg}; }}
QTabBar::tab {{
    background: {theme.panel_bg};
    color: {theme.text};
    padding: 8px 16px;
    border: 1px solid {theme.border};
    border-bottom: none;
}}
QTabBar::tab:selected {{ background: {theme.accent}; color: {theme.accent_text}; }}
QMenuBar {{ background: {theme.panel_bg}; color: {theme.text}; }}
QMenuBar::item:selected {{ background: {theme.accent}; color: {theme.accent_text}; }}
QMenu {{ background: {theme.panel_bg}; color: {theme.text}; border: 1px solid {theme.border}; }}
QMenu::item:selected {{ background: {theme.accent}; color: {theme.accent_text}; }}
QMessageBox {{ background: {theme.panel_bg}; color: {theme.text}; }}
QScrollBar:vertical {{ background: {theme.panel_bg}; width: 12px; }}
QScrollBar::handle:vertical {{ background: {theme.border}; border-radius: 5px; min-height: 20px; }}
QScrollBar::handle:vertical:hover {{ background: {theme.accent}; }}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0px; }}
"""


def apply_theme_to_axes(ax, theme: Theme) -> None:
    """Re-applies theme coloring to a matplotlib Axes -- needed after
    every `.cla()`, since clearing an axes resets it to matplotlib's
    default (light) styling regardless of the figure's own facecolor."""
    ax.set_facecolor(theme.plot_bg)
    ax.title.set_color(theme.plot_fg)
    ax.xaxis.label.set_color(theme.plot_fg)
    ax.yaxis.label.set_color(theme.plot_fg)
    ax.tick_params(colors=theme.plot_fg, which="both")
    for spine in ax.spines.values():
        spine.set_color(theme.plot_grid)


def apply_theme_to_figure(figure, theme: Theme) -> None:
    figure.patch.set_facecolor(theme.plot_bg)
    for ax in figure.get_axes():
        apply_theme_to_axes(ax, theme)
