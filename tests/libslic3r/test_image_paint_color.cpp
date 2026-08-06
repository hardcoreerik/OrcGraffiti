#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/ImagePaint/ColorSpace.hpp"
#include "libslic3r/ImagePaint/ColorDifference.hpp"
#include "libslic3r/ImagePaint/ColorQuantizer.hpp"
#include "libslic3r/ImagePaint/FilamentMatcher.hpp"
#include "libslic3r/ImagePaint/FaceAdjacency.hpp"

using namespace Slic3r::ImagePaint;
using namespace Catch::Matchers;

// ---------------------------------------------------------------------------
// ColorSpace — sRGB <-> linear <-> Lab
// ---------------------------------------------------------------------------

TEST_CASE("srgb_byte_to_linear black is 0", "[ImagePaint][Color]")
{
    CHECK_THAT(srgb_byte_to_linear(0), WithinAbs(0.f, 1e-6f));
}

TEST_CASE("srgb_byte_to_linear white is 1", "[ImagePaint][Color]")
{
    CHECK_THAT(srgb_byte_to_linear(255), WithinAbs(1.f, 1e-4f));
}

TEST_CASE("srgb_byte_to_linear midpoint is below 0.5 linear", "[ImagePaint][Color]")
{
    // sRGB 128 is approximately 0.216 in linear light
    const float lin = srgb_byte_to_linear(128);
    CHECK(lin > 0.20f);
    CHECK(lin < 0.23f);
}

TEST_CASE("linear_to_srgb_byte round-trips cleanly for black and white", "[ImagePaint][Color]")
{
    CHECK(linear_to_srgb_byte(0.f)  == 0);
    CHECK(linear_to_srgb_byte(1.f)  == 255);
}

TEST_CASE("rgb8_to_lab pure white produces near-zero ab", "[ImagePaint][Color]")
{
    const auto lab = rgb8_to_lab(ColorRgb8{255, 255, 255});
    CHECK_THAT(lab.l, WithinAbs(100.0, 0.5));
    CHECK_THAT(lab.a, WithinAbs(0.0,   1.0));
    CHECK_THAT(lab.b, WithinAbs(0.0,   1.0));
}

TEST_CASE("rgb8_to_lab pure black produces L near 0", "[ImagePaint][Color]")
{
    const auto lab = rgb8_to_lab(ColorRgb8{0, 0, 0});
    CHECK_THAT(lab.l, WithinAbs(0.0, 0.5));
}

TEST_CASE("lab_to_linear round-trip is close for neutral grey", "[ImagePaint][Color]")
{
    const ColorRgbf grey{0.5f, 0.5f, 0.5f};
    const ColorLab  lab  = linear_to_lab(grey);
    const ColorRgbf back = lab_to_linear(lab);

    CHECK_THAT(back.r, WithinAbs(0.5f, 0.01f));
    CHECK_THAT(back.g, WithinAbs(0.5f, 0.01f));
    CHECK_THAT(back.b, WithinAbs(0.5f, 0.01f));
}

// ---------------------------------------------------------------------------
// CIEDE2000 — published reference pairs (Sharma et al. 2005, Table 1)
// Reference: doi:10.1002/col.20070
// Pairs chosen to cover the main correction terms.
// Tolerances: ±0.01 ΔE per the paper's test data.
// ---------------------------------------------------------------------------

struct De2000Pair {
    ColorLab a, b;
    double expected_de;
};

// A selection of Sharma et al. (2005) Table 1 reference pairs.
static const De2000Pair kRefPairs[] = {
    // Pair 1
    {{ 50.0000,  2.6772, -79.7751}, { 50.0000,  0.0000, -82.7485}, 2.0425},
    // Pair 2
    {{ 50.0000,  3.1571, -77.2803}, { 50.0000,  0.0000, -82.7485}, 2.8615},
    // Pair 6 (hue rotation term)
    {{ 50.0000, -1.3802, -84.2814}, { 50.0000,  0.0000, -82.7485}, 0.9082},
    // Pair 17 (RT term)
    {{ 50.0000,  49.1812, -56.3700}, { 50.0000,  49.1882, -56.3700}, 0.0009},
    // Identical colors
    {{ 50.0000,  0.0000,   0.0000}, { 50.0000,  0.0000,   0.0000}, 0.0000},
};

TEST_CASE("delta_e_2000 matches Sharma 2005 reference pairs", "[ImagePaint][Color]")
{
    for (const auto& p : kRefPairs) {
        const double de = delta_e_2000(p.a, p.b);
        CAPTURE(p.a.l, p.a.a, p.a.b, p.b.l, p.b.a, p.b.b, p.expected_de, de);
        CHECK_THAT(de, WithinAbs(p.expected_de, 0.01));
    }
}

TEST_CASE("delta_e_2000 is symmetric", "[ImagePaint][Color]")
{
    const ColorLab a{60.0, 10.0, -20.0};
    const ColorLab b{40.0, -5.0,  15.0};
    CHECK_THAT(delta_e_2000(a, b), WithinAbs(delta_e_2000(b, a), 1e-10));
}

TEST_CASE("delta_e_2000 identical colors returns 0", "[ImagePaint][Color]")
{
    const ColorLab c{50.0, 25.0, -30.0};
    CHECK_THAT(delta_e_2000(c, c), WithinAbs(0.0, 1e-10));
}

// ---------------------------------------------------------------------------
// ColorQuantizer
// ---------------------------------------------------------------------------

