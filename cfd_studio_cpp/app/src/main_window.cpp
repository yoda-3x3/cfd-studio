#include "main_window.hpp"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), settings_("CFDStudio", "CFDStudio") {
    setWindowTitle("CFD Studio");
    resize(1280, 820);

    applyTheme(settings_.value("theme", kDefaultThemeKey).toString());
}

void MainWindow::applyTheme(const QString& key) {
    const Theme& theme = theme_by_key(key);
    setStyleSheet(build_stylesheet(theme));
    currentThemeKey_ = theme.key;
    settings_.setValue("theme", theme.key);
}
