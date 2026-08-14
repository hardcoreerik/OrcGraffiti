#pragma once

#include "ImagePaintTypes.hpp"
#include "ImagePaintErrors.hpp"

#include "ImagePaintCompat.hpp"
#include <filesystem>

namespace Slic3r::ImagePaint {

// Decode an image file to a normalized RGBA8 DecodedImage.
// All supported formats (PNG, JPEG, BMP) are handled by OpenCV.
// Limits are checked BEFORE allocation to prevent decode-bomb attacks.
//
// Channel rules:
//   BGR  -> RGBA (alpha = 255)
//   BGRA -> RGBA
//   GRAY -> RGBA (grey replicated to RGB, alpha = 255)
//   JPEG is always opaque (has_alpha = false regardless of file content)
//
// UV convention: (u=0, v=0) = top-left pixel, matching the architecture doc.
// No network access. No metadata execution.
Expected<DecodedImage, ImagePaintError>
decode_image(const std::filesystem::path& path,
             const ImageDecodeLimits&     limits = {});

} // namespace Slic3r::ImagePaint
