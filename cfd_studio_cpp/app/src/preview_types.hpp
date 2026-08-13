#pragma once

#include <memory>

#include <QMetaType>

#include "pipeline/run_3d.hpp"
#include "solvers/navier_stokes_2d.hpp"

// Cross-thread signal payloads for the Sim2DWorker/Sim3DWorker ->
// GUI-thread plot updates. Wrapped in shared_ptr so an emit is a refcount
// bump, not a copy of the underlying field vectors; registered with
// qRegisterMetaType (see main.cpp) since Qt's queued cross-thread
// connections need custom types to be known to the meta-object system.
struct Preview2DSnapshot {
    std::shared_ptr<const cfd::solvers::Fields2D> fields;
    int nx = 0, ny = 0;
    double dx = 0.0, dy = 0.0;
};

struct Preview3DSnapshot {
    std::shared_ptr<const cfd::pipeline::Preview3DSlice> slice;
};

Q_DECLARE_METATYPE(Preview2DSnapshot)
Q_DECLARE_METATYPE(Preview3DSnapshot)
