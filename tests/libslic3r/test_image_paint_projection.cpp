#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/ImagePaint/Projection.hpp"
#include "libslic3r/ImagePaint/FaceSampler.hpp"

#include <cmath>

using namespace Slic3r;
using namespace Slic3r::ImagePaint;
using namespace Catch::Matchers;

// ---------------------------------------------------------------------------
// ProjectorFrame construction
// ---------------------------------------------------------------------------

TEST_CASE("make_projector_frame produces valid orthonormal frame", "[ImagePaint][Projection]")
{
    auto result = make_projector_frame(
        Vec3d(0, 0, -1),   // look toward -Z
        Vec3d(0, 1,  0),   // up = +Y
        Vec3d(0, 0,  1));  // origin

    REQUIRE(result.has_value());
    const auto& f = result.value();

    // Axes must be unit length.
    CHECK_THAT(f.axis_u.norm(), WithinAbs(1.0, 1e-10));
    CHECK_THAT(f.axis_v.norm(), WithinAbs(1.0, 1e-10));
    CHECK_THAT(f.normal.norm(), WithinAbs(1.0, 1e-10));

    // Axes must be orthogonal.
    CHECK_THAT(f.axis_u.dot(f.axis_v), WithinAbs(0.0, 1e-10));
    CHECK_THAT(f.axis_u.dot(f.normal), WithinAbs(0.0, 1e-10));
    CHECK_THAT(f.axis_v.dot(f.normal), WithinAbs(0.0, 1e-10));
}

TEST_CASE("make_projector_frame rejects degenerate up/direction combination", "[ImagePaint][Projection]")
{
    // look and up are parallel — frame is degenerate
    auto result = make_projector_frame(
        Vec3d(0, 1, 0),
        Vec3d(0, 1, 0),
        Vec3d::Zero());

    REQUIRE(!result.has_value());
    CHECK(result.error().code == ImagePaintErrorCode::InvalidProjection);
}

// ---------------------------------------------------------------------------
// project_planar — known-point checks
// ---------------------------------------------------------------------------

static PlanarProjectionSettings default_proj()
{
    PlanarProjectionSettings s;
    s.frame.origin = Vec3d::Zero();
    s.frame.axis_u = Vec3d::UnitX();
    s.frame.axis_v = Vec3d::UnitY();
    s.frame.normal = Vec3d::UnitZ();
    s.width_mm  = 100.0;
    s.height_mm = 100.0;
    return s;
}

TEST_CASE("project_planar maps origin to image centre", "[ImagePaint][Projection]")
{
    auto s = default_proj();
    const auto pp = project_planar(Vec3d(0, 0, 1), s);

    CHECK_THAT(pp.u, WithinAbs(0.5, 1e-10));
    CHECK_THAT(pp.v, WithinAbs(0.5, 1e-10));
    CHECK(pp.inside);
    CHECK(pp.depth > 0.0);
}

TEST_CASE("project_planar maps top-left corner correctly", "[ImagePaint][Projection]")
{
    auto s = default_proj();
    // Point at (-50, 50, 0) in projector space: u=0, v=0 (top-left)
    const auto pp = project_planar(Vec3d(-50, 50, 1), s);

    CHECK_THAT(pp.u, WithinAbs(0.0, 1e-10));
    CHECK_THAT(pp.v, WithinAbs(0.0, 1e-10));
    CHECK(pp.inside);
}

TEST_CASE("project_planar maps bottom-right corner correctly", "[ImagePaint][Projection]")
{
    auto s = default_proj();
    // Point at (50, -50, 0): u=1, v=1 (bottom-right)
    const auto pp = project_planar(Vec3d(50, -50, 1), s);

    CHECK_THAT(pp.u, WithinAbs(1.0, 1e-10));
    CHECK_THAT(pp.v, WithinAbs(1.0, 1e-10));
    CHECK(pp.inside);
}

TEST_CASE("project_planar marks out-of-bounds point as not inside", "[ImagePaint][Projection]")
{
    auto s = default_proj();
    const auto pp = project_planar(Vec3d(200, 0, 1), s);

    CHECK(!pp.inside);
    CHECK(pp.u > 1.0);
}

// ---------------------------------------------------------------------------
// apply_rotation_mirror
// ---------------------------------------------------------------------------

TEST_CASE("apply_rotation_mirror identity leaves UV unchanged", "[ImagePaint][Projection]")
{
    auto [u, v] = apply_rotation_mirror(0.3, 0.7, 0.0, false, false);
    CHECK_THAT(u, WithinAbs(0.3, 1e-10));
    CHECK_THAT(v, WithinAbs(0.7, 1e-10));
}

