#include "main_window.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>

namespace {
constexpr const char* kAppTitle = "CFD Studio — 2D/3D Incompressible Flow with ParaView";
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), settings_("CFDStudio", "CFDStudio") {
    setWindowTitle(kAppTitle);
    resize(1280, 820);
    setWindowIcon(QIcon(":/app_icon.ico"));

    applyTheme(settings_.value("theme", kDefaultThemeKey).toString());
    buildMenus();

    auto* tabs = new QTabWidget(this);

    twoDPanel_ = new TwoDPanel(tabs);
    tabs->addTab(twoDPanel_, "2D Flow Scenarios");

    threeDPanel_ = new ThreeDPanel(tabs);
    tabs->addTab(threeDPanel_, "3D Custom Geometry");

    setCentralWidget(tabs);

    statusBar()->showMessage("Ready.");
}

void MainWindow::buildMenus() {
    auto* settingsMenu = menuBar()->addMenu("&Settings");
    auto* locateParaviewAction = settingsMenu->addAction("Locate ParaView...");
    connect(locateParaviewAction, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "Locate ParaView Executable", QString(),
                                                      "paraview.exe;;All Files (*.*)");
        if (!path.isEmpty()) settings_.setValue("paraviewPath", path);
    });

    auto* themeMenu = menuBar()->addMenu("&Theme");
    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    for (const auto& theme : themes()) {
        auto* action = themeMenu->addAction(theme.label);
        action->setCheckable(true);
        action->setChecked(theme.key == currentThemeKey_);
        themeGroup->addAction(action);
        QString key = theme.key;
        connect(action, &QAction::triggered, this, [this, key]() { applyTheme(key); });
    }

    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* aboutAction = helpMenu->addAction("About");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::applyTheme(const QString& key) {
    const Theme& theme = theme_by_key(key);
    setStyleSheet(build_stylesheet(theme));
    currentThemeKey_ = theme.key;
    settings_.setValue("theme", theme.key);
    if (twoDPanel_) twoDPanel_->setTheme(theme);
    if (threeDPanel_) threeDPanel_->setTheme(theme);
}

void MainWindow::showAbout() {
    QMessageBox::information(this, "About CFD Studio",
                              "CFD Studio (C++/Qt6 rewrite)\n\n"
                              "2D/3D incompressible Navier-Stokes solver with OpenFOAM/ParaView export.");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (twoDPanel_) twoDPanel_->shutdown();
    if (threeDPanel_) threeDPanel_->shutdown();
    event->accept();
}
