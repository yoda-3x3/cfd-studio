#pragma once

#include <QMainWindow>
#include <QSettings>

#include "theme.hpp"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    // Applies the stylesheet and persists the choice under the same
    // QSettings org/app/key ("CFDStudio"/"CFDStudio"/"theme") as
    // ui/main_window.py's _apply_theme -- reused by the Theme menu once
    // it's built (Phase 6.5).
    void applyTheme(const QString& key);

    QSettings settings_;
    QString currentThemeKey_;
};
