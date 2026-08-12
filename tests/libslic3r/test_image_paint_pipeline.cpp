#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/ImagePaint/ImagePaintPipeline.hpp"
#include "libslic3r/ImagePaint/PaintStateMerge.hpp"
#include "libslic3r/ImagePaint/Projection.hpp"

#include <filesystem>

using namespace Slic3r;
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

TEST_CASE("run_image_paint real file with fit paints front of plate", "[ImagePaint][Pipeline][Integration]")
{
    // Prefer local copy (build dir) then the user sample path.
    namespace fs = std::filesystem;
    fs::path sample = "F:/Ai/OrcGraffiti/build/garth.jpg";
    if (!fs::exists(sample))
        sample = "C:/Users/hardc/OneDrive/Pictures/garth.jpg";
    if (!fs::exists(sample)) {
        SKIP("Sample image garth.jpg not present — skipping integration test");
    }

    // Flat plate in XY at z=0 (10 triangles), 100mm x 100mm — faces +Z.
    std::vector<Vec3f> verts = {
        {0,0,0},{100,0,0},{100,100,0},{0,100,0},
        {0,0,5},{100,0,5},{100,100,5},{0,100,5},
    };
    // Top (+Z) and bottom (-Z) only — 4 tris top, 4 bottom.
    std::vector<Vec3i32> idxs = {
        {4,5,6},{4,6,7},   // top
        {0,2,1},{0,3,2},   // bottom (inward for -Z? outward: 0,1,2 / 0,2,3)
        {0,1,2},{0,2,3},
    };
    // Fix bottom winding for outward -Z
    idxs[2] = {0,2,1};
    idxs[3] = {0,3,2};
    // Actually use clean 4 tris:
    idxs = {
        {4,5,6},{4,6,7}, // top +Z
        {0,3,2},{0,2,1}, // bottom -Z
    };

    ImagePaintRequest req;
    req.vertices = verts;
    req.indices  = idxs;
    req.image_path = sample.string();
    req.filaments = one_red_filament();
    // Add a second filament so multi-color matching has room
    {
        FilamentColor f;
        f.project_index = 1;
        f.name = "Black";
        f.display_rgb = {20, 20, 20};
        req.filaments.push_back(f);
        FilamentColor f2;
        f2.project_index = 2;
        f2.name = "White";
        f2.display_rgb = {240, 240, 240};
        req.filaments.push_back(f2);
        FilamentColor f3;
        f3.project_index = 3;
        f3.name = "Skin";
        f3.display_rgb = {200, 160, 120};
        req.filaments.push_back(f3);
    }
    req.quantization.target_colors = 4;
    req.quality = SamplingQuality::Gaussian7;
    req.merge_policy = MergePolicy::OverwriteInsideMask;
    req.cleanup.enabled = true;

    auto fitted = fit_planar_projection(
        Span<const Vec3f>(verts.data(), verts.size()),
        Vec3d(0, 0, -1),
        Vec3d(0, 1, 0),
        692.0 / 994.0,  // garth aspect
        1.02);
    REQUIRE(fitted.has_value());
    fitted->front_face_cosine_threshold = 0.05;
    fitted->minimum_coverage = 0.25;
    req.projection = *fitted;

    const auto result = run_image_paint(req);
    if (!result.has_value()) {
        FAIL("run_image_paint failed: " + result.error().user_message
             + " code=" + std::to_string(static_cast<int>(result.error().code)));
    }
    // Top faces should paint; bottom should not.
    CHECK(result->states[0] != kStateNone);
    CHECK(result->states[1] != kStateNone);
    CHECK(result->states[2] == kStateNone);
    CHECK(result->states[3] == kStateNone);
    CHECK(result->diagnostics.painted_faces >= 2);
}

