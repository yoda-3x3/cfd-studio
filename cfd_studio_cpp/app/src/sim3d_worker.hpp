#pragma once

#include <atomic>

#include <QObject>
#include <QString>

#include "mesh/mesh.hpp"
#include "pipeline/run_3d.hpp"
#include "preview_types.hpp"

// Wraps cfd::pipeline::run_3d for a QThread -- port of ui/worker3d.py's
// Simulation3DWorker. `mesh` must already be oriented (constructed after
// the orientation dialog confirms).
class Sim3DWorker : public QObject {
    Q_OBJECT

public:
    Sim3DWorker(cfd::mesh::Mesh mesh, cfd::pipeline::Run3DOptions opts, QObject* parent = nullptr);

    void requestStop();

public slots:
    void run();

signals:
    void progressChanged(int step, double residual);
    void previewReady(Preview3DSnapshot snapshot);
    void finished(QString foamPath, bool wasCached, QString resultsCacheDir);
    void stopped();
    void errorOccurred(QString message);

private:
    cfd::mesh::Mesh mesh_;
    cfd::pipeline::Run3DOptions opts_;
    std::atomic<bool> stopFlag_{false};
};
