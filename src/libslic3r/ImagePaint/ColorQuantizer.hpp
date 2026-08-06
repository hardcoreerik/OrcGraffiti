#pragma once

#include "ImagePaintTypes.hpp"
#include "ColorSpace.hpp"

#include <vector>
#include "ImagePaintCompat.hpp"
#include <functional>

namespace Slic3r::ImagePaint {

struct QuantizationSettings {
    std::uint32_t target_colors      = 4;
    std::uint32_t max_iterations     = 30;
    double        convergence_epsilon = 0.01;  // Lab distance
    bool          one_to_one_filament_match = true;
};

// One weighted color sample fed into the quantizer.
struct ColorSample {
    ColorLab lab;
    double   weight = 1.0;  // e.g. projected area in mm²
};

// Run deterministic Lab k-means and return cluster centroids.
// Seed: median-cut on the input samples (no random).
// Empty clusters are removed and the result is stable-sorted by luminance.
// Cancellation callback is polled between iterations.
std::vector<SourceCluster>
quantize_colors(Span<const ColorSample> samples,
                const QuantizationSettings&   settings,
                const std::function<bool()>&  cancel = {});

} // namespace Slic3r::ImagePaint