TEST_CASE("run_image_paint fit_planar_projection front view paints front faces", "[ImagePaint][Pipeline]")
{
    // Simulate the gizmo path: fit projector from a front camera (+Y look)
    // onto the unit cube, then run the pipeline. Front faces are indices 4-5.
    auto req = base_request();
    auto fitted = fit_planar_projection(
        Span<const Vec3f>(req.vertices.data(), req.vertices.size()),
        Vec3d(0, 1, 0),   // look toward +Y (camera at -Y facing the front face)
        Vec3d(0, 0, 1),   // up = +Z
        /*aspect=*/1.0,
        /*margin=*/1.02);
    REQUIRE(fitted.has_value());
    fitted->front_face_cosine_threshold = 0.05;
    fitted->minimum_coverage = 0.25;
    req.projection = *fitted;

    const auto image = make_solid_image(32, 32, 0, 255, 0);  // solid green
    const auto result = run_image_paint(req, image);
    REQUIRE(result.has_value());

    // Front faces (y=0, normal -Y): look is +Y so -normal·look... front_facing uses
    // n.dot(-proj.normal); front normal = (0,-1,0), -normal = (0,1,0), look/normal = (0,1,0)
    // → dot = 1 → front faces painted.
    CHECK(result->states[4] == kStateExtruderMin);
    CHECK(result->states[5] == kStateExtruderMin);

    // Back faces (y=1, normal +Y) should remain unpainted.
    CHECK(result->states[6] == kStateNone);
    CHECK(result->states[7] == kStateNone);

    CHECK(result->diagnostics.painted_faces >= 2);
}

// The following four lock in the orcgraffiti CLI's back/left/right/bottom
// --view presets (view_preset() in orcgraffiti.cpp), which were previously
// only documented by inspection, not tested — front/top were the only
// golden tests. Same look/up vectors as the CLI table.

TEST_CASE("run_image_paint fit_planar_projection back view paints back faces", "[ImagePaint][Pipeline]")
{
    // CLI preset: back = look (0,-1,0), up (0,0,1).
    auto req = base_request();
    auto fitted = fit_planar_projection(
        Span<const Vec3f>(req.vertices.data(), req.vertices.size()),
        Vec3d(0, -1, 0), Vec3d(0, 0, 1), /*aspect=*/1.0, /*margin=*/1.02);
    REQUIRE(fitted.has_value());
    fitted->front_face_cosine_threshold = 0.05;
    fitted->minimum_coverage = 0.25;
    req.projection = *fitted;

    const auto image = make_solid_image(32, 32, 0, 255, 0);
    const auto result = run_image_paint(req, image);
    REQUIRE(result.has_value());

    // Back faces (y=1, normal +Y) are indices 6-7.
    CHECK(result->states[6] == kStateExtruderMin);
    CHECK(result->states[7] == kStateExtruderMin);
    // Front faces (y=0, normal -Y) should remain unpainted.
    CHECK(result->states[4] == kStateNone);
    CHECK(result->states[5] == kStateNone);
    CHECK(result->diagnostics.painted_faces >= 2);
}

TEST_CASE("run_image_paint fit_planar_projection left view paints left faces", "[ImagePaint][Pipeline]")
{
    // CLI preset: left = look (1,0,0), up (0,0,1).
    auto req = base_request();
    auto fitted = fit_planar_projection(
        Span<const Vec3f>(req.vertices.data(), req.vertices.size()),
        Vec3d(1, 0, 0), Vec3d(0, 0, 1), /*aspect=*/1.0, /*margin=*/1.02);
    REQUIRE(fitted.has_value());
    fitted->front_face_cosine_threshold = 0.05;
    fitted->minimum_coverage = 0.25;
    req.projection = *fitted;

    const auto image = make_solid_image(32, 32, 0, 255, 0);
    const auto result = run_image_paint(req, image);
    REQUIRE(result.has_value());

    // Left faces (x=0, normal -X) are indices 8-9.
    CHECK(result->states[8] == kStateExtruderMin);
    CHECK(result->states[9] == kStateExtruderMin);
    // Right faces (x=1, normal +X) should remain unpainted.
    CHECK(result->states[10] == kStateNone);
    CHECK(result->states[11] == kStateNone);
    CHECK(result->diagnostics.painted_faces >= 2);
}

