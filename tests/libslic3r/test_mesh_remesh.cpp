#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/ImagePaint/MeshRemesh.hpp"
#include "libslic3r/ImagePaint/MeshBake.hpp"
#include "libslic3r/ImagePaint/ImageDecoder.hpp"
#include "libslic3r/ImagePaint/Projection.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include "test_utils.hpp"

#include <cmath>
#include <filesystem>

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

TEST_CASE("remesh_by_color_boundary shared-edge color crossings stay manifold", "[ImagePaint][MeshRemesh]")
{
    // The vertical split crosses the cube top face's shared diagonal. Both
    // top triangles must reuse the same cached crossing vertex rather than
    // independently interpolating it from their local grids — otherwise
    // validate_baked_mesh() rejects the result as non-manifold and Apply
    // fails. This is the defining property of the edge-crossing cache.
    const auto image = make_vertical_split_image(64, 64, {255, 0, 0}, {0, 255, 0});

    const auto result = remesh_by_color_boundary(
        cube_vertices(), cube_indices(), image, top_down_projector(),
        red_and_green_filaments(), two_colors(), /*grid_resolution=*/10);
    REQUIRE(result.has_value());

    const auto validated = validate_baked_mesh(*result);
    REQUIRE(validated.has_value());
    CHECK(its_num_open_edges(validated->mesh) == 0);

    bool found_red = false, found_green = false;
    for (auto s : validated->triangle_states) {
        if (s == kStateExtruderMin)     found_red = true;
        if (s == kStateExtruderMin + 1) found_green = true;
    }
    CHECK(found_red);
    CHECK(found_green);

    // The color boundary crosses the top diagonal (0,0,1)-(1,1,1) at its
    // midpoint. That Steiner vertex must exist and be referenced — two
    // faces sharing an index, not two nearby-but-distinct positions.
    constexpr float eps = 2e-2f;
    int midpoint_index = -1;
    for (int i = 0; i < static_cast<int>(validated->mesh.vertices.size()); ++i) {
        const auto& v = validated->mesh.vertices[static_cast<std::size_t>(i)];
        if (std::abs(v.x() - 0.5f) < eps &&
            std::abs(v.y() - 0.5f) < eps &&
            std::abs(v.z() - 1.0f) < eps) {
            midpoint_index = i;
            break;
        }
    }
    REQUIRE(midpoint_index >= 0);

    int uses = 0;
    for (const auto& tri : validated->mesh.indices)
        if (tri[0] == midpoint_index || tri[1] == midpoint_index || tri[2] == midpoint_index)
            ++uses;
    CHECK(uses >= 2);
}

TEST_CASE("remesh_by_color_boundary leaves the unpainted bottom unsplit", "[ImagePaint][MeshRemesh]")
{
    const auto image = make_vertical_split_image(64, 64, {255, 0, 0}, {0, 255, 0});

    const auto result = remesh_by_color_boundary(
        cube_vertices(), cube_indices(), image, top_down_projector(),
        red_and_green_filaments(), two_colors(), /*grid_resolution=*/10);
    REQUIRE(result.has_value());

    std::size_t bottom_triangles = 0;
    for (const auto& tri : result->mesh.indices) {
        const auto& a = result->mesh.vertices[tri[0]];
        const auto& b = result->mesh.vertices[tri[1]];
        const auto& c = result->mesh.vertices[tri[2]];
        if (std::abs(a.z()) < 1e-3f && std::abs(b.z()) < 1e-3f && std::abs(c.z()) < 1e-3f)
            ++bottom_triangles;
    }
    CHECK(bottom_triangles == 2);
}

TEST_CASE("remesh_by_color_boundary shared-edge cache is deterministic", "[ImagePaint][MeshRemesh]")
{
    const auto image = make_vertical_split_image(64, 64, {255, 0, 0}, {0, 255, 0});

    std::size_t verts = 0, faces = 0;
    for (int i = 0; i < 8; ++i) {
        const auto result = remesh_by_color_boundary(
            cube_vertices(), cube_indices(), image, top_down_projector(),
            red_and_green_filaments(), two_colors(), /*grid_resolution=*/10);
        REQUIRE(result.has_value());
        const auto validated = validate_baked_mesh(*result);
        REQUIRE(validated.has_value());
        CHECK(its_num_open_edges(validated->mesh) == 0);
        if (i == 0) {
            verts = validated->mesh.vertices.size();
            faces = validated->mesh.indices.size();
        } else {
            CHECK(validated->mesh.vertices.size() == verts);
            CHECK(validated->mesh.indices.size() == faces);
        }
    }
}

