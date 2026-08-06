#include "ColorQuantizer.hpp"
#include "ColorDifference.hpp"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <limits>

namespace Slic3r::ImagePaint {

namespace {

// Median-cut seed: split the Lab bounding box recursively until we have
// target_k buckets, using the longest axis at each step.
// Returns initial centroid Lab values.
std::vector<ColorLab>
median_cut_seed(Span<const ColorSample> samples, std::uint32_t target_k)
{
    if (samples.empty() || target_k == 0)
        return {};

    struct Box {
        std::vector<std::size_t> indices;
    };

    std::vector<Box> boxes;
    boxes.push_back({});
    boxes[0].indices.resize(samples.size());
    std::iota(boxes[0].indices.begin(), boxes[0].indices.end(), 0);

    while (boxes.size() < target_k) {
        // Find the box with the largest range in any Lab channel.
        std::size_t split_box = 0;
        int split_channel = 0;
        double max_range = -1.0;

        for (std::size_t i = 0; i < boxes.size(); ++i) {
            const auto& idx = boxes[i].indices;
            if (idx.empty()) continue;

            double lo_l = samples[idx[0]].lab.l, hi_l = lo_l;
            double lo_a = samples[idx[0]].lab.a, hi_a = lo_a;
            double lo_b = samples[idx[0]].lab.b, hi_b = lo_b;

            for (std::size_t j : idx) {
                lo_l = std::min(lo_l, samples[j].lab.l); hi_l = std::max(hi_l, samples[j].lab.l);
                lo_a = std::min(lo_a, samples[j].lab.a); hi_a = std::max(hi_a, samples[j].lab.a);
                lo_b = std::min(lo_b, samples[j].lab.b); hi_b = std::max(hi_b, samples[j].lab.b);
            }

            const double ranges[3] = {hi_l - lo_l, hi_a - lo_a, hi_b - lo_b};
            for (int ch = 0; ch < 3; ++ch) {
                if (ranges[ch] > max_range) {
                    max_range  = ranges[ch];
                    split_box  = i;
                    split_channel = ch;
                }
            }
        }

        if (max_range < 1e-10 || boxes[split_box].indices.size() < 2)
            break;  // can't split further

        auto& idx = boxes[split_box].indices;
        // Sort on the split channel, then split at median.
        std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
            const double va = split_channel == 0 ? samples[a].lab.l
                            : split_channel == 1 ? samples[a].lab.a : samples[a].lab.b;
            const double vb = split_channel == 0 ? samples[b].lab.l
                            : split_channel == 1 ? samples[b].lab.a : samples[b].lab.b;
            return va < vb;
        });

        const std::size_t mid = idx.size() / 2;
        Box new_box;
        new_box.indices.assign(idx.begin() + mid, idx.end());
        idx.resize(mid);
        boxes.push_back(std::move(new_box));
    }

    // Compute weighted centroid of each box.
    std::vector<ColorLab> seeds;
    seeds.reserve(boxes.size());
    for (const auto& box : boxes) {
        if (box.indices.empty()) continue;
        double wl = 0, wa = 0, wb = 0, tw = 0;
        for (std::size_t i : box.indices) {
            const double w = samples[i].weight;
            wl += w * samples[i].lab.l;
            wa += w * samples[i].lab.a;
            wb += w * samples[i].lab.b;
            tw += w;
        }
        if (tw > 0)
            seeds.push_back({wl / tw, wa / tw, wb / tw});
    }

    return seeds;
}

} // namespace

std::vector<SourceCluster>
quantize_colors(Span<const ColorSample> samples,
                const QuantizationSettings&   settings,
                const std::function<bool()>&  cancel)
{
    if (samples.empty() || settings.target_colors == 0)
        return {};

    const std::uint32_t k = std::min(
        settings.target_colors,
        static_cast<std::uint32_t>(samples.size()));

    // Seed centroids via median-cut (deterministic).
    std::vector<ColorLab> centroids = median_cut_seed(samples, k);
    if (centroids.empty()) return {};

    const std::size_t K = centroids.size();
    std::vector<std::size_t> assignments(samples.size(), 0);

    // k-means iterations.
    for (std::uint32_t iter = 0; iter < settings.max_iterations; ++iter) {
        if (cancel && cancel()) break;

        // Assignment step.
        bool changed = false;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            double best = std::numeric_limits<double>::max();
            std::size_t best_k = 0;
            for (std::size_t j = 0; j < K; ++j) {
                const double d = delta_e_76(samples[i].lab, centroids[j]);
                if (d < best) { best = d; best_k = j; }
            }
            if (assignments[i] != best_k) { assignments[i] = best_k; changed = true; }
        }

        // Update step.
        std::vector<double> wl(K, 0), wa(K, 0), wb(K, 0), tw(K, 0);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const std::size_t c = assignments[i];
            const double w = samples[i].weight;
            wl[c] += w * samples[i].lab.l;
            wa[c] += w * samples[i].lab.a;
            wb[c] += w * samples[i].lab.b;
            tw[c] += w;
        }

        double max_shift = 0.0;
        for (std::size_t j = 0; j < K; ++j) {
            if (tw[j] <= 0.0) continue;
            const ColorLab new_c{wl[j] / tw[j], wa[j] / tw[j], wb[j] / tw[j]};
            max_shift = std::max(max_shift, delta_e_76(centroids[j], new_c));
            centroids[j] = new_c;
        }

        if (!changed || max_shift < settings.convergence_epsilon) break;
    }

    // Build clusters — skip empty ones.
    std::vector<std::pair<std::size_t, SourceCluster>> indexed;
    for (std::size_t j = 0; j < K; ++j) {
        std::uint64_t count = 0;
        double total_w = 0.0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (assignments[i] == j) { ++count; total_w += samples[i].weight; }
        }
        if (count == 0) continue;

        SourceCluster sc;
        sc.id = static_cast<std::uint32_t>(j);
        // Representative: convert Lab centroid to sRGB8.
        const ColorRgbf lin = lab_to_linear(centroids[j]);
        sc.representative = ColorRgb8{
            linear_to_srgb_byte(lin.r),
            linear_to_srgb_byte(lin.g),
            linear_to_srgb_byte(lin.b)
        };
        sc.sample_count = count;
        sc.projected_area_mm2 = total_w;
        indexed.push_back({j, sc});
    }

    // Stable sort by luminance (L*) of centroid — deterministic order.
    std::stable_sort(indexed.begin(), indexed.end(), [&](const auto& a, const auto& b) {
        return centroids[a.first].l < centroids[b.first].l;
    });

    // Re-assign sequential IDs after sort.
    std::vector<SourceCluster> result;
    result.reserve(indexed.size());
    for (std::uint32_t n = 0; n < indexed.size(); ++n) {
        auto sc = indexed[n].second;
        sc.id = n;
        result.push_back(sc);
    }
    return result;
}

} // namespace Slic3r::ImagePaint
