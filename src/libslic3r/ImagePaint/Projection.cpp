#include "Projection.hpp"

#include <cmath>
#include <cassert>
#include <limits>

namespace Slic3r::ImagePaint {

Expected<ProjectorFrame, ImagePaintError>
make_projector_frame(const Vec3d& look_direction,
                     const Vec3d& up_hint,
                     const Vec3d& origin)
{
    constexpr double kEps = 1e-8;

    const Vec3d fwd = look_direction.normalized();
    if (fwd.norm() < kEps)
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::InvalidProjection,
            "Projection direction is degenerate (zero-length)."});

    // axis_u = cross(up_hint, fwd) — points image-right in a right-handed frame.
    Vec3d right = up_hint.cross(fwd);
    if (right.norm() < kEps)
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::InvalidProjection,
            "Look direction and up hint are parallel — cannot build projector frame."});

    right.normalize();

    // axis_v = cross(fwd, right) — points image-up (v increases downward in UV).
    Vec3d up = fwd.cross(right);
    up.normalize();

    ProjectorFrame frame;
    frame.origin = origin;
    frame.axis_u = right;
    frame.axis_v = up;
    frame.normal = fwd;
    return frame;
}

Expected<PlanarProjectionSettings, ImagePaintError>
fit_planar_projection(Span<const Vec3f> vertices,
                      const Vec3d&      look_direction,
                      const Vec3d&      up_hint,
                      double            image_aspect_w_over_h,
                      double            margin)
{
    if (vertices.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::NoEligibleFaces,
            "Cannot fit projection: mesh has no vertices."});

    if (!(margin > 0.0) || !std::isfinite(margin))
        margin = 1.02;

    // Build orthonormal axes (origin filled in after extent scan).
    auto axes = make_projector_frame(look_direction, up_hint, Vec3d::Zero());
    if (!axes)
        return make_unexpected(axes.error());

    const Vec3d& axis_u = axes->axis_u;
    const Vec3d& axis_v = axes->axis_v;
    const Vec3d& normal = axes->normal;

    double min_u =  std::numeric_limits<double>::infinity();
    double max_u = -std::numeric_limits<double>::infinity();
    double min_v =  std::numeric_limits<double>::infinity();
    double max_v = -std::numeric_limits<double>::infinity();
    double min_n =  std::numeric_limits<double>::infinity();
    double max_n = -std::numeric_limits<double>::infinity();

    for (const Vec3f& vf : vertices) {
        const Vec3d p = vf.cast<double>();
        const double u = p.dot(axis_u);
        const double v = p.dot(axis_v);
        const double n = p.dot(normal);
        min_u = std::min(min_u, u); max_u = std::max(max_u, u);
        min_v = std::min(min_v, v); max_v = std::max(max_v, v);
        min_n = std::min(min_n, n); max_n = std::max(max_n, n);
    }

    double extent_u = std::max(max_u - min_u, 1e-6);
    double extent_v = std::max(max_v - min_v, 1e-6);

    // Preserve source image aspect by expanding the smaller plane dimension so
    // the mesh is still fully covered (letterbox the mesh into the image plane).
    if (image_aspect_w_over_h > 1e-9 && std::isfinite(image_aspect_w_over_h)) {
        const double mesh_aspect = extent_u / extent_v;
        if (image_aspect_w_over_h > mesh_aspect)
            extent_u = extent_v * image_aspect_w_over_h;
        else
            extent_v = extent_u / image_aspect_w_over_h;
    }

    extent_u *= margin;
    extent_v *= margin;

    const double mid_u = 0.5 * (min_u + max_u);
    const double mid_v = 0.5 * (min_v + max_v);
    // Place the projector plane on the camera side of the mesh (smaller depth
    // along look/normal), slightly in front so depth is positive into the mesh.
    const double plane_n = min_n - 1.0;

    PlanarProjectionSettings s;
    s.frame.origin = axis_u * mid_u + axis_v * mid_v + normal * plane_n;
    s.frame.axis_u = axis_u;
    s.frame.axis_v = axis_v;
    s.frame.normal = normal;
    s.width_mm  = extent_u;
    s.height_mm = extent_v;
    return s;
}

std::pair<double,double> apply_rotation_mirror(double u, double v,
                                                double rotation_radians,
                                                bool   mirror_u,
                                                bool   mirror_v)
{
    // Rotate/mirror around image centre (0.5, 0.5).
    u -= 0.5;
    v -= 0.5;

    if (rotation_radians != 0.0) {
        const double cos_r = std::cos(rotation_radians);
        const double sin_r = std::sin(rotation_radians);
        const double u2 =  cos_r * u + sin_r * v;
        const double v2 = -sin_r * u + cos_r * v;
        u = u2;
        v = v2;
    }

    if (mirror_u) u = -u;
    if (mirror_v) v = -v;

    u += 0.5;
    v += 0.5;

    return {u, v};
}

ProjectedPoint project_planar(const Vec3d&                   p,
                               const PlanarProjectionSettings& s)
{
    const Vec3d d = p - s.frame.origin;

    const double x     = d.dot(s.frame.axis_u);
    const double y     = d.dot(s.frame.axis_v);
    const double depth = d.dot(s.frame.normal);

    // Normalize to [0,1] with (0,0) at top-left.
    // x maps to u: left = 0, right = 1.
    // y maps to v: top  = 0 (v decreases as y increases since axis_v points up).
    double u =  x / s.width_mm  + 0.5;
    double v = -y / s.height_mm + 0.5;

    auto [ru, rv] = apply_rotation_mirror(u, v,
                                          s.rotation_radians,
                                          s.mirror_u,
                                          s.mirror_v);

    const bool inside = (ru >= 0.0 && ru <= 1.0 &&
                          rv >= 0.0 && rv <= 1.0);

    return ProjectedPoint{ru, rv, depth, inside};
}

} // namespace Slic3r::ImagePaint
