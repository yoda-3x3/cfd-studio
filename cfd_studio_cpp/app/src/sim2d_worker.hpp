#pragma once

#include <atomic>

#include <QObject>
#include <QString>

#include "pipeline/run_2d.hpp"
#include "preview_types.hpp"

// Wraps cfd::pipeline::run_2d for a QThread -- port of ui/worker.py's
// SimulationWorker. Constructed on the GUI thread, then moveToThread'd;
// QThread::started should be connected to run().
class Sim2DWorker : public QObject {
    Q_OBJECT

public:
    explicit Sim2DWorker(cfd::pipeline::Run2DOptions opts, QObject* parent = nullptr);

    // Safe to call from any thread (sets an atomic flag checked once per
    // step inside run_2d) -- port of ui/worker.py's request_stop().
    void requestStop();

public slots:
    void run();

signals:
    void progressChanged(int step, double residual);
    void previewReady(Preview2DSnapshot snapshot);
    void finished(QString pvdPath);
    void stopped();
    void errorOccurred(QString message);

private:
    cfd::pipeline::Run2DOptions opts_;
    std::atomic<bool> stopFlag_{false};
};
