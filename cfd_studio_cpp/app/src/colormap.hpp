#pragma once

#include <QColor>

// Shared 5-stop viridis-like colormap (simplified, not the real 256-entry
// viridis LUT, but perceptually similar and dependency-free) -- used by
// PlotWidget's 2D heatmaps, ResultsViewerWidget's slice-plane GLSL shader
// (a hand-written GLSL port of this same LUT, see
// widgets/results_viewer_widget.cpp), and ColorLegendWidget's color key.
// `t` is clamped to [0,1].
[[nodiscard]] QColor colormap_sample(double t);

// Classic blue-cyan-green-yellow-red "jet"-style rainbow, used only by
// ResultsViewerWidget's streamlines/arrow glyphs -- a more vivid, wider-
// gamut palette than colormap_sample() for the flow-ribbon look reference
// CFD visualizations (e.g. ParaView's default rainbow LUT) typically use,
// intentionally kept separate from colormap_sample() so the slice
// heatmap/legend (which must stay visually consistent with PlotWidget's 2D
// plots elsewhere in the app) is unaffected. `t` is clamped to [0,1].
[[nodiscard]] QColor flow_colormap_sample(double t);