TEST_CASE("apply_rotation_mirror 90 degrees rotates correctly", "[ImagePaint][Projection]")
{
    // Rotating (1,0) 90 degrees CCW around (0.5,0.5):
    // translate: (0.5, -0.5) -> rotate 90 CCW: (0.5, 0.5) -> translate back: (1.0, 1.0)
    // Actually: rotate (0.5, -0.5) by 90 deg: cos90=0, sin90=1
    //   u' = cos*u + sin*v = 0*0.5 + 1*(-0.5) = -0.5
    //   v' = -sin*u + cos*v = -1*0.5 + 0*(-0.5) = -0.5
    // translate back: (-0.5+0.5, -0.5+0.5) = (0, 0)
    constexpr double pi_2 = 1.5707963267948966;
    auto [u, v] = apply_rotation_mirror(1.0, 0.0, pi_2, false, false);
    CHECK_THAT(u, WithinAbs(0.0, 1e-10));
    CHECK_THAT(v, WithinAbs(0.0, 1e-10));
}

TEST_CASE("apply_rotation_mirror mirror_u flips horizontally", "[ImagePaint][Projection]")
{
    // (0.2, 0.5) mirror U around 0.5 -> (0.8, 0.5)
    auto [u, v] = apply_rotation_mirror(0.2, 0.5, 0.0, true, false);
    CHECK_THAT(u, WithinAbs(0.8, 1e-10));
    CHECK_THAT(v, WithinAbs(0.5, 1e-10));
}

TEST_CASE("apply_rotation_mirror mirror_v flips vertically", "[ImagePaint][Projection]")
{
    auto [u, v] = apply_rotation_mirror(0.5, 0.3, 0.0, false, true);
    CHECK_THAT(u, WithinAbs(0.5, 1e-10));
    CHECK_THAT(v, WithinAbs(0.7, 1e-10));
}

// ---------------------------------------------------------------------------
// sample_bilinear
// ---------------------------------------------------------------------------

static DecodedImage make_2x2_image()
{
    // 2x2 RGBA image:
    //  TL=(255,0,0,255)  TR=(0,255,0,255)
    //  BL=(0,0,255,255)  BR=(255,255,0,255)
    DecodedImage img;
    img.width  = 2;
    img.height = 2;
    img.has_alpha = false;
    img.rgba = {
        255, 0,   0,   255,   // TL red
        0,   255, 0,   255,   // TR green
        0,   0,   255, 255,   // BL blue
        255, 255, 0,   255,   // BR yellow
    };
    return img;
}

TEST_CASE("sample_bilinear top-left corner returns TL pixel", "[ImagePaint][Sampling]")
{
    auto img = make_2x2_image();
    // With the -0.5 offset convention, pixel (i,j) center is at UV = ((i+0.5)/w, (j+0.5)/h).
    // TL pixel (0,0) center in 2x2 image: u=0.25, v=0.25.
    const auto px = sample_bilinear(img, 0.25, 0.25);
    CHECK(px.r == 255);
    CHECK(px.g == 0);
    CHECK(px.b == 0);
    CHECK(px.a == 255);
}

TEST_CASE("sample_bilinear centre blends all four pixels", "[ImagePaint][Sampling]")
{
    auto img = make_2x2_image();
    const auto px = sample_bilinear(img, 0.5, 0.5);
    // At exactly centre all four pixels contribute equally
    // R: (255+0+0+255)/4 = 127 (allow ±2 for rounding)
    CHECK(px.r > 120);
    CHECK(px.r < 135);
}

TEST_CASE("sample_bilinear out-of-bounds returns transparent black", "[ImagePaint][Sampling]")
{
    auto img = make_2x2_image();
    const auto px = sample_bilinear(img, -0.1, 0.5);
    CHECK(px.a == 0);
}

// ---------------------------------------------------------------------------
// fit_planar_projection
// ---------------------------------------------------------------------------

