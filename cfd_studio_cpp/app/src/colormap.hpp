#pragma once

#include <QColor>

// Shared 5-stop viridis-like colormap (simplified, not the real 256-entry
// viridis LUT, but perceptually similar and dependency-free) -- used by
// PlotWidget's 2D heatmaps, ResultsViewerWidget's slice-plane GLSL shader
// (a hand-written GLSL port of this same LUT, see
// widgets/results_viewer_widget.cpp), and ColorLegendWidget's color key.
// `t` is clamped to [0,1].
[[nodiscard]] QColor colormap_sample(double t);
