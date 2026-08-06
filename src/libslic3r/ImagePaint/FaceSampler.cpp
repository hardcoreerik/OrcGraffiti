#include "FaceSampler.hpp"

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

#include <atomic>
#include <cmath>
#include <cassert>

namespace Slic3r::ImagePaint {

namespace {

// Compute the world-space normal of a triangle (not normalized).
Vec3d triangle_normal(const Vec3f& a, const Vec3f& b, const Vec3f& c)
{
    const Vec3d ab = (b - a).cast<double>();
    const Vec3d ac = (c - a).cast<double>();
    return ab.cross(ac);
}

// Project a 3D point (double) through the settings and sample the image.
// Returns {linear_rgb, alpha} or {black, 0} if out of bounds.
std::pair<ColorRgbf, float>
sample_point(const Vec3d& p,
             const PlanarProjectionSettings& proj,
             const DecodedImage& image)
{
    const auto pp = project_planar(p, proj);
    const ColorRgba8 px = sample_bilinear(image, pp.u, pp.v);
    const float a = px.a / 255.f;
    ColorRgbf linear{
        srgb_to_linear(px.r),
        srgb_to_linear(px.g),
        srgb_to_linear(px.b)
    };
    return {linear, a};
}

FaceSample sample_one_face(
    FaceIndex                         face_idx,
    const Vec3f&                      va,
    const Vec3f&                      vb,
    const Vec3f&                      vc,
    const DecodedImage&               image,
    const PlanarProjectionSettings&   proj,
    SamplingQuality                   quality)
{
    FaceSample result;
    result.face_index = face_idx;

    // Front-facing test using the projector normal.
    const Vec3d n = triangle_normal(va, vb, vc);
    const double dot = n.dot(-proj.frame.normal);
    result.front_facing = dot >= proj.front_face_cosine_threshold * n.norm();

    if (!proj.paint_through && !result.front_facing)
        return result;

    // Build sample points from barycentric coordinates.
    // centroid used for depth
    const Vec3d centroid = ((va + vb + vc).cast<double>()) / 3.0;
    const auto  cp = project_planar(centroid, proj);
    result.average_depth = static_cast<float>(cp.depth);

    switch (quality) {
    case SamplingQuality::FastCentroid: {
        auto [lin, a] = sample_point(centroid, proj, image);
        result.coverage   = cp.inside ? a : 0.f;
        result.alpha      = result.coverage;
        result.linear_rgb = lin;
        result.inside     = cp.inside && a > 0.f;
        break;
    }
    case SamplingQuality::ThreePoint: {
        // Vertices as sample points with equal weights.
        constexpr float w = 1.f / 3.f;
        float sum_w_a = 0.f, sum_w = 0.f;
        ColorRgbf acc{};
        bool any_inside = false;

        for (const auto& vx : {va, vb, vc}) {
            const Vec3d pd = vx.cast<double>();
            auto [lin, a] = sample_point(pd, proj, image);
            const auto pp2 = project_planar(pd, proj);
            if (pp2.inside) {
                any_inside = true;
                sum_w_a += w * a;
                acc.r += w * a * lin.r;
                acc.g += w * a * lin.g;
                acc.b += w * a * lin.b;
            }
            sum_w += w;
        }
        result.coverage = sum_w > 0.f ? (any_inside ? 1.f : 0.f) : 0.f;
        result.alpha    = sum_w_a;
        if (sum_w_a > 0.f) {
            result.linear_rgb = {acc.r / sum_w_a, acc.g / sum_w_a, acc.b / sum_w_a};
        }
        result.inside = any_inside && result.alpha > proj.alpha_threshold;
        break;
    }
    case SamplingQuality::Gaussian7: {
        float sum_w_a = 0.f;
        ColorRgbf acc{};
        bool any_inside = false;
        float points_inside = 0.f;

        const Vec3d da = va.cast<double>();
        const Vec3d db = vb.cast<double>();
        const Vec3d dc = vc.cast<double>();

        for (int i = 0; i < GaussianSampler7::kN; ++i) {
            const auto& b  = GaussianSampler7::kBary[i];
            const float  w = GaussianSampler7::kWeight[i];

            const Vec3d sample_pt = b[0]*da + b[1]*db + b[2]*dc;
            const auto pp2 = project_planar(sample_pt, proj);

            if (pp2.inside) {
                points_inside += w;
                auto [lin, a] = sample_point(sample_pt, proj, image);
                sum_w_a += w * a;
                acc.r += w * a * lin.r;
                acc.g += w * a * lin.g;
                acc.b += w * a * lin.b;
                if (a > 0.f) any_inside = true;
            }
        }

        result.coverage = points_inside;
        result.alpha    = sum_w_a;
        if (sum_w_a > 0.f) {
            result.linear_rgb = {acc.r / sum_w_a, acc.g / sum_w_a, acc.b / sum_w_a};
        }
        result.inside = any_inside && result.alpha >= static_cast<float>(proj.alpha_threshold);
        break;
    }
    }

    return result;
}

} // namespace

ColorRgba8 sample_bilinear(const DecodedImage& image, double u, double v)
{
    if (image.width == 0 || image.height == 0)
        return {};

    // Map UV to pixel coordinates. (0,0) = top-left texel centre.
    const double px = u * image.width  - 0.5;
    const double py = v * image.height - 0.5;

    const int x0 = static_cast<int>(std::floor(px));
    const int y0 = static_cast<int>(std::floor(py));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;

    const float fx = static_cast<float>(px - x0);
    const float fy = static_cast<float>(py - y0);

    // Clamp to valid range — out-of-range returns transparent black.
    auto clamp_x = [&](int x) { return x >= 0 && x < image.width  ? x : -1; };
    auto clamp_y = [&](int y) { return y >= 0 && y < image.height ? y : -1; };

    auto fetch = [&](int x, int y) -> ColorRgba8 {
        const int cx = clamp_x(x);
        const int cy = clamp_y(y);
        if (cx < 0 || cy < 0) return ColorRgba8{0, 0, 0, 0};
        const std::size_t idx = (static_cast<std::size_t>(cy) * image.width + cx) * 4;
        assert(idx + 3 < image.rgba.size());
        return ColorRgba8{image.rgba[idx], image.rgba[idx+1],
                          image.rgba[idx+2], image.rgba[idx+3]};
    };

    const auto c00 = fetch(x0, y0);
    const auto c10 = fetch(x1, y0);
    const auto c01 = fetch(x0, y1);
    const auto c11 = fetch(x1, y1);

    // Bilinear interpolation per channel.
    auto lerp_ch = [&](uint8_t a, uint8_t b, uint8_t c, uint8_t d) -> uint8_t {
        const float top = a + fx * (b - a);
        const float bot = c + fx * (d - c);
        return static_cast<uint8_t>(std::clamp(top + fy * (bot - top), 0.f, 255.f));
    };

    return ColorRgba8{
        lerp_ch(c00.r, c10.r, c01.r, c11.r),
        lerp_ch(c00.g, c10.g, c01.g, c11.g),
        lerp_ch(c00.b, c10.b, c01.b, c11.b),
        lerp_ch(c00.a, c10.a, c01.a, c11.a),
    };
}

std::vector<FaceSample>
sample_faces(
    Span<const Vec3f>    vertices,
    Span<const Vec3i32>  indices,
    const DecodedImage&        image,
    const PlanarProjectionSettings& proj,
    SamplingQuality            quality,
    std::size_t                face_begin,
    std::size_t                face_end,
    const std::function<bool()>& cancel)
{
    assert(face_end <= indices.size());
    const std::size_t count = face_end - face_begin;
    if (count == 0) return {};

    // Pre-size so threads can write by index without synchronisation.
    std::vector<FaceSample> results(count);

    // Shared cancellation flag polled by each TBB grain.
    std::atomic<bool> canceled{false};

    tbb::parallel_for(
        tbb::blocked_range<std::size_t>(face_begin, face_end, /*grain=*/256),
        [&](const tbb::blocked_range<std::size_t>& range) {
            if (canceled.load(std::memory_order_relaxed)) return;
            if (cancel && cancel()) {
                canceled.store(true, std::memory_order_relaxed);
                return;
            }

            for (std::size_t i = range.begin(); i < range.end(); ++i) {
                if (canceled.load(std::memory_order_relaxed)) break;

                const Vec3i32& tri = indices[i];
                assert(tri[0] >= 0 && static_cast<std::size_t>(tri[0]) < vertices.size());
                assert(tri[1] >= 0 && static_cast<std::size_t>(tri[1]) < vertices.size());
                assert(tri[2] >= 0 && static_cast<std::size_t>(tri[2]) < vertices.size());

                results[i - face_begin] = sample_one_face(
                    static_cast<FaceIndex>(i),
                    vertices[tri[0]],
                    vertices[tri[1]],
                    vertices[tri[2]],
                    image, proj, quality);
            }
        });

    return results;
}

} // namespace Slic3r::ImagePaint
