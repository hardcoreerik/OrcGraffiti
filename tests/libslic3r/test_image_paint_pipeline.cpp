#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/ImagePaint/ImagePaintPipeline.hpp"
#include "libslic3r/ImagePaint/PaintStateMerge.hpp"

using namespace Slic3r::ImagePaint;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

// Unit cube: 8 vertices, 12 triangles with outward-facing normals.
static std::vector<Vec3f> cube_vertices()
{
    return {
        {0.f,0.f,0.f}, {1.f,0.f,0.f}, {1.f,1.f,0.f}, {0.f,1.f,0.f},
        {0.f,0.f,1.f}, {1.f,0.f,1.f}, {1.f,1.f,1.f}, {0.f,1.f,1.f},
    };
}

static std::vector<Vec3i32> cube_indices()
{
    // Outward-facing winding (CCW from outside):
    // Faces 0-1: bottom (z=0, normal -Z)
    // Faces 2-3: top    (z=1, normal +Z)  ← painted by top-down projector
    // Faces 4-5: front  (y=0, normal -Y)
    // Faces 6-7: back   (y=1, normal +Y)
    // Faces 8-9: left   (x=0, normal -X)
    // Faces 10-11: right (x=1, normal +X)
    return {
        {0,3,2}, {0,2,1},   // bottom
        {4,5,6}, {4,6,7},   // top
        {0,1,5}, {0,5,4},   // front
        {2,3,7}, {2,7,6},   // back
        {0,4,7}, {0,7,3},   // left
        {1,2,6}, {1,6,5},   // right
    };
}

// Solid RGBA8 image of given size and colour.
static DecodedImage make_solid_image(int w, int h,
                                      uint8_t r, uint8_t g, uint8_t b,
                                      uint8_t a = 255)
{
    DecodedImage img;
    img.width     = w;
    img.height    = h;
    img.has_alpha = (a != 255);
    img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i+0] = r; img.rgba[i+1] = g;
        img.rgba[i+2] = b; img.rgba[i+3] = a;
    }
    return img;
}

// Projector looking straight down (-Z), centred above the unit cube.
// front_face_cosine_threshold=0.5 restricts painting to nearly-horizontal faces
// (top/bottom), so only the +Z top face passes (dot=1) while sides (dot≈0) do not.
static PlanarProjectionSettings make_top_down_projector()
{
    PlanarProjectionSettings s;
    auto fr = make_projector_frame(Vec3d(0,0,-1), Vec3d(0,1,0), Vec3d(0.5,0.5,2.0));
    REQUIRE(fr.has_value());
    s.frame                      = *fr;
    s.width_mm                   = 2.0;
    s.height_mm                  = 2.0;
    s.front_face_cosine_threshold = 0.5;  // only ~perpendicular faces painted
    return s;
}

static std::vector<FilamentColor> one_red_filament()
{
    FilamentColor f;
    f.project_index = 0;
    f.name          = "Red";
    f.display_rgb   = {255, 0, 0};
    return {f};
}

static ImagePaintRequest base_request()
{
    ImagePaintRequest req;
    req.vertices     = cube_vertices();
    req.indices      = cube_indices();
    req.projection   = make_top_down_projector();
    req.filaments    = one_red_filament();
    req.quality      = SamplingQuality::FastCentroid;
    req.merge_policy = MergePolicy::OverwriteInsideMask;
    req.cleanup.enabled = false;
    req.quantization.target_colors = 1;
    return req;
}

// ---------------------------------------------------------------------------
// PaintStateMerge
// ---------------------------------------------------------------------------

TEST_CASE("merge_paint_states PreserveExisting leaves pre-painted faces alone", "[ImagePaint][Pipeline]")
{
    const std::vector<SelectorState> current  = {0, 2, 0};
    const std::vector<SelectorState> proposed = {1, 3, 1};
    const auto result = merge_paint_states(current, proposed, MergePolicy::PreserveExisting);
    CHECK(result[0] == 1);  // was 0 → painted
    CHECK(result[1] == 2);  // was 2 → kept
    CHECK(result[2] == 1);  // was 0 → painted
}

TEST_CASE("merge_paint_states OverwriteInsideMask replaces existing paint", "[ImagePaint][Pipeline]")
{
    const std::vector<SelectorState> current  = {0, 2, 0};
    const std::vector<SelectorState> proposed = {1, 3, 0};
    const auto result = merge_paint_states(current, proposed, MergePolicy::OverwriteInsideMask);
    CHECK(result[0] == 1);  // overwritten
    CHECK(result[1] == 3);  // overwritten
    CHECK(result[2] == 0);  // proposed=0 → no-op
}

