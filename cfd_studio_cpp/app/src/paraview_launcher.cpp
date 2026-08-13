#include "paraview_launcher.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <algorithm>

namespace paraview_launcher {

QStringList findCandidates() {
    QStringList candidates;
    QDir programFiles("C:/Program Files");
    for (const QString& entry : programFiles.entryList({"ParaView*"}, QDir::Dirs, QDir::Name)) {
        QString exe = programFiles.filePath(entry + "/bin/paraview.exe");
        if (QFileInfo::exists(exe)) candidates.append(exe);
    }
    // Newest version first (lexicographic works for "ParaView 5.13.3"-style names).
    std::sort(candidates.begin(), candidates.end(), std::greater<QString>());
    return candidates;
}

bool launch(const QString& paraviewExe, const QString& filePath) {
    if (!QFileInfo::exists(paraviewExe)) return false;
    QStringList args;
    if (!filePath.isEmpty()) args << filePath;
    return QProcess::startDetached(paraviewExe, args);
}

} // namespace paraview_launcher
