#include "Projection.hpp"

#include <cmath>
#include <cassert>

namespace Slic3r::ImagePaint {

std::expected<ProjectorFrame, ImagePaintError>
make_projector_frame(const Vec3d& look_direction,
                     const Vec3d& up_hint,
                     const Vec3d& origin)
{
    constexpr double kEps = 1e-8;

    const Vec3d fwd = look_direction.normalized();
    if (fwd.norm() < kEps)
        return std::unexpected(ImagePaintError{
            ImagePaintErrorCode::InvalidProjection,
            "Projection direction is degenerate (zero-length)."});

    // axis_u = cross(up_hint, fwd) — points image-right in a right-handed frame.
    Vec3d right = up_hint.cross(fwd);
    if (right.norm() < kEps)
        return std::unexpected(ImagePaintError{
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
