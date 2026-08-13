#include "sim3d_worker.hpp"

#include <utility>

Sim3DWorker::Sim3DWorker(cfd::mesh::Mesh mesh, cfd::pipeline::Run3DOptions opts, QObject* parent)
    : QObject(parent), mesh_(std::move(mesh)), opts_(std::move(opts)) {
    opts_.stop_flag = &stopFlag_;
}

void Sim3DWorker::requestStop() {
    stopFlag_.store(true);
}

void Sim3DWorker::run() {
    try {
        auto result = cfd::pipeline::run_3d(
            mesh_, opts_, [this](int step, double residual) { emit progressChanged(step, residual); },
            [this](const cfd::pipeline::Preview3DSlice& slice) {
                Preview3DSnapshot snapshot;
                snapshot.slice = std::make_shared<cfd::pipeline::Preview3DSlice>(slice);
                emit previewReady(snapshot);
            });
        if (result.stopped) {
            emit stopped();
        } else {
            emit finished(QString::fromStdString(result.foam_path), result.was_cached);
        }
    } catch (const std::exception& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
    }
}