TEST_CASE("quantize_colors empty input returns empty", "[ImagePaint][Color]")
{
    QuantizationSettings s;
    const auto result = quantize_colors({}, s);
    CHECK(result.empty());
}

TEST_CASE("quantize_colors produces at most target_colors clusters", "[ImagePaint][Color]")
{
    // 10 random-ish Lab samples
    std::vector<ColorSample> samples;
    for (int i = 0; i < 10; ++i)
        samples.push_back({{i * 10.0, i * 2.0 - 10.0, 5.0}, 1.0});

    QuantizationSettings s;
    s.target_colors = 4;
    const auto clusters = quantize_colors(samples, s);
    CHECK(clusters.size() <= 4);
    CHECK(!clusters.empty());
}

TEST_CASE("quantize_colors is deterministic for same input", "[ImagePaint][Color]")
{
    std::vector<ColorSample> samples;
    for (int i = 0; i < 8; ++i)
        samples.push_back({{i * 12.0, i * 3.0, -i * 2.0}, 1.0});

    QuantizationSettings s;
    s.target_colors = 3;
    const auto r1 = quantize_colors(samples, s);
    const auto r2 = quantize_colors(samples, s);

    REQUIRE(r1.size() == r2.size());
    for (std::size_t i = 0; i < r1.size(); ++i) {
        CHECK(r1[i].representative.r == r2[i].representative.r);
        CHECK(r1[i].representative.g == r2[i].representative.g);
        CHECK(r1[i].representative.b == r2[i].representative.b);
    }
}

TEST_CASE("quantize_colors clusters have sequential IDs", "[ImagePaint][Color]")
{
    std::vector<ColorSample> samples = {
        {{10, 0, 0}, 1.0}, {{80, 0, 0}, 1.0}, {{50, 0, 0}, 1.0}
    };
    QuantizationSettings s;
    s.target_colors = 3;
    const auto clusters = quantize_colors(samples, s);
    for (std::uint32_t i = 0; i < clusters.size(); ++i)
        CHECK(clusters[i].id == i);
}

// ---------------------------------------------------------------------------
// FilamentMatcher — index conversion
// ---------------------------------------------------------------------------

TEST_CASE("filament_index_to_selector_state converts index 0 to state 1", "[ImagePaint][Color]")
{
    const auto result = filament_index_to_selector_state(0);
    REQUIRE(result.has_value());
    CHECK(*result == 1);
}

TEST_CASE("filament_index_to_selector_state converts index 15 to state 16", "[ImagePaint][Color]")
{
    const auto result = filament_index_to_selector_state(15);
    REQUIRE(result.has_value());
    CHECK(*result == 16);
}

TEST_CASE("filament_index_to_selector_state rejects index 16", "[ImagePaint][Color]")
{
    const auto result = filament_index_to_selector_state(16);
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ImagePaintErrorCode::FilamentOutOfRange);
}

TEST_CASE("selector_state_to_filament_index round-trips cleanly", "[ImagePaint][Color]")
{
    for (SelectorState s = kStateExtruderMin; s <= kStateExtruderMax; ++s) {
        const auto fi = selector_state_to_filament_index(s);
        REQUIRE(fi.has_value());
        const auto back = filament_index_to_selector_state(*fi);
        REQUIRE(back.has_value());
        CHECK(*back == s);
    }
}

TEST_CASE("selector_state_to_filament_index rejects state 0", "[ImagePaint][Color]")
{
    const auto result = selector_state_to_filament_index(0);
    REQUIRE(!result.has_value());
}

// ---------------------------------------------------------------------------
// FaceAdjacency — cube mesh
// ---------------------------------------------------------------------------

// Cube: 8 vertices, 12 triangles (2 per face × 6 faces).
static std::vector<Vec3i32> cube_indices_adj()
{
    return {
        {0,1,2}, {0,2,3},
        {4,6,5}, {4,7,6},
        {0,4,5}, {0,5,1},
        {3,2,6}, {3,6,7},
        {0,3,7}, {0,7,4},
        {1,5,6}, {1,6,2}
    };
}

TEST_CASE("build_face_adjacency cube has correct face count", "[ImagePaint][Adjacency]")
{
    const auto idx = cube_indices_adj();
    const auto adj = build_face_adjacency(idx);
    CHECK(adj.face_count() == 12);
}

TEST_CASE("build_face_adjacency each cube face has at least 1 neighbor", "[ImagePaint][Adjacency]")
{
    const auto idx = cube_indices_adj();
    const auto adj = build_face_adjacency(idx);
    for (FaceIndex fi = 0; fi < adj.face_count(); ++fi)
        CHECK(!adj.neighbors_of(fi).empty());
}

TEST_CASE("build_face_adjacency adjacency is symmetric", "[ImagePaint][Adjacency]")
{
    const auto idx = cube_indices_adj();
    const auto adj = build_face_adjacency(idx);

    for (FaceIndex fi = 0; fi < adj.face_count(); ++fi) {
        for (FaceIndex nb : adj.neighbors_of(fi)) {
            bool found = false;
            for (FaceIndex nb2 : adj.neighbors_of(nb))
                if (nb2 == fi) { found = true; break; }
            CHECK(found);
        }
    }
}

TEST_CASE("build_face_adjacency empty mesh returns empty adjacency", "[ImagePaint][Adjacency]")
{
    const auto adj = build_face_adjacency(std::span<const Vec3i32>{});
    CHECK(adj.face_count() == 0);
}