TEST_CASE("remesh_by_color_boundary stays manifold on a dense grid whose color boundary crosses many shared edges", "[ImagePaint][MeshRemesh]")
{
    // 8x8 quads (128 triangles) in the z=1 plane. The vertical split crosses
    // every interior edge that straddles x=0.5 — the case that used to
    // fail validate_baked_mesh() on anything denser than a lucky cube.
    constexpr int n = 8;
    std::vector<Vec3f> vertices;
    std::vector<Vec3i32> indices;
    vertices.reserve(static_cast<std::size_t>(n + 1) * (n + 1));
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i)
            vertices.push_back({static_cast<float>(i) / n, static_cast<float>(j) / n, 1.f});
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const int a = j * (n + 1) + i;
            indices.push_back({a, a + 1, a + n + 2});
            indices.push_back({a, a + n + 2, a + n + 1});
        }
    }

    const auto image = make_vertical_split_image(64, 64, {255, 0, 0}, {0, 255, 0});
    const auto result = remesh_by_color_boundary(
        vertices, indices, image, top_down_projector(),
        red_and_green_filaments(), two_colors(), /*grid_resolution=*/8);
    REQUIRE(result.has_value());

    // This is an open plane (32 boundary edges). The vertical split adds
    // one Steiner on the top and bottom boundaries, so 34 open edges is
    // the correct, T-junction-free count — not zero. validate_baked_mesh()
    // requires a closed solid and is the wrong checker here.
    CHECK(its_num_open_edges(result->mesh) == 34);
    CHECK(result->mesh.indices.size() > indices.size());
    const TriangleMesh check_mesh(result->mesh);
    CHECK_FALSE(MeshBoolean::cgal::does_self_intersect(check_mesh));

    bool found_red = false, found_green = false;
    for (auto s : result->triangle_states) {
        if (s == kStateExtruderMin)     found_red = true;
        if (s == kStateExtruderMin + 1) found_green = true;
    }
    CHECK(found_red);
    CHECK(found_green);
}

TEST_CASE("remesh_by_color_boundary on a real cube obj plus photo is manifold when the photo is present", "[ImagePaint][MeshRemesh]")
{
    // Not a substitute for a human GUI check — this runs the same remesh
    // Apply uses, on a loaded .obj and a real JPEG if one is sitting in
    // the build tree. Skips cleanly when the photo is not there.
    const std::filesystem::path photo{"F:/Ai/OrcGraffiti/build/garth.jpg"};
    if (!std::filesystem::exists(photo))
        SKIP("garth.jpg not in the build tree");

    const TriangleMesh mesh = load_model("20mm_cube.obj");
    REQUIRE_FALSE(mesh.its.indices.empty());

    auto image = decode_image(photo);
    REQUIRE(image.has_value());

    const auto aspect = static_cast<double>(image->width) / std::max(1, image->height);
    std::vector<Vec3f> vertices = mesh.its.vertices;
    std::vector<Vec3i32> indices;
    indices.reserve(mesh.its.indices.size());
    for (const auto& t : mesh.its.indices)
        indices.push_back(t.cast<int32_t>());

    auto fitted = fit_planar_projection(
        Span<const Vec3f>(vertices.data(), vertices.size()),
        Vec3d(0, 1, 0), Vec3d(0, 0, 1), aspect, 1.02);
    REQUIRE(fitted.has_value());
    fitted->front_face_cosine_threshold = 0.05;
    fitted->minimum_coverage = 0.05;
    fitted->width_mm  *= 0.30;
    fitted->height_mm *= 0.30;

    const auto result = remesh_by_color_boundary(
        vertices, indices, *image, *fitted,
        red_and_green_filaments(), two_colors(), /*grid_resolution=*/8);
    REQUIRE(result.has_value());

    const auto validated = validate_baked_mesh(*result);
    REQUIRE(validated.has_value());
    CHECK(its_num_open_edges(validated->mesh) == 0);
}

TEST_CASE("remesh_by_color_boundary on an empty mesh returns NoEligibleFaces", "[ImagePaint][MeshRemesh]")
{
    const auto result = remesh_by_color_boundary(
        {}, {}, DecodedImage{}, PlanarProjectionSettings{},
        red_and_green_filaments(), two_colors(), 10);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ImagePaintErrorCode::NoEligibleFaces);
}
