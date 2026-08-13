#include "theme.hpp"

const std::array<Theme, 5>& themes() {
    static const std::array<Theme, 5> all = {{
        {
            "light", "Light",
            "#f3f4f6", "#ffffff", "#1f2937", "#5a5f6a", "#d0d3d8", "#ffffff",
            "#2563eb", "#1d4ed8", "#ffffff",
            "#dc2626", "#b91c1c", "#059669", "#047857",
            "#ffffff", "#1f2937", "#e5e7eb",
        },
        {
            "dark_blue", "Dark - Blue",
            "#0f172a", "#1e293b", "#e2e8f0", "#94a3b8", "#334155", "#0f172a",
            "#3b82f6", "#60a5fa", "#ffffff",
            "#f87171", "#ef4444", "#34d399", "#10b981",
            "#1e293b", "#e2e8f0", "#334155",
        },
        {
            "dark_purple", "Dark - Purple",
            "#161320", "#221d33", "#e9e4f7", "#a99fc7", "#3c3356", "#161320",
            "#a78bfa", "#c4b5fd", "#1e1533",
            "#f87171", "#ef4444", "#4ade80", "#22c55e",
            "#221d33", "#e9e4f7", "#3c3356",
        },
        {
            "dark_teal", "Dark - Teal",
            "#0c1a1a", "#132727", "#dcf5f0", "#8fbdb5", "#254545", "#0c1a1a",
            "#2dd4bf", "#5eead4", "#062a26",
            "#fb7185", "#f43f5e", "#a3e635", "#84cc16",
            "#132727", "#dcf5f0", "#254545",
        },
        {
            "dark_amber", "Dark - Amber",
            "#1c1712", "#2a221a", "#f6ecdd", "#bfa98a", "#4a3c28", "#1c1712",
            "#fbbf24", "#fcd34d", "#241c0a",
            "#f87171", "#ef4444", "#a3e635", "#84cc16",
            "#2a221a", "#f6ecdd", "#4a3c28",
        },
    }};
    return all;
}

const Theme& theme_by_key(const QString& key) {
    for (const auto& t : themes()) {
        if (t.key == key) return t;
    }
    for (const auto& t : themes()) {
        if (t.key == QString(kDefaultThemeKey)) return t;
    }
    return themes().front();
}

QString build_stylesheet(const Theme& t) {
    return QString(
               "QMainWindow, QWidget { background: %1; color: %2; }\n"
               "QGroupBox {\n"
               "    font-weight: 600;\n"
               "    border: 1px solid %3;\n"
               "    border-radius: 8px;\n"
               "    margin-top: 14px;\n"
               "    background: %4;\n"
               "    padding-top: 6px;\n"
               "    color: %2;\n"
               "}\n"
               "QGroupBox::title {\n"
               "    subcontrol-origin: margin;\n"
               "    left: 10px;\n"
               "    padding: 0 4px;\n"
               "    color: %2;\n"
               "}\n"
               "QLabel { color: %2; background: transparent; }\n"
               "QLabel#description { color: %5; font-style: italic; }\n"
               "QPushButton {\n"
               "    background: %6;\n"
               "    color: %7;\n"
               "    border-radius: 6px;\n"
               "    padding: 7px 14px;\n"
               "    font-weight: 600;\n"
               "    border: none;\n"
               "}\n"
               "QPushButton:hover { background: %8; }\n"
               "QPushButton:disabled { background: %3; color: %5; }\n"
               "QPushButton#stopButton { background: %9; }\n"
               "QPushButton#stopButton:hover { background: %10; }\n"
               "QPushButton#paraviewButton { background: %11; }\n"
               "QPushButton#paraviewButton:hover { background: %12; }\n"
               "QProgressBar {\n"
               "    border: 1px solid %3;\n"
               "    border-radius: 6px;\n"
               "    text-align: center;\n"
               "    background: %13;\n"
               "    color: %2;\n"
               "    height: 18px;\n"
               "}\n"
               "QProgressBar::chunk { background-color: %6; border-radius: 6px; }\n"
               "QStatusBar { background: %4; color: %2; }\n"
               "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {\n"
               "    background: %13;\n"
               "    color: %2;\n"
               "    border: 1px solid %3;\n"
               "    border-radius: 4px;\n"
               "    padding: 3px;\n"
               "    selection-background-color: %6;\n"
               "}\n"
               "QComboBox QAbstractItemView {\n"
               "    background: %13;\n"
               "    color: %2;\n"
               "    selection-background-color: %6;\n"
               "    selection-color: %7;\n"
               "}\n"
               "QCheckBox, QRadioButton { color: %2; background: transparent; }\n"
               "QScrollArea { background: transparent; border: none; }\n"
               "QTabWidget::pane { border: 1px solid %3; background: %1; }\n"
               "QTabBar::tab {\n"
               "    background: %4;\n"
               "    color: %2;\n"
               "    padding: 8px 16px;\n"
               "    border: 1px solid %3;\n"
               "    border-bottom: none;\n"
               "}\n"
               "QTabBar::tab:selected { background: %6; color: %7; }\n"
               "QMenuBar { background: %4; color: %2; }\n"
               "QMenuBar::item:selected { background: %6; color: %7; }\n"
               "QMenu { background: %4; color: %2; border: 1px solid %3; }\n"
               "QMenu::item:selected { background: %6; color: %7; }\n"
               "QMessageBox { background: %4; color: %2; }\n"
               "QScrollBar:vertical { background: %4; width: 12px; }\n"
               "QScrollBar::handle:vertical { background: %3; border-radius: 5px; min-height: 20px; }\n"
               "QScrollBar::handle:vertical:hover { background: %6; }\n"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }\n")
        .arg(t.bg)          // %1
        .arg(t.text)        // %2
        .arg(t.border)      // %3
        .arg(t.panel_bg)    // %4
        .arg(t.muted_text)  // %5
        .arg(t.accent)      // %6
        .arg(t.accent_text) // %7
        .arg(t.accent_hover)// %8
        .arg(t.danger)      // %9
        .arg(t.danger_hover)// %10
        .arg(t.success)     // %11
        .arg(t.success_hover) // %12
        .arg(t.input_bg);   // %13
}
