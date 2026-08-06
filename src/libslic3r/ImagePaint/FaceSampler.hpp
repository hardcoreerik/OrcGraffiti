#pragma once

#include "ImagePaintTypes.hpp"
#include "Projection.hpp"

#include "libslic3r/Point.hpp"

#include <array>
#include <vector>
#include <span>
#include <functional>

namespace Slic3r::ImagePaint {

// Sampling quality for face-colour queries.
enum class SamplingQuality {
    FastCentroid,  // 1 point — fast enough for interactive drag preview
    ThreePoint,    // 3 barycentric points — medium quality
    Gaussian7      // 7-point triangular Gaussian quadrature — deterministic final
};

// Per-face sample result in projector space.
struct FaceSample {
    FaceIndex  face_index    = 0;
    ColorRgbf  linear_rgb    = {};    // area-weighted average in linear light
    float      alpha         = 0.f;  // weighted coverage in [0, 1]
    float      coverage      = 0.f;  // fraction of sample points inside image
    float      average_depth = 0.f;  // mean projector-space depth
    bool       front_facing  = false;
    bool       inside        = false; // at least one sample inside image footprint
    bool       occluded      = false; // reserved for Phase 7
};

// Sample the image color for every face in [face_begin, face_end).
// vertices and indices describe the mesh in the same space as the projector frame.
// The cancellation callback is polled at regular intervals; returning true aborts.
std::vector<FaceSample>
sample_faces(
    std::span<const Vec3f>    vertices,
    std::span<const Vec3i32>  indices,
    const DecodedImage&        image,
    const PlanarProjectionSettings& proj,
    SamplingQuality            quality,
    std::size_t                face_begin,
    std::size_t                face_end,
    const std::function<bool()>& cancel = {});

// Bilinear sample of a decoded image at UV coordinates.
// u=0 left, u=1 right, v=0 top, v=1 bottom.
// Out-of-range UV returns transparent black (0,0,0,0).
ColorRgba8 sample_bilinear(const DecodedImage& image, double u, double v);

// Convert sRGB8 to linear float [0,1].
inline float srgb_to_linear(uint8_t v) noexcept
{
    const float f = v / 255.f;
    return f <= 0.04045f ? f / 12.92f : std::pow((f + 0.055f) / 1.055f, 2.4f);
}

// Seven-point triangular Gaussian quadrature barycentric coordinates and weights.
// Adapted from Bambu Studio TextureToColor/ColorUtils (algorithm concept only).
// Source: https://github.com/bambulab/BambuStudio
// Reference commit: f43cbe4296b16cb6089a47af8fe82d2b79d2442f
// License: GNU Affero General Public License v3.0
struct GaussianSampler7 {
    static constexpr int kN = 7;

    // [point][bary_coord]
    static constexpr std::array<std::array<float, 3>, kN> kBary = {{
        {1.f/3.f, 1.f/3.f, 1.f/3.f},
        {0.059715871f, 0.470142064f, 0.470142064f},
        {0.470142064f, 0.059715871f, 0.470142064f},
        {0.470142064f, 0.470142064f, 0.059715871f},
        {0.797426985f, 0.101286507f, 0.101286507f},
        {0.101286507f, 0.797426985f, 0.101286507f},
        {0.101286507f, 0.101286507f, 0.797426985f},
    }};

    static constexpr std::array<float, kN> kWeight = {
        0.225f,
        0.132394152f, 0.132394152f, 0.132394152f,
        0.125939181f, 0.125939181f, 0.125939181f,
    };

    // Verify weights sum to 1.0 (compile-time check via constexpr).
    static constexpr float weight_sum()
    {
        float s = 0.f;
        for (float w : kWeight) s += w;
        return s;
    }
    static_assert(weight_sum() > 0.999f && weight_sum() < 1.001f,
                  "Gaussian7 weights must sum to 1");
};

} // namespace Slic3r::ImagePaint
