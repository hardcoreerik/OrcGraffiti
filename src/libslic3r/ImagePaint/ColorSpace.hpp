#pragma once

#include "ImagePaintTypes.hpp"

namespace Slic3r::ImagePaint {

// sRGB byte -> linear float [0,1].
// Uses the IEC 61966-2-1 piecewise formula.
float srgb_byte_to_linear(uint8_t v) noexcept;

// Linear float [0,1] -> sRGB byte.
uint8_t linear_to_srgb_byte(float v) noexcept;

// ColorRgb8 (sRGB) -> ColorRgbf (linear light).
ColorRgbf rgb8_to_linear(ColorRgb8 srgb) noexcept;

// Linear RGB -> CIE XYZ (D65 illuminant, standard observer 2°).
// Input channels are in [0,1].
struct ColorXyz { double x = 0, y = 0, z = 0; };
ColorXyz linear_to_xyz(ColorRgbf linear) noexcept;

// CIE XYZ -> CIE L*a*b* (D65 illuminant).
ColorLab xyz_to_lab(ColorXyz xyz) noexcept;

// Shortcut: sRGB8 -> Lab (the common pipeline path).
ColorLab rgb8_to_lab(ColorRgb8 srgb) noexcept;

// Shortcut: linear RGB -> Lab.
ColorLab linear_to_lab(ColorRgbf linear) noexcept;

// Lab -> XYZ -> linear RGB (clamps to [0,1]).
ColorRgbf lab_to_linear(ColorLab lab) noexcept;

} // namespace Slic3r::ImagePaint
