#pragma once

#include <QString>
#include <QStringList>

// Finds and launches ParaView -- Qt-dependent (QProcess/QDir), so it lives
// in app/ rather than a Qt-free library. Port of paraview_launcher.py's
// find_paraview_candidates/get_paraview_path/launch_paraview/set_paraview_path,
// minus the caching-via-module-global (the GUI owns the located path via
// QSettings instead -- see MainWindow's use of the same settings instance
// as the theme key).
namespace paraview_launcher {

// Common Windows install locations, newest-version-first if multiple
// ParaView installs are found (e.g. "C:\Program Files\ParaView 5.13.3").
[[nodiscard]] QStringList findCandidates();

// Launches paraview.exe, optionally opening `filePath` (a .pvd or .foam
// case). Returns false if `paraviewExe` doesn't exist or the process
// fails to start.
bool launch(const QString& paraviewExe, const QString& filePath = QString());

} // namespace paraview_launcher
