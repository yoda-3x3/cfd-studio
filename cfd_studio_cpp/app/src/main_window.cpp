#include "main_window.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "mesh/primitives.hpp"

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

    // TEMPORARY 6.6/6.7 smoke test -- added first/default so it's visible
    // immediately on launch for screenshot verification.
    auto* testTab = new QWidget(tabs);
    auto* testLayout = new QVBoxLayout(testTab);
    auto* openDialogButton = new QPushButton("Open Orientation Dialog (temp test)", testTab);
    testLayout->addWidget(openDialogButton);
    testLayout->addStretch();
    connect(openDialogButton, &QPushButton::clicked, this, [this]() {
        cfd::mesh::Mesh box = cfd::mesh::make_box({1.0, 0.6, 0.4});
        OrientationDialog dlg(box, "test_box.stl", this);
        if (dlg.exec() == QDialog::Accepted && dlg.confirmedMesh()) {
            statusBar()->showMessage("Orientation confirmed.", 5000);
        } else {
            statusBar()->showMessage("Orientation dialog cancelled.", 5000);
        }
    });
    tabs->addTab(testTab, "3D Preview Test (temp)");

    twoDPanel_ = new TwoDPanel(tabs);
    tabs->addTab(twoDPanel_, "2D Flow Scenarios");
    // 3D Custom Geometry tab lands in Phase 6.8, once the orientation
    // dialog (6.7) that owns the mesh preview widget exists.

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
}

void MainWindow::showAbout() {
    QMessageBox::information(this, "About CFD Studio",
                              "CFD Studio (C++/Qt6 rewrite)\n\n"
                              "2D/3D incompressible Navier-Stokes solver with OpenFOAM/ParaView export.");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (twoDPanel_) twoDPanel_->shutdown();
    event->accept();
}