TEST_CASE("fit_planar_projection covers unit cube from top", "[ImagePaint][Projection]")
{
    // Unit cube [0,1]^3 vertices.
    const std::vector<Vec3f> verts = {
        {0,0,0},{1,0,0},{1,1,0},{0,1,0},
        {0,0,1},{1,0,1},{1,1,1},{0,1,1},
    };
    auto fitted = fit_planar_projection(
        Span<const Vec3f>(verts.data(), verts.size()),
        Vec3d(0, 0, -1),  // look -Z (top-down)
        Vec3d(0, 1, 0),
        /*aspect=*/0.0,
        /*margin=*/1.0);
    REQUIRE(fitted.has_value());
    CHECK_THAT(fitted->width_mm,  WithinAbs(1.0, 1e-6));
    CHECK_THAT(fitted->height_mm, WithinAbs(1.0, 1e-6));

    // All 8 corners must project inside [0,1].
    for (const auto& v : verts) {
        const auto pp = project_planar(v.cast<double>(), *fitted);
        CHECK(pp.u >= -1e-9);
        CHECK(pp.u <= 1.0 + 1e-9);
        CHECK(pp.v >= -1e-9);
        CHECK(pp.v <= 1.0 + 1e-9);
        CHECK(pp.inside);
    }
}

TEST_CASE("fit_planar_projection preserves image aspect by expanding plane", "[ImagePaint][Projection]")
{
    // Thin strip 10 x 2 in XY, look -Z.
    const std::vector<Vec3f> verts = {
        {0,0,0},{10,0,0},{10,2,0},{0,2,0},
    };
    // Portrait image 1:2 → plane should expand height to 20 when width fixed at 10.
    auto fitted = fit_planar_projection(
        Span<const Vec3f>(verts.data(), verts.size()),
        Vec3d(0, 0, -1),
        Vec3d(0, 1, 0),
        /*aspect=*/0.5,   // w/h = 0.5 → height = width/0.5
        /*margin=*/1.0);
    REQUIRE(fitted.has_value());
    // Mesh extent_u=10, extent_v=2, mesh_aspect=5 > 0.5 → expand v: extent_v = 10/0.5 = 20
    CHECK_THAT(fitted->width_mm,  WithinAbs(10.0, 1e-6));
    CHECK_THAT(fitted->height_mm, WithinAbs(20.0, 1e-6));
    CHECK_THAT(fitted->width_mm / fitted->height_mm, WithinAbs(0.5, 1e-6));
}

TEST_CASE("fit_planar_projection empty mesh returns error", "[ImagePaint][Projection]")
{
    auto fitted = fit_planar_projection(
        Span<const Vec3f>{},
        Vec3d(0, 0, -1),
        Vec3d(0, 1, 0));
    REQUIRE(!fitted.has_value());
    CHECK(fitted.error().code == ImagePaintErrorCode::NoEligibleFaces);
}

// ---------------------------------------------------------------------------
// Gaussian7 weight invariant
// ---------------------------------------------------------------------------

