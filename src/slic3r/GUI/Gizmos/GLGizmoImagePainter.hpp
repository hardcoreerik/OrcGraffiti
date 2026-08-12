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

class GLModel;

// Image-paint gizmo — projects a 2D image onto the selected volume's surface
// and writes the result as per-face MMU segmentation (mmu_segmentation_facets).
//
// Primary interaction is "stamp": move the mouse over the model, a live
// cursor marker follows the raycast hit point; click to stamp the image
// there, oriented to the LOCAL surface normal at that point (not a fixed
// camera direction — see stamp_at()). Auto-fit/manual-size Apply remains
// available as a secondary path for precise full-surface placement.
//
// Coordinate space (INV-009):
//   Mesh snapshot is volume-local. Raycast hit/normal come back mesh-local
//   from MeshRaycaster::unproject_on_mesh (see GLGizmoPainterBase for the
//   same convention). Camera look/up (legacy Apply path) are transformed
//   into mesh-local via inverse(world_matrix).linear() before
//   fit_planar_projection. Face indices remain volume-local for
//   TriangleSelector apply.
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
    void        on_render() override;
    bool        on_mouse(const wxMouseEvent& mouse_event) override;
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

    // --- Stamp workflow ---
    // Rebuild m_raycaster for the currently selected volume if it changed.
    // Returns the selected ModelVolume (mesh-local) or nullptr if none.
    const ModelVolume* refresh_raycaster();
    // Raycast the current mouse position against the tracked mesh; updates
    // m_hover_valid/m_hover_hit_local/m_hover_normal_local.
    void update_hover();
    // Draw a live marker at the hover hit point (sphere, scaled to stamp size).
    void render_cursor() const;
    // Build a projector frame from a raycast hit (mesh-local point + normal)
    // and submit the same ImagePaintJob pipeline as apply(), oriented to the
    // local surface instead of the camera.
    void stamp_at(const Vec3f& hit_local, const Vec3f& normal_local);

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

    // Stamp size (mm) for the click-to-stamp workflow — independent of
    // m_width_mm/m_height_mm, which remain the legacy full-surface Apply size.
    float m_stamp_width_mm  = 30.f;
    float m_stamp_height_mm = 30.f;

    // Raycasting for the stamp cursor/click, rebuilt when the selected
    // volume changes.
    std::unique_ptr<MeshRaycaster> m_raycaster;
    ObjectID m_raycaster_volume_id;
    bool     m_hover_valid = false;
    Vec3f    m_hover_hit_local    = Vec3f::Zero();
    Vec3f    m_hover_normal_local = Vec3f::UnitZ();
    mutable std::shared_ptr<GLModel> m_cursor_sphere; // lazily built in render_cursor()

    // Job infrastructure.
    std::unique_ptr<Worker>            m_worker;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    bool                               m_job_running = false;
    std::string                        m_status_text;

    // Volume tracked when gizmo opened — used to detect deselection.
    ObjectID m_tracked_volume_id;
};

} // namespace Slic3r::GUI
