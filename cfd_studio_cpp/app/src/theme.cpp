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

QString ui_scale_key(UiScale scale) {
    switch (scale) {
        case UiScale::Small: return "small";
        case UiScale::Large: return "large";
        case UiScale::Medium: default: return "medium";
    }
}

QString ui_scale_label(UiScale scale) {
    switch (scale) {
        case UiScale::Small: return "Small";
        case UiScale::Large: return "Large";
        case UiScale::Medium: default: return "Medium";
    }
}

UiScale ui_scale_by_key(const QString& key) {
    if (key == "small") return UiScale::Small;
    if (key == "large") return UiScale::Large;
    return kDefaultUiScale;
}

const std::array<UiScale, 3>& ui_scales() {
    static const std::array<UiScale, 3> all = {UiScale::Small, UiScale::Medium, UiScale::Large};
    return all;
}

namespace {
// Font size / padding metrics per UiScale -- kept as plain string literals
// (not computed) so each tier's numbers are easy to eyeball and tweak.
struct ScaleMetrics {
    const char* base_font_pt;
    const char* button_padding;
    const char* input_padding;
    const char* tab_padding;
};

ScaleMetrics scale_metrics(UiScale scale) {
    switch (scale) {
        case UiScale::Small: return {"8.5pt", "5px 10px", "2px", "5px 10px"};
        case UiScale::Large: return {"14pt", "14px 26px", "7px", "14px 24px"};
        case UiScale::Medium:
        default: return {"10.5pt", "7px 14px", "3px", "8px 16px"};
    }
}
} // namespace

QString build_stylesheet(const Theme& t, UiScale scale, bool dyslexia_font) {
    ScaleMetrics m = scale_metrics(scale);

    QString fontFamilyRule;
    if (dyslexia_font) {
        // Neither font is bundled with the app: OpenDyslexic only takes
        // effect if the user has separately installed it (a specialized
        // dyslexia-readability font, so not something to silently force on
        // everyone via a bundled resource); Comic Sans MS ships with
        // Windows and is a commonly-cited free fallback with the same
        // "distinguishable letterforms" property. Falls through to the
        // platform default font if neither is present.
        fontFamilyRule = "QWidget { font-family: \"OpenDyslexic\", \"Comic Sans MS\", sans-serif; }\n";
    }

    return (fontFamilyRule + QString(
               "QWidget { font-size: %14; }\n"
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
               "    padding: %15;\n"
               "    font-weight: 600;\n"
               "    border: none;\n"
               "}\n"
               "QPushButton:hover { background: %8; }\n"
               "QPushButton:disabled { background: %3; color: %5; }\n"
               "QPushButton#stopButton { background: %9; }\n"
               "QPushButton#stopButton:hover { background: %10; }\n"
               "QPushButton#paraviewButton { background: %11; }\n"
               "QPushButton#paraviewButton:hover { background: %12; }\n"
               // Without these, the #id background rules above (higher
               // specificity than the plain ":disabled" rule) kept the
               // full-saturation red/green background on a disabled
               // stop/paraview button while still picking up ":disabled"'s
               // muted-gray text color for want of any more-specific color
               // rule -- muted gray on saturated green/red read as low
               // contrast, close to illegible. Falling back to the same
               // neutral disabled look as every other button fixes that
               // and also reads as "disabled" more clearly than a
               // still-vivid button did.
               "QPushButton#stopButton:disabled, QPushButton#paraviewButton:disabled {\n"
               "    background: %3;\n"
               "    color: %5;\n"
               "}\n"
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
               "    padding: %16;\n"
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
               "    padding: %17;\n"
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
        .arg(t.input_bg)    // %13
        .arg(m.base_font_pt)    // %14
        .arg(m.button_padding)  // %15
        .arg(m.input_padding)   // %16
        .arg(m.tab_padding));   // %17
}