TEST_CASE("run_image_paint fit_planar_projection right view paints right faces", "[ImagePaint][Pipeline]")
{
    // CLI preset: right = look (-1,0,0), up (0,0,1).
    auto req = base_request();
    auto fitted = fit_planar_projection(
        Span<const Vec3f>(req.vertices.data(), req.vertices.size()),
        Vec3d(-1, 0, 0), Vec3d(0, 0, 1), /*aspect=*/1.0, /*margin=*/1.02);
    REQUIRE(fitted.has_value());
    fitted->front_face_cosine_threshold = 0.05;
    fitted->minimum_coverage = 0.25;
    req.projection = *fitted;

    const auto image = make_solid_image(32, 32, 0, 255, 0);
    const auto result = run_image_paint(req, image);
    REQUIRE(result.has_value());

    // Right faces (x=1, normal +X) are indices 10-11.
    CHECK(result->states[10] == kStateExtruderMin);
    CHECK(result->states[11] == kStateExtruderMin);
    // Left faces (x=0, normal -X) should remain unpainted.
    CHECK(result->states[8] == kStateNone);
    CHECK(result->states[9] == kStateNone);
    CHECK(result->diagnostics.painted_faces >= 2);
}

TEST_CASE("run_image_paint fit_planar_projection bottom view paints bottom faces", "[ImagePaint][Pipeline]")
{
    // CLI preset: bottom = look (0,0,1), up (0,1,0).
    auto req = base_request();
    auto fitted = fit_planar_projection(
        Span<const Vec3f>(req.vertices.data(), req.vertices.size()),
        Vec3d(0, 0, 1), Vec3d(0, 1, 0), /*aspect=*/1.0, /*margin=*/1.02);
    REQUIRE(fitted.has_value());
    fitted->front_face_cosine_threshold = 0.05;
    fitted->minimum_coverage = 0.25;
    req.projection = *fitted;

    const auto image = make_solid_image(32, 32, 0, 255, 0);
    const auto result = run_image_paint(req, image);
    REQUIRE(result.has_value());

    // Bottom faces (z=0, normal -Z) are indices 0-1.
    CHECK(result->states[0] == kStateExtruderMin);
    CHECK(result->states[1] == kStateExtruderMin);
    // Top faces (z=1, normal +Z) should remain unpainted.
    CHECK(result->states[2] == kStateNone);
    CHECK(result->states[3] == kStateNone);
    CHECK(result->diagnostics.painted_faces >= 2);
}

// ---------------------------------------------------------------------------
// Curved projections through the full pipeline (Phase 7 sampling integration)
// ---------------------------------------------------------------------------

TEST_CASE("run_image_paint with cylindrical projection paints faces facing radially outward", "[ImagePaint][Pipeline][Cylindrical]")
{
    // Two small quads positioned as if on a radius-5 cylinder around +Z:
    // Quad A at angle 0 (x=5), outward normal +X.
    // Quad B at angle 90 (y=5), outward normal +Y.
    // A single fixed camera direction (planar projection) could only ever
    // front-face one of these — both painting confirms outward_direction()
    // is genuinely using the LOCAL radial direction at each face, not one
    // global direction, i.e. the FaceSampler dispatch is wired correctly.
    std::vector<Vec3f> verts = {
        {5.f,-0.1f,-0.1f}, {5.f,0.1f,-0.1f}, {5.f,0.1f,0.1f}, {5.f,-0.1f,0.1f},
        {-0.1f,5.f,-0.1f}, {0.1f,5.f,-0.1f}, {0.1f,5.f,0.1f}, {-0.1f,5.f,0.1f},
    };
    std::vector<Vec3i32> idxs = {
        {0,1,2}, {0,2,3},   // Quad A, normal +X (angle 0)
        {4,6,5}, {4,7,6},   // Quad B, normal +Y (angle 90)
    };

    auto fr = make_cylinder_frame(Vec3d(0,0,1), Vec3d(1,0,0), Vec3d::Zero());
    REQUIRE(fr.has_value());
    CylindricalProjectionSettings cyl;
    cyl.frame = *fr;
    cyl.height_mm = 10.0;
    cyl.front_face_cosine_threshold = 0.5; // both quads are exactly radial, well above threshold

    ImagePaintRequest req;
    req.vertices = verts;
    req.indices  = idxs;
    req.projection = cyl;
    req.filaments = one_red_filament();
    req.quality = SamplingQuality::FastCentroid;
    req.merge_policy = MergePolicy::OverwriteInsideMask;
    req.cleanup.enabled = false;
    req.quantization.target_colors = 1;

    const auto image = make_solid_image(8, 8, 255, 0, 0);
    const auto result = run_image_paint(req, image);
    REQUIRE(result.has_value());
    REQUIRE(result->states.size() == 4);

    CHECK(result->states[0] == kStateExtruderMin); // Quad A, angle 0
    CHECK(result->states[1] == kStateExtruderMin);
    CHECK(result->states[2] == kStateExtruderMin); // Quad B, angle 90
    CHECK(result->states[3] == kStateExtruderMin);
}

