#pragma once

#include <QMainWindow>
#include <QSettings>

#include "tab_2d.hpp"
#include "tab_3d.hpp"
#include "theme.hpp"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    // Applies the stylesheet and persists the choice under QSettings
    // org/app/key ("VenturiCFD"/"VenturiCFD"/"theme") -- diverges from
    // ui/main_window.py's own "CFDStudio"/"CFDStudio" (the Python app is a
    // frozen spec, not rebranded) since this C++ app now has its own name.
    void applyTheme(const QString& key);
    void showAbout();

    QSettings settings_;
    QString currentThemeKey_;
    TwoDPanel* twoDPanel_ = nullptr;
    ThreeDPanel* threeDPanel_ = nullptr;
};
