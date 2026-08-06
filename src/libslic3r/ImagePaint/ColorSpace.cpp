#include "ColorSpace.hpp"

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Slic3r::ImagePaint {

namespace {

// D65 reference white in XYZ.
constexpr double kD65X = 0.95047;
constexpr double kD65Y = 1.00000;
constexpr double kD65Z = 1.08883;

// f(t) for CIE Lab conversion.
constexpr double kDelta  = 6.0 / 29.0;
constexpr double kDelta2 = kDelta * kDelta;
constexpr double kDelta3 = kDelta * kDelta * kDelta;

inline double lab_f(double t) noexcept
{
    return t > kDelta3
           ? std::cbrt(t)
           : t / (3.0 * kDelta2) + 4.0 / 29.0;
}

inline double lab_f_inv(double t) noexcept
{
    return t > kDelta
           ? t * t * t
           : 3.0 * kDelta2 * (t - 4.0 / 29.0);
}

} // namespace

float srgb_byte_to_linear(uint8_t v) noexcept
{
    const float f = v / 255.f;
    return f <= 0.04045f
           ? f / 12.92f
           : std::pow((f + 0.055f) / 1.055f, 2.4f);
}

uint8_t linear_to_srgb_byte(float v) noexcept
{
    const float c = v <= 0.0031308f
                    ? v * 12.92f
                    : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(
        std::clamp(std::lround(c * 255.f), 0L, 255L));
}

ColorRgbf rgb8_to_linear(ColorRgb8 srgb) noexcept
{
    return ColorRgbf{
        srgb_byte_to_linear(srgb.r),
        srgb_byte_to_linear(srgb.g),
        srgb_byte_to_linear(srgb.b)
    };
}

// sRGB primaries matrix (IEC 61966-2-1, D65).
ColorXyz linear_to_xyz(ColorRgbf lin) noexcept
{
    const double r = lin.r, g = lin.g, b = lin.b;
    return ColorXyz{
        r * 0.4124564 + g * 0.3575761 + b * 0.1804375,
        r * 0.2126729 + g * 0.7151522 + b * 0.0721750,
        r * 0.0193339 + g * 0.1191920 + b * 0.9503041
    };
}

ColorLab xyz_to_lab(ColorXyz xyz) noexcept
{
    const double fx = lab_f(xyz.x / kD65X);
    const double fy = lab_f(xyz.y / kD65Y);
    const double fz = lab_f(xyz.z / kD65Z);
    return ColorLab{
        116.0 * fy - 16.0,
        500.0 * (fx - fy),
        200.0 * (fy - fz)
    };
}

ColorLab rgb8_to_lab(ColorRgb8 srgb) noexcept
{
    return linear_to_lab(rgb8_to_linear(srgb));
}

ColorLab linear_to_lab(ColorRgbf linear) noexcept
{
    return xyz_to_lab(linear_to_xyz(linear));
}

ColorRgbf lab_to_linear(ColorLab lab) noexcept
{
    const double fy = (lab.l + 16.0) / 116.0;
    const double fx = lab.a / 500.0 + fy;
    const double fz = fy - lab.b / 200.0;

    const ColorXyz xyz{
        kD65X * lab_f_inv(fx),
        kD65Y * lab_f_inv(fy),
        kD65Z * lab_f_inv(fz)
    };

    // XYZ -> linear sRGB (inverse of linear_to_xyz matrix).
    const double xr =  3.2404542 * xyz.x - 1.5371385 * xyz.y - 0.4985314 * xyz.z;
    const double xg = -0.9692660 * xyz.x + 1.8760108 * xyz.y + 0.0415560 * xyz.z;
    const double xb =  0.0556434 * xyz.x - 0.2040259 * xyz.y + 1.0572252 * xyz.z;

    return ColorRgbf{
        static_cast<float>(std::clamp(xr, 0.0, 1.0)),
        static_cast<float>(std::clamp(xg, 0.0, 1.0)),
        static_cast<float>(std::clamp(xb, 0.0, 1.0))
    };
}

} // namespace Slic3r::ImagePaint
