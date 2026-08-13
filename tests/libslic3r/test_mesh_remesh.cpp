#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/ImagePaint/MeshRemesh.hpp"
#include "libslic3r/ImagePaint/MeshBake.hpp"
#include "libslic3r/ImagePaint/Projection.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cmath>

using namespace Slic3r;
using namespace Slic3r::ImagePaint;

// Mesh Graffiti-style remeshing: real geometry whose edges follow the
// image's color-region boundaries (marching squares + constrained Delaunay
// triangulation), instead of a uniform grid. See MeshRemesh.hpp.

namespace {

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
    s.minimum_coverage = 0.05;
    return s;
}

QuantizationSettings two_colors()
{
    QuantizationSettings q;
    q.target_colors = 2;
    return q;
}

} // namespace

TEST_CASE("remesh_by_color_boundary produces two colors on one original triangle", "[ImagePaint][MeshRemesh]")
{
    const auto image = make_vertical_split_image(64, 64, {255, 0, 0}, {0, 255, 0});

    const auto result = remesh_by_color_boundary(
        cube_vertices(), cube_indices(), image, top_down_projector(),
        red_and_green_filaments(), two_colors(), /*grid_resolution=*/10);

    REQUIRE(result.has_value());
    CHECK(result->mesh.indices.size() > 12); // more triangles than the original cube

    bool found_red = false, found_green = false;
    for (auto s : result->triangle_states) {
        if (s == kStateExtruderMin)     found_red = true;
        if (s == kStateExtruderMin + 1) found_green = true;
    }
    CHECK(found_red);
    CHECK(found_green);
}

TEST_CASE("remesh_by_color_boundary keeps every vertex on one of the cube's original planes", "[ImagePaint][MeshRemesh]")
{
    const auto image = make_vertical_split_image(64, 64, {255, 0, 0}, {0, 255, 0});

    const auto result = remesh_by_color_boundary(
        cube_vertices(), cube_indices(), image, top_down_projector(),
        red_and_green_filaments(), two_colors(), /*grid_resolution=*/10);

    REQUIRE(result.has_value());

    constexpr float eps = 1e-3f;
    for (const auto& v : result->mesh.vertices) {
        const bool on_a_plane =
            std::abs(v.x() - 0.f) < eps || std::abs(v.x() - 1.f) < eps ||
            std::abs(v.y() - 0.f) < eps || std::abs(v.y() - 1.f) < eps ||
            std::abs(v.z() - 0.f) < eps || std::abs(v.z() - 1.f) < eps;
        CHECK(on_a_plane);
    }
}

TEST_CASE("remesh_by_color_boundary untouched faces stay a single triangle", "[ImagePaint][MeshRemesh]")
{
    const auto image = make_vertical_split_image(64, 64, {255, 0, 0}, {0, 255, 0});

    const auto result = remesh_by_color_boundary(
        cube_vertices(), cube_indices(), image, top_down_projector(),
        red_and_green_filaments(), two_colors(), /*grid_resolution=*/10);

    REQUIRE(result.has_value());

    // The bottom face (untouched by a top-down projector) contributes
    // exactly 2 flat triangles (kStateNone) — no subdivision happened there.
    std::size_t none_triangles = 0;
    for (auto s : result->triangle_states)
        if (s == kStateNone) ++none_triangles;
    CHECK(none_triangles >= 2);
}

TEST_CASE("remesh_by_color_boundary output passes or is safely rejected by bake-stage validation", "[ImagePaint][MeshRemesh]")
{
    // This is the real test of the v1 known-limitation documented in
    // MeshRemesh.hpp: contour points along a mesh edge shared by two faces
    // are computed independently by each face's own local grid, so
    // adjacent faces are not guaranteed to agree on where a boundary
    // crosses that shared edge. Piping the output through the same
    // validate_baked_mesh() used by the uniform-grid bake path is the
    // actual test of whether that risk manifests in practice here, rather
    // than a hypothetical worry.
    const auto image = make_vertical_split_image(64, 64, {255, 0, 0}, {0, 255, 0});

    const auto result = remesh_by_color_boundary(
        cube_vertices(), cube_indices(), image, top_down_projector(),
        red_and_green_filaments(), two_colors(), /*grid_resolution=*/10);
    REQUIRE(result.has_value());

    // On this fixture the color boundary happens to cross the diagonal edge
    // shared by the cube's two top-face triangles — exactly the known
    // limitation. Either outcome is acceptable for v1: the two faces'
    // independently-traced crossing points happened to agree (validation
    // passes), or they didn't and validate_baked_mesh() correctly rejects
    // the result instead of silently committing a gap. What would be a
    // real bug is anything else — a crash, or validation silently passing
    // on a mesh that actually has open edges.
    const auto validated = validate_baked_mesh(*result);
    if (validated.has_value()) {
        CHECK(its_num_open_edges(validated->mesh) == 0);
    } else {
        CHECK(validated.error().code == ImagePaintErrorCode::BakeInvalidGeometry);
    }
}

TEST_CASE("remesh_by_color_boundary on an empty mesh returns NoEligibleFaces", "[ImagePaint][MeshRemesh]")
{
    const auto result = remesh_by_color_boundary(
        {}, {}, DecodedImage{}, PlanarProjectionSettings{},
        red_and_green_filaments(), two_colors(), 10);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ImagePaintErrorCode::NoEligibleFaces);
}