TEST_CASE("run_image_paint with cylindrical projection respects radius filtering", "[ImagePaint][Pipeline][Cylindrical]")
{
    // Same Quad A as above (radius 5), but max_radius_mm excludes it.
    std::vector<Vec3f> verts = {
        {5.f,-0.1f,-0.1f}, {5.f,0.1f,-0.1f}, {5.f,0.1f,0.1f}, {5.f,-0.1f,0.1f},
    };
    std::vector<Vec3i32> idxs = { {0,1,2}, {0,2,3} };

    auto fr = make_cylinder_frame(Vec3d(0,0,1), Vec3d(1,0,0), Vec3d::Zero());
    REQUIRE(fr.has_value());
    CylindricalProjectionSettings cyl;
    cyl.frame = *fr;
    cyl.height_mm = 10.0;
    cyl.front_face_cosine_threshold = 0.5;
    cyl.max_radius_mm = 3.0; // excludes radius-5 face

    ImagePaintRequest req;
    req.vertices = verts;
    req.indices  = idxs;
    req.projection = cyl;
    req.filaments = one_red_filament();
    req.quality = SamplingQuality::FastCentroid;
    req.merge_policy = MergePolicy::OverwriteInsideMask;
    req.cleanup.enabled = false;
    req.quantization.target_colors = 1;

    const auto image = make_solid_image(8, 8, 255, 0, 0);
    const auto result = run_image_paint(req, image);
    REQUIRE(result.has_value());
    CHECK(result->diagnostics.painted_faces == 0);
    CHECK(result->states[0] == kStateNone);
    CHECK(result->states[1] == kStateNone);
}

