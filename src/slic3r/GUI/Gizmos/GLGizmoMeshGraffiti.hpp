#pragma once

// Include GLGizmoBase before I18N to avoid libigl/macro collision.
#include "GLGizmoBase.hpp"

#include "libslic3r/ImagePaint/MeshRemesh.hpp"
#include "libslic3r/ImagePaint/Projection.hpp"
#include "libslic3r/ObjectID.hpp"

#include "slic3r/GUI/Jobs/Worker.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace Slic3r::GUI {

// Mesh Graffiti gizmo — real geometry whose triangle edges follow the
// image's color boundaries (constrained Delaunay triangulation, see
// libslic3r/ImagePaint/MeshRemesh.hpp), instead of per-face MMU painting on
// the unchanged mesh (that's GLGizmoImagePainter, kept separate/unchanged).
//
// Workflow deliberately mirrors GLGizmoImagePainter's View/Size/Rotate/Flip/
// Colors panel as closely as possible — both already replicate MakerWorld's
// Mesh Graffiti tool (image fixed to a view direction, model positioned via
// preset buttons + Size/Rotate, not a camera-facing/raycast frame). This
// gizmo drops the legacy camera-facing "Advanced" section entirely — it
// isn't part of that workflow, and doesn't fit a geometry-replacing Apply
// (no auto-fit-to-current-camera concept makes sense once Apply produces a
// new baked mesh rather than paint data than can be freely re-applied).
//
// Coordinate space: mesh snapshot and view-preset vectors are volume-local,
// same as GLGizmoImagePainter — see that gizmo's header for the invariant
// notes (INV-009) that also apply here unchanged.
class GLGizmoMeshGraffiti : public GLGizmoBase
{
public:
    GLGizmoMeshGraffiti(GLCanvas3D& parent, const std::string& icon_filename,
                        unsigned int sprite_id);
    ~GLGizmoMeshGraffiti() override;

protected:
    bool        on_init() override;
    std::string on_get_name() const override;
    bool        on_is_activable() const override;
    void        on_set_state() override;
    void        on_render() override {}
    void        on_render_input_window(float x, float y, float bottom_limit) override;

private:
    void apply();
    void cancel_job();
    double image_aspect_ratio() const;

    std::optional<Slic3r::ImagePaint::PlanarProjectionSettings>
    build_view_preset_projection(const std::vector<Vec3f>& vertices);

    char  m_image_path[1024] = {};
    int   m_image_px_w = 0;
    int   m_image_px_h = 0;

    int   m_view_preset   = -1; // indexes Slic3r::ImagePaint::ViewPreset
    float m_size_percent  = 100.f;
    float m_rotation_deg  = 0.f;
    bool  m_mirror_u      = false;
    int   m_target_colors = 4;

    // CDT grid resolution (NxN local barycentric grid per remeshed face) —
    // the remesh analogue of GLGizmoImagePainter's mm-based Detail slider.
    // Higher = finer boundary tracing, more triangles.
    int   m_grid_resolution = 12;

    std::unique_ptr<Worker>            m_worker;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    bool                               m_job_running = false;
    std::string                        m_status_text;

    ObjectID m_tracked_volume_id;
};

} // namespace Slic3r::GUI