TEST_CASE("merge_paint_states empty proposed returns current unchanged", "[ImagePaint][Pipeline]")
{
    const std::vector<SelectorState> current = {1, 2, 3};
    const auto result = merge_paint_states(current, {}, MergePolicy::OverwriteInsideMask);
    REQUIRE(result.size() == 3);
    CHECK(result[0] == 1);
    CHECK(result[1] == 2);
    CHECK(result[2] == 3);
}

TEST_CASE("make_blank_states returns all-zero vector of correct length", "[ImagePaint][Pipeline]")
{
    const auto blank = make_blank_states(7);
    REQUIRE(blank.size() == 7);
    for (auto s : blank)
        CHECK(s == kStateNone);
}

// ---------------------------------------------------------------------------
// Golden cube — end-to-end pipeline with pre-decoded image
// ---------------------------------------------------------------------------

TEST_CASE("run_image_paint top-down solid image paints only top face", "[ImagePaint][Pipeline]")
{
    const auto image = make_solid_image(64, 64, 255, 0, 0);  // solid red

    const auto result = run_image_paint(base_request(), image);
    REQUIRE(result.has_value());

    const auto& states = result->states;
    REQUIRE(states.size() == 12);

    // Faces 2 and 3 are the top face (+Z normal, front-facing from above).
    CHECK(states[2] == kStateExtruderMin);  // filament 0 → SelectorState 1
    CHECK(states[3] == kStateExtruderMin);

    // Bottom faces (0,1) are back-facing from above → not painted.
    CHECK(states[0] == kStateNone);
    CHECK(states[1] == kStateNone);
}

TEST_CASE("run_image_paint fully transparent image paints nothing", "[ImagePaint][Pipeline]")
{
    const auto image = make_solid_image(64, 64, 255, 0, 0, 0);  // alpha=0

    const auto result = run_image_paint(base_request(), image);
    REQUIRE(result.has_value());

    for (const auto s : result->states)
        CHECK(s == kStateNone);
}

TEST_CASE("run_image_paint PreserveExisting keeps pre-painted top face", "[ImagePaint][Pipeline]")
{
    auto req = base_request();
    req.merge_policy = MergePolicy::PreserveExisting;

    // Pre-paint face 2 with state 2 (some other filament).
    req.existing_states.assign(12, kStateNone);
    req.existing_states[2] = 2;

    const auto image = make_solid_image(64, 64, 255, 0, 0);

    const auto result = run_image_paint(req, image);
    REQUIRE(result.has_value());

    CHECK(result->states[2] == 2);               // pre-painted face preserved
    CHECK(result->states[3] == kStateExtruderMin); // adjacent top face painted
}

TEST_CASE("run_image_paint diagnostics count painted faces", "[ImagePaint][Pipeline]")
{
    const auto image = make_solid_image(64, 64, 255, 0, 0);

    const auto result = run_image_paint(base_request(), image);
    REQUIRE(result.has_value());

    const auto& d = result->diagnostics;
    CHECK(d.total_faces == 12);
    CHECK(d.painted_faces >= 1);    // at least the top face
    CHECK(d.painted_surface_area_mm2 > 0.0);
}

TEST_CASE("run_image_paint fingerprint is deterministic", "[ImagePaint][Pipeline]")
{
    const auto image = make_solid_image(4, 4, 255, 0, 0);

    const auto r1 = run_image_paint(base_request(), image);
    const auto r2 = run_image_paint(base_request(), image);
    REQUIRE(r1.has_value());
    REQUIRE(r2.has_value());
    CHECK(r1->fingerprint == r2->fingerprint);
}

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------

TEST_CASE("run_image_paint empty mesh returns NoEligibleFaces error", "[ImagePaint][Pipeline]")
{
    ImagePaintRequest req;
    req.filaments  = one_red_filament();
    req.projection = make_top_down_projector();

    const auto image  = make_solid_image(4, 4, 255, 0, 0);
    const auto result = run_image_paint(req, image);
    CHECK(!result.has_value());
    CHECK(result.error().code == ImagePaintErrorCode::NoEligibleFaces);
}

TEST_CASE("run_image_paint no filaments returns NoAvailableFilaments error", "[ImagePaint][Pipeline]")
{
    auto req     = base_request();
    req.filaments = {};  // clear filaments

    const auto image  = make_solid_image(4, 4, 255, 0, 0);
    const auto result = run_image_paint(req, image);
    CHECK(!result.has_value());
    CHECK(result.error().code == ImagePaintErrorCode::NoAvailableFilaments);
}
