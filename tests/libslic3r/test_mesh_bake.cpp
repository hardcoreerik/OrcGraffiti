#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/ImagePaint/MeshBake.hpp"
#include "libslic3r/ImagePaint/ImagePaintPipeline.hpp"
#include "libslic3r/ImagePaint/Projection.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cmath>

using namespace Slic3r;
using namespace Slic3r::ImagePaint;

// Bake stage, Option 1 (docs/OrcGraffiti/MeshGraffiti_Bake_Plan.md section 4/12
// stage 1): materialize FacePaintPlan's already-computed detail leaves as
// real geometry. These tests prove the candidate-mesh step in isolation —
// no commit-to-live-model path is exercised here yet.

namespace {

// Same unit-cube fixture as test_image_paint_pipeline.cpp.
std::vector<Vec3f> cube_vertices()
{
    return {
        {0.f,0.f,0.f}, {1.f,0.f,0.f}, {1.f,1.f,0.f}, {0.f,1.f,0.f},
        {0.f,0.f,1.f}, {1.f,0.f,1.f}, {1.f,1.f,1.f}, {0.f,1.f,1.f},
    };
}

std::vector<Vec3i32> cube_indices()
{
    return {
        {0,3,2}, {0,2,1},   // bottom
        {4,5,6}, {4,6,7},   // top   (faces 2,3 — painted by the top-down projector below)
        {0,1,5}, {0,5,4},   // front
        {2,3,7}, {2,7,6},   // back
        {0,4,7}, {0,7,3},   // left
        {1,2,6}, {1,6,5},   // right
    };
}

DecodedImage make_vertical_split_image(int w, int h, ColorRgb8 left, ColorRgb8 right)
{
    DecodedImage img;
    img.width  = w;
    img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const ColorRgb8& c = (x < w / 2) ? left : right;
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            img.rgba[i+0] = c.r; img.rgba[i+1] = c.g; img.rgba[i+2] = c.b; img.rgba[i+3] = 255;
        }
    }
    return img;
}

std::vector<FilamentColor> red_and_green_filaments()
{
    FilamentColor red;
    red.project_index = 0;
    red.name          = "Red";
    red.display_rgb   = {255, 0, 0};
    FilamentColor green;
    green.project_index = 1;
    green.name          = "Green";
    green.display_rgb   = {0, 255, 0};
    return {red, green};
}

PlanarProjectionSettings top_down_projector()
{
    PlanarProjectionSettings s;
    auto fr = make_projector_frame(Vec3d(0,0,-1), Vec3d(0,1,0), Vec3d(0.5,0.5,2.0));
    REQUIRE(fr.has_value());
    s.frame = *fr;
    s.width_mm = 2.0;
    s.height_mm = 2.0;
    s.front_face_cosine_threshold = 0.5;
    return s;
}

} // namespace

TEST_CASE("bake_candidate_mesh with no detail leaves reproduces original topology", "[ImagePaint][MeshBake]")
{
    const auto vertices = cube_vertices();
    const auto indices  = cube_indices();

    std::vector<SelectorState> flat_states(12, kStateNone);
    flat_states[2] = kStateExtruderMin;

    const auto result = bake_candidate_mesh(vertices, indices, flat_states, {}, /*detail_edge_length_mm=*/0.0);
    REQUIRE(result.has_value());

    CHECK(result->mesh.indices.size() == 12);
    CHECK(result->triangle_states.size() == 12);

    std::size_t painted = 0;
    for (auto s : result->triangle_states)
        if (s == kStateExtruderMin) ++painted;
    CHECK(painted == 1);
}

TEST_CASE("bake_candidate_mesh materializes detail leaves as real, manifold geometry", "[ImagePaint][MeshBake]")
{
    const auto image = make_vertical_split_image(64, 64, {255, 0, 0}, {0, 255, 0});

    ImagePaintRequest req;
    req.vertices = cube_vertices();
    req.indices  = cube_indices();
    req.projection = top_down_projector();
    req.filaments = red_and_green_filaments();
    req.quality = SamplingQuality::FastCentroid;
    req.merge_policy = MergePolicy::OverwriteInsideMask;
    req.cleanup.enabled = false;
    req.quantization.target_colors = 2;
    req.detail_edge_length_mm = 0.1;

    const auto plan = run_image_paint(req, image);
    REQUIRE(plan.has_value());
    REQUIRE_FALSE(plan->detail_leaf_states.empty());

    const auto baked = bake_candidate_mesh(req.vertices, req.indices, plan->states,
                                           plan->detail_leaf_states, req.detail_edge_length_mm);
    REQUIRE(baked.has_value());

    // More real triangles than the original 12 — this is the whole point.
    CHECK(baked->mesh.indices.size() > 12);
    REQUIRE(baked->mesh.indices.size() == baked->triangle_states.size());

    // Both colors actually made it into real geometry, not just the virtual plan.
    bool found_red = false, found_green = false;
    for (auto s : baked->triangle_states) {
        if (s == kStateExtruderMin)     found_red = true;
        if (s == kStateExtruderMin + 1) found_green = true;
    }
    CHECK(found_red);
    CHECK(found_green);

    // Untouched faces (everything except the top) are still closed — the cube
    // as a whole stays manifold; only the painted top region's triangle
    // pattern changed density.
    CHECK(its_num_open_edges(baked->mesh) == 0);
}