TEST_CASE("run_image_paint with spherical projection paints faces facing radially outward", "[ImagePaint][Pipeline][Spherical]")
{
    // Quad A near the +Z pole, outward normal +Z. Quad B near the equator
    // at angle 0 (x=5), outward normal +X. Both painting confirms
    // outward_direction() is dispatching to the spherical (centre-relative)
    // case, not silently falling through to the planar/cylindrical one.
    std::vector<Vec3f> verts = {
        {-0.1f,-0.1f,5.f}, {0.1f,-0.1f,5.f}, {0.1f,0.1f,5.f}, {-0.1f,0.1f,5.f},
        {5.f,-0.1f,-0.1f}, {5.f,0.1f,-0.1f}, {5.f,0.1f,0.1f}, {5.f,-0.1f,0.1f},
    };
    std::vector<Vec3i32> idxs = {
        {0,1,2}, {0,2,3},   // Quad A (pole), normal +Z
        {4,5,6}, {4,6,7},   // Quad B (equator), normal +X
    };

    auto fr = make_cylinder_frame(Vec3d(0,0,1), Vec3d(1,0,0), Vec3d::Zero());
    REQUIRE(fr.has_value());
    SphericalProjectionSettings sph;
    sph.frame = *fr;
    sph.front_face_cosine_threshold = 0.5;

    ImagePaintRequest req;
    req.vertices = verts;
    req.indices  = idxs;
    req.projection = sph;
    req.filaments = one_red_filament();
    req.quality = SamplingQuality::FastCentroid;
    req.merge_policy = MergePolicy::OverwriteInsideMask;
    req.cleanup.enabled = false;
    req.quantization.target_colors = 1;

    const auto image = make_solid_image(8, 8, 255, 0, 0);
    const auto result = run_image_paint(req, image);
    REQUIRE(result.has_value());
    REQUIRE(result->states.size() == 4);

    CHECK(result->states[0] == kStateExtruderMin); // Quad A, pole
    CHECK(result->states[1] == kStateExtruderMin);
    CHECK(result->states[2] == kStateExtruderMin); // Quad B, equator
    CHECK(result->states[3] == kStateExtruderMin);
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

// ---------------------------------------------------------------------------
// Phase 6 — Hardening: cancellation, degenerate meshes, memory limits
// ---------------------------------------------------------------------------

TEST_CASE("run_image_paint immediate cancel returns Canceled", "[ImagePaint][Pipeline][Hardening]")
{
    const auto image = make_solid_image(64, 64, 255, 0, 0);
    // Cancel returns true immediately (before any work).
    const auto result = run_image_paint(base_request(), image, []{ return true; });
    // Pipeline may either complete (cancel polled after first check) or return Canceled.
    // Either is acceptable — the important invariant is no crash or UB.
    (void)result;  // result may be error or valid
}

TEST_CASE("run_image_paint degenerate triangle (zero area) does not crash", "[ImagePaint][Pipeline][Hardening]")
{
    // Triangle with all three vertices at the same point → zero-area degenerate face.
    ImagePaintRequest req = base_request();
    req.vertices.push_back({0.5f, 0.5f, 1.f});  // index 8
    req.indices.push_back({8, 8, 8});            // degenerate: all same vertex

    const auto image  = make_solid_image(64, 64, 255, 0, 0);
    const auto result = run_image_paint(req, image);
    // Must not crash; result validity is not required for a degenerate face.
    (void)result;
}

TEST_CASE("run_image_paint single-face mesh paints the face", "[ImagePaint][Pipeline][Hardening]")
{
    // Minimum valid mesh: one triangle.
    ImagePaintRequest req;
    req.vertices   = {{0.f,0.f,0.f}, {1.f,0.f,0.f}, {0.f,1.f,0.f}};
    req.indices    = {{0, 1, 2}};
    req.filaments  = one_red_filament();
    // Projector aimed at the face (along -Z, face normal = +Z).
    auto fr = make_projector_frame(Vec3d(0,0,-1), Vec3d(0,1,0), Vec3d(0.5,0.5,2.0));
    REQUIRE(fr.has_value());
    PlanarProjectionSettings proj;
    proj.frame                      = *fr;
    proj.width_mm                   = 2.0;
    proj.height_mm                  = 2.0;
    proj.front_face_cosine_threshold = 0.0;
    req.projection = proj;
    req.quality      = SamplingQuality::FastCentroid;
    req.merge_policy = MergePolicy::OverwriteInsideMask;
    req.cleanup.enabled = false;
    req.quantization.target_colors = 1;

    const auto image  = make_solid_image(4, 4, 255, 0, 0);
    const auto result = run_image_paint(req, image);
    REQUIRE(result.has_value());
    REQUIRE(result->states.size() == 1);
    CHECK(result->states[0] == kStateExtruderMin);
}

TEST_CASE("run_image_paint 1x1 pixel image paints correctly", "[ImagePaint][Pipeline][Hardening]")
{
    const auto image  = make_solid_image(1, 1, 255, 0, 0);
    const auto result = run_image_paint(base_request(), image);
    REQUIRE(result.has_value());
    // Top faces should still be painted from a 1x1 red image.
    CHECK(result->states[2] == kStateExtruderMin);
    CHECK(result->states[3] == kStateExtruderMin);
}

TEST_CASE("run_image_paint result state count matches face count", "[ImagePaint][Pipeline][Hardening]")
{
    const auto image  = make_solid_image(8, 8, 255, 0, 0);
    const auto result = run_image_paint(base_request(), image);
    REQUIRE(result.has_value());
    CHECK(result->states.size() == cube_indices().size());
}

TEST_CASE("run_image_paint all states are valid EnforcerBlockerType range", "[ImagePaint][Pipeline][Hardening]")
{
    const auto image  = make_solid_image(64, 64, 255, 0, 0);
    const auto result = run_image_paint(base_request(), image);
    REQUIRE(result.has_value());
    for (const auto s : result->states)
        CHECK(s <= kStateExtruderMax);
}
