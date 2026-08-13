#pragma once

#include <array>

#include <QString>

// Port of ui/theme.py's Theme/THEMES -- app-wide color themes driving both
// the Qt widget stylesheet (build_stylesheet) and, directly (no separate
// re-application step like matplotlib needed), the hand-rolled plot
// widget's own painting.
struct Theme {
    QString key, label;
    QString bg;          // window/app background
    QString panel_bg;    // group box / card background
    QString text;
    QString muted_text;  // "description"-style secondary labels
    QString border;
    QString input_bg;
    QString accent;      // primary buttons, progress bar, selected tab
    QString accent_hover;
    QString accent_text; // text/icon color drawn on top of `accent`
    QString danger;      // stop button
    QString danger_hover;
    QString success;     // paraview button
    QString success_hover;
    QString plot_bg;     // plot widget background
    QString plot_fg;     // plot widget text/tick/axis color
    QString plot_grid;
};

inline constexpr const char* kDefaultThemeKey = "dark_blue";

// The 5 themes (1 light + 4 dark variants), in the same order as
// ui/theme.py's THEMES dict.
[[nodiscard]] const std::array<Theme, 5>& themes();

// Falls back to kDefaultThemeKey if `key` doesn't match any theme --
// intentionally lenient (unlike Python's dict[key] KeyError) since this is
// used to resolve a QSettings-persisted key that could be stale/corrupt.
[[nodiscard]] const Theme& theme_by_key(const QString& key);

// Port of ui/theme.py's build_stylesheet -- QSS syntax is unchanged
// between PySide6 and Qt6/C++, so every selector transfers directly.
[[nodiscard]] QString build_stylesheet(const Theme& theme);