TEST_CASE("bake_candidate_mesh keeps every vertex on one of the cube's six original planes", "[ImagePaint][MeshBake]")
{
    const auto image = make_vertical_split_image(64, 64, {255, 0, 0}, {0, 255, 0});

    ImagePaintRequest req;
    req.vertices = cube_vertices();
    req.indices  = cube_indices();
    req.projection = top_down_projector();
    req.filaments = red_and_green_filaments();
    req.quality = SamplingQuality::FastCentroid;
    req.merge_policy = MergePolicy::OverwriteInsideMask;
    req.cleanup.enabled = false;
    req.quantization.target_colors = 2;
    req.detail_edge_length_mm = 0.1;

    const auto plan = run_image_paint(req, image);
    REQUIRE(plan.has_value());

    const auto baked = bake_candidate_mesh(req.vertices, req.indices, plan->states,
                                           plan->detail_leaf_states, req.detail_edge_length_mm);
    REQUIRE(baked.has_value());

    // The unit cube's faces sit at x=0, x=1, y=0, y=1, z=0, z=1. Any new
    // vertex introduced by subdivision (necessarily barycentric within the
    // original face it split) must still land exactly on one of these six
    // planes — this is the "stays on the original surface" guarantee, since
    // subdivide_facet_uniform bisects edges of a single planar triangle.
    constexpr float eps = 1e-4f;
    for (const auto& v : baked->mesh.vertices) {
        const bool on_a_plane =
            std::abs(v.x() - 0.f) < eps || std::abs(v.x() - 1.f) < eps ||
            std::abs(v.y() - 0.f) < eps || std::abs(v.y() - 1.f) < eps ||
            std::abs(v.z() - 0.f) < eps || std::abs(v.z() - 1.f) < eps;
        CHECK(on_a_plane);
    }
}

TEST_CASE("bake_candidate_mesh on an empty mesh returns NoEligibleFaces", "[ImagePaint][MeshBake]")
{
    const auto result = bake_candidate_mesh({}, {}, {}, {}, 0.5);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ImagePaintErrorCode::NoEligibleFaces);
}

// ---------------------------------------------------------------------------
// validate_baked_mesh — bake stage 2 (plan section 8, steps 1-3)
// ---------------------------------------------------------------------------

TEST_CASE("validate_baked_mesh passes through an already-valid mesh unchanged", "[ImagePaint][MeshBake]")
{
    BakedMesh baked;
    baked.mesh.vertices = cube_vertices();
    baked.mesh.indices  = cube_indices();
    baked.triangle_states.assign(12, kStateNone);
    baked.triangle_states[2] = kStateExtruderMin;

    const auto result = validate_baked_mesh(baked);
    REQUIRE(result.has_value());
    CHECK(result->mesh.indices.size() == 12);
    CHECK(result->triangle_states[2] == kStateExtruderMin);
}

TEST_CASE("validate_baked_mesh drops degenerate triangles and keeps states in sync", "[ImagePaint][MeshBake]")
{
    BakedMesh baked;
    baked.mesh.vertices = cube_vertices();
    baked.mesh.indices  = cube_indices();
    baked.triangle_states.assign(12, kStateNone);
    baked.triangle_states[2] = kStateExtruderMin;

    // Append one zero-area (degenerate) triangle with a distinctive state —
    // if it survives cleanup, or its removal desyncs the parallel array,
    // this test catches it.
    baked.mesh.indices.push_back({0, 0, 1});
    baked.triangle_states.push_back(kStateExtruderMin + 5);

    const auto result = validate_baked_mesh(baked);
    REQUIRE(result.has_value());
    CHECK(result->mesh.indices.size() == 12);
    REQUIRE(result->mesh.indices.size() == result->triangle_states.size());

    bool found_bogus_state = false;
    for (auto s : result->triangle_states)
        if (s == kStateExtruderMin + 5) found_bogus_state = true;
    CHECK_FALSE(found_bogus_state);
    CHECK(result->triangle_states[2] == kStateExtruderMin);
}

TEST_CASE("validate_baked_mesh rejects non-manifold geometry", "[ImagePaint][MeshBake]")
{
    BakedMesh baked;
    baked.mesh.vertices = cube_vertices();
    baked.mesh.indices  = cube_indices();
    baked.mesh.indices.pop_back(); // drop one triangle of the "right" face -> open edges
    baked.triangle_states.assign(baked.mesh.indices.size(), kStateNone);

    const auto result = validate_baked_mesh(baked);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ImagePaintErrorCode::BakeInvalidGeometry);
}
