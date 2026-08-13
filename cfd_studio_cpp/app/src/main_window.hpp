#pragma once

#include <QMainWindow>
#include <QSettings>

#include "tab_2d.hpp"
#include "theme.hpp"
#include "widgets/mesh_preview_widget.hpp"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    // Applies the stylesheet and persists the choice under the same
    // QSettings org/app/key ("CFDStudio"/"CFDStudio"/"theme") as
    // ui/main_window.py's _apply_theme.
    void applyTheme(const QString& key);
    void showAbout();

    QSettings settings_;
    QString currentThemeKey_;
    TwoDPanel* twoDPanel_ = nullptr;
};
