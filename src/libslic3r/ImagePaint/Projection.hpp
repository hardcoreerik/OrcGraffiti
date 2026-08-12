#pragma once

#include "ImagePaintTypes.hpp"
#include "ImagePaintErrors.hpp"

#include "libslic3r/Point.hpp"  // Vec3d, Transform3d
#include "libslic3r/libslic3r.h"  // Slic3r::PI

#include "ImagePaintCompat.hpp"
#include <optional>
#include <limits>

namespace Slic3r::ImagePaint {

// A right-handed orthonormal projector frame in world/object space.
// axis_u points right (+U in image), axis_v points up (+V is DOWN in image),
// normal points toward the camera (out of the projection plane).
//
// UV convention: u=0 left, u=1 right, v=0 top, v=1 bottom — matches DecodedImage.
struct ProjectorFrame {
    Vec3d origin  = Vec3d::Zero();
    Vec3d axis_u  = Vec3d::UnitX();  // image right
    Vec3d axis_v  = Vec3d::UnitY();  // image up  (v increases downward in UV)
    Vec3d normal  = Vec3d::UnitZ();  // toward viewer
};

// Settings for a planar projection onto the mesh surface.
// All distances in millimetres. Rotation in radians around the projector normal.
struct PlanarProjectionSettings {
    ProjectorFrame frame;

    double width_mm  = 100.0;
    double height_mm = 100.0;
    double rotation_radians = 0.0;

    bool mirror_u = false;
    bool mirror_v = false;

    // Faces whose centroid alpha-weighted coverage is below this are unpainted.
    double alpha_threshold = 0.05;

    // Minimum fraction of Gaussian sample points that must be inside the
    // image footprint for the face to be considered a candidate.
    double minimum_coverage = 0.5;

    // dot(face_normal, -projection_direction) >= threshold to be front-facing.
    // 0.0 = any face not exactly perpendicular; increase to exclude grazing faces.
    double front_face_cosine_threshold = 0.0;

    // When false (default), back-facing faces are skipped.
    bool paint_through = false;
};

// Result of projecting a single 3D point onto the image plane.
// Coordinate space: projector space, u in [0,1] left-to-right,
//                   v in [0,1] top-to-bottom.
struct ProjectedPoint {
    double u     = 0.0;
    double v     = 0.0;
    double depth = 0.0;  // signed distance along normal (positive = in front)
    bool   inside = false; // true when u and v are both in [0, 1]
};

// Build a right-handed orthonormal ProjectorFrame from an arbitrary camera
// direction and an up hint. Returns error if the combination is degenerate
// (direction parallel to up hint within tolerance).
//
// Coordinate space: look_direction, up_hint, and origin must share one space
// (mesh-local, object, or world). The returned frame is in that same space.
Expected<ProjectorFrame, ImagePaintError>
make_projector_frame(const Vec3d& look_direction,
                     const Vec3d& up_hint,
                     const Vec3d& origin);

// Fit a planar projector so the image plane covers all mesh vertices when
// viewed along look_direction (same coordinate space as vertices).
//
// image_aspect_w_over_h:
//   > 0  — expand width or height so plane aspect matches the image
//          (avoids stretching the source image onto the mesh)
//   <= 0 — use the natural projected mesh extent (may stretch the image)
//
// margin: scale factor applied to both dimensions (1.02 = 2% pad).
//
// Resulting PlanarProjectionSettings has frame, width_mm, height_mm set;
// other fields keep PlanarProjectionSettings defaults.
Expected<PlanarProjectionSettings, ImagePaintError>
fit_planar_projection(Span<const Vec3f> vertices,
                      const Vec3d&      look_direction,
                      const Vec3d&      up_hint,
                      double            image_aspect_w_over_h = 0.0,
                      double            margin = 1.02);

// Project a single point from the coordinate space of the ProjectorFrame.
// p must already be in the same space as frame.origin/axes.
ProjectedPoint project_planar(const Vec3d&                   p,
                               const PlanarProjectionSettings& s);

// Apply rotation and mirror around image centre (0.5, 0.5) to a UV coordinate.
// Called internally; exposed for tests.
std::pair<double,double> apply_rotation_mirror(double u, double v,
                                                double rotation_radians,
                                                bool   mirror_u,
                                                bool   mirror_v);

// ---------------------------------------------------------------------------
// Phase 7 — Cylindrical projection (Roadmap.md §11).
// ---------------------------------------------------------------------------

// A right-handed orthonormal cylinder frame in mesh-local/object/world space.
// axis is the cylinder's rotation axis; radial_basis is the "0 angle"
// direction (perpendicular to axis); tangent = axis x radial_basis.
struct CylinderFrame {
    Vec3d origin        = Vec3d::Zero();
    Vec3d axis          = Vec3d::UnitZ();
    Vec3d radial_basis  = Vec3d::UnitX();
    Vec3d tangent       = Vec3d::UnitY();
};

// Settings for a cylindrical projection onto the mesh surface.
// All distances in millimetres, all angles in radians.
struct CylindricalProjectionSettings {
    CylinderFrame frame;

    // seam_angle_radians names the angular direction that maps to u=0.5
    // (the mapping's "front"); the UV seam (u wraps 0<->1) sits at the
    // opposite angle, seam_angle_radians + pi. This mirrors the standard
    // cylindrical-UV convention of naming a front-facing reference angle
    // rather than the seam location itself.
    double seam_angle_radians = 0.0;

    // Total angular extent mapped across u in [0,1]. 2*pi = full wrap
    // (seamless, textures the whole circumference); less than 2*pi paints
    // only an angular slice, leaving the rest outside.
    double wrap_angle_radians = 2.0 * PI;

    // Total height mapped across v in [0,1], centred on the frame origin
    // (height=0 maps to v=0.5).
    double height_mm = 100.0;

    // Points whose radial distance from the axis falls outside this range
    // are excluded (ProjectedPoint::inside = false) — used to filter the
    // inner wall of a hollow cylindrical object from the outer surface.
    double min_radius_mm = 0.0;
    double max_radius_mm = std::numeric_limits<double>::infinity();

    bool mirror_u = false;
    bool mirror_v = false;
};

// Build a right-handed orthonormal CylinderFrame from a cylinder axis and a
// radial-direction hint (the direction that becomes u=0.5). Returns error if
// axis and radial_hint are parallel within tolerance (degenerate frame),
// matching make_projector_frame's contract.
Expected<CylinderFrame, ImagePaintError>
make_cylinder_frame(const Vec3d& axis_direction,
                    const Vec3d& radial_hint,
                    const Vec3d& origin);

// Project a single point from the coordinate space of the CylinderFrame.
// p must already be in the same space as frame.origin/axes.
ProjectedPoint project_cylindrical(const Vec3d&                          p,
                                    const CylindricalProjectionSettings& s);

} // namespace Slic3r::ImagePaint
