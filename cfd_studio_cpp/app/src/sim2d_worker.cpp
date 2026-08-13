#include "sim2d_worker.hpp"

#include <utility>

Sim2DWorker::Sim2DWorker(cfd::pipeline::Run2DOptions opts, QObject* parent)
    : QObject(parent), opts_(std::move(opts)) {
    opts_.stop_flag = &stopFlag_;
}

void Sim2DWorker::requestStop() {
    stopFlag_.store(true);
}

void Sim2DWorker::run() {
    try {
        auto result = cfd::pipeline::run_2d(
            opts_, [this](int step, double residual) { emit progressChanged(step, residual); },
            [this](const cfd::solvers::Fields2D& fields, int nx, int ny, double dx, double dy) {
                Preview2DSnapshot snapshot;
                snapshot.fields = std::make_shared<cfd::solvers::Fields2D>(fields);
                snapshot.nx = nx;
                snapshot.ny = ny;
                snapshot.dx = dx;
                snapshot.dy = dy;
                emit previewReady(snapshot);
            });
        if (result.stopped) {
            emit stopped();
        } else {
            emit finished(QString::fromStdString(result.pvd_path));
        }
    } catch (const std::exception& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
    }
}
