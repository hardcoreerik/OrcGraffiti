#pragma once

// Include GLGizmoBase before I18N to avoid libigl/macro collision.
#include "GLGizmoBase.hpp"

#include "libslic3r/ImagePaint/ImagePaintPipeline.hpp"
#include "libslic3r/ObjectID.hpp"

#include "slic3r/GUI/Jobs/Worker.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace Slic3r::GUI {

// Image-paint gizmo — projects a 2D image onto the selected volume's surface
// and writes the result as per-face MMU segmentation (mmu_segmentation_facets).
//
// Coordinate space (INV-009):
//   Mesh snapshot is volume-local. Camera look/up are transformed into
//   mesh-local via inverse(world_matrix).linear() before fit_planar_projection.
//   Face indices remain volume-local for TriangleSelector apply.
class GLGizmoImagePainter : public GLGizmoBase
{
public:
    GLGizmoImagePainter(GLCanvas3D& parent, const std::string& icon_filename,
                        unsigned int sprite_id);
    ~GLGizmoImagePainter() override;

protected:
    // GLGizmoBase interface.
    bool        on_init() override;
    std::string on_get_name() const override;
    bool        on_is_activable() const override;
    void        on_set_state() override;
    void        on_render() override {}
    // Picking uses raycasters (GLGizmoBase::on_register_raycasters_for_picking);
    // no separate on_render_for_picking in current OrcaSlicer base.
    void        on_render_input_window(float x, float y, float bottom_limit) override;

private:
    void apply();
    void cancel_job();

    // Recompute width/height from current selection + camera (mesh-local fit).
    // Returns false and sets m_status_text on failure.
    bool fit_to_view();

    // Optional image aspect (width/height). 0 = unknown / use mesh extent only.
    double image_aspect_ratio() const;

    // UI state — persists while gizmo is open.
    char  m_image_path[1024] = {};
    float m_width_mm         = 100.f;
    float m_height_mm        = 100.f;
    int   m_target_colors    = 4;

    // When true (default), Apply runs fit_to_view() so mapping matches the
    // current camera and mesh size instead of a fixed 100×100 mm plane.
    bool  m_auto_fit_to_view = true;

    // Cached image pixel size for aspect-correct fit (0 if unknown).
    int   m_image_px_w = 0;
    int   m_image_px_h = 0;

    // Job infrastructure.
    std::unique_ptr<Worker>            m_worker;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    bool                               m_job_running = false;
    std::string                        m_status_text;

    // Volume tracked when gizmo opened — used to detect deselection.
    ObjectID m_tracked_volume_id;
};

} // namespace Slic3r::GUI