TEST_CASE("GaussianSampler7 weights sum to 1.0", "[ImagePaint][Sampling]")
{
    float sum = 0.f;
    for (float w : GaussianSampler7::kWeight) sum += w;
    CHECK_THAT(sum, WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("GaussianSampler7 barycentric coords sum to 1.0 per point", "[ImagePaint][Sampling]")
{
    for (const auto& b : GaussianSampler7::kBary) {
        const float s = b[0] + b[1] + b[2];
        CHECK_THAT(s, WithinAbs(1.0f, 1e-5f));
    }
}

// ---------------------------------------------------------------------------
// CylinderFrame construction (Phase 7 — Roadmap.md §11)
// ---------------------------------------------------------------------------

TEST_CASE("make_cylinder_frame produces valid orthonormal frame", "[ImagePaint][Projection][Cylindrical]")
{
    auto result = make_cylinder_frame(Vec3d(0, 0, 1), Vec3d(1, 0, 0), Vec3d::Zero());
    REQUIRE(result.has_value());
    const auto& f = result.value();

    CHECK_THAT(f.axis.norm(),         WithinAbs(1.0, 1e-10));
    CHECK_THAT(f.radial_basis.norm(), WithinAbs(1.0, 1e-10));
    CHECK_THAT(f.tangent.norm(),      WithinAbs(1.0, 1e-10));

    CHECK_THAT(f.axis.dot(f.radial_basis), WithinAbs(0.0, 1e-10));
    CHECK_THAT(f.axis.dot(f.tangent),      WithinAbs(0.0, 1e-10));
    CHECK_THAT(f.radial_basis.dot(f.tangent), WithinAbs(0.0, 1e-10));
}

TEST_CASE("make_cylinder_frame rejects degenerate axis/radial_hint combination", "[ImagePaint][Projection][Cylindrical]")
{
    // radial_hint parallel to axis — no well-defined "angle 0" direction.
    auto result = make_cylinder_frame(Vec3d(0, 0, 1), Vec3d(0, 0, 5), Vec3d::Zero());
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ImagePaintErrorCode::InvalidProjection);
}

// ---------------------------------------------------------------------------
// project_cylindrical — known-point checks
// ---------------------------------------------------------------------------

static CylindricalProjectionSettings default_cyl_proj()
{
    auto fr = make_cylinder_frame(Vec3d(0, 0, 1), Vec3d(1, 0, 0), Vec3d::Zero());
    REQUIRE(fr.has_value());

    CylindricalProjectionSettings s;
    s.frame = *fr;
    s.seam_angle_radians = 0.0;
    s.wrap_angle_radians = 2.0 * PI;
    s.height_mm = 100.0;
    return s;
}

TEST_CASE("project_cylindrical maps the seam-angle direction to u=0.5", "[ImagePaint][Projection][Cylindrical]")
{
    auto s = default_cyl_proj();
    const auto pp = project_cylindrical(Vec3d(10, 0, 0), s);

    CHECK_THAT(pp.u, WithinAbs(0.5, 1e-10));
    CHECK_THAT(pp.v, WithinAbs(0.5, 1e-10));
    CHECK(pp.inside);
}

TEST_CASE("project_cylindrical maps a quarter turn to u=0.75", "[ImagePaint][Projection][Cylindrical]")
{
    auto s = default_cyl_proj();
    // (0, R, 0) is +90 degrees from the seam-angle direction (1,0,0) around +Z.
    const auto pp = project_cylindrical(Vec3d(0, 10, 0), s);

    CHECK_THAT(pp.u, WithinAbs(0.75, 1e-10));
    CHECK(pp.inside);
}

TEST_CASE("project_cylindrical maps the opposite direction to the seam (u=1.0)", "[ImagePaint][Projection][Cylindrical]")
{
    auto s = default_cyl_proj();
    const auto pp = project_cylindrical(Vec3d(-10, 0, 0), s);

    CHECK_THAT(pp.u, WithinAbs(1.0, 1e-10));
}

TEST_CASE("project_cylindrical maps height to v centred at the frame origin", "[ImagePaint][Projection][Cylindrical]")
{
    auto s = default_cyl_proj();

    const auto top    = project_cylindrical(Vec3d(10, 0, 50), s);
    const auto bottom = project_cylindrical(Vec3d(10, 0, -50), s);
    const auto mid     = project_cylindrical(Vec3d(10, 0, 0), s);

    CHECK_THAT(top.v,    WithinAbs(0.0, 1e-10));
    CHECK_THAT(bottom.v, WithinAbs(1.0, 1e-10));
    CHECK_THAT(mid.v,    WithinAbs(0.5, 1e-10));
}

TEST_CASE("project_cylindrical excludes points outside the radius range", "[ImagePaint][Projection][Cylindrical]")
{
    auto s = default_cyl_proj();
    s.min_radius_mm = 8.0;
    s.max_radius_mm = 12.0;

    const auto outer_ok  = project_cylindrical(Vec3d(10, 0, 0), s);
    const auto too_small = project_cylindrical(Vec3d(2, 0, 0), s);  // radius 2 < min 8
    const auto too_large = project_cylindrical(Vec3d(20, 0, 0), s); // radius 20 > max 12

    CHECK(outer_ok.inside);
    CHECK(!too_small.inside);
    CHECK(!too_large.inside);
}

TEST_CASE("project_cylindrical partial wrap excludes points beyond the wrap angle", "[ImagePaint][Projection][Cylindrical]")
{
    auto s = default_cyl_proj();
    s.wrap_angle_radians = PI; // paint only a 180-degree slice centred on the seam angle

    const auto front = project_cylindrical(Vec3d(10, 0, 0), s);   // 0 degrees — inside the slice
    const auto side   = project_cylindrical(Vec3d(0, 10, 0), s);   // 90 degrees — at the edge
    const auto back    = project_cylindrical(Vec3d(-10, 0, 0), s); // 180 degrees — outside a 180-degree wrap

    CHECK(front.inside);
    CHECK(side.inside);
    CHECK(!back.inside);
}

TEST_CASE("project_cylindrical is continuous across the seam for a full wrap", "[ImagePaint][Projection][Cylindrical]")
{
    auto s = default_cyl_proj();
    // Two points just on either side of the seam (opposite the seam angle)
    // should map to u values close to 0 and 1 respectively, not jump elsewhere.
    const double eps = 1e-4;
    const auto just_before = project_cylindrical(
        Vec3d(10 * std::cos(PI - eps), 10 * std::sin(PI - eps), 0), s);
    const auto just_after = project_cylindrical(
        Vec3d(10 * std::cos(-PI + eps), 10 * std::sin(-PI + eps), 0), s);

    CHECK(just_before.u > 0.99);
    CHECK(just_after.u  < 0.01);
}
