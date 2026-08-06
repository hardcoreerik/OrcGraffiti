#pragma once

#include "ImagePaintTypes.hpp"

namespace Slic3r::ImagePaint {

// CIEDE2000 color difference metric.
// Reference: Sharma G, Wu W, Dalal EN (2005) "The CIEDE2000 color-difference
// formula: Implementation notes, supplementary test data, and mathematical
// observations." Color Research & Application 30(1):21-30.
//
// Returns ΔE₀₀ ≥ 0. Lower = more similar.
double delta_e_2000(const ColorLab& a, const ColorLab& b) noexcept;

// Simpler CIE76 ΔE (Euclidean Lab distance). Faster, less perceptually uniform.
double delta_e_76(const ColorLab& a, const ColorLab& b) noexcept;

} // namespace Slic3r::ImagePaint
