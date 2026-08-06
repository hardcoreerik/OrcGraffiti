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
// Phase 5 scaffold: ImGui panel with image path, size, and Apply button.
// Interactive placement handles are planned for Phase 6.
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
    void        on_render_for_picking() override {}
    void        on_render_input_window(float x, float y, float bottom_limit) override;

private:
    void apply();
    void cancel_job();

    // UI state — persists while gizmo is open.
    char  m_image_path[1024] = {};
    float m_width_mm         = 100.f;
    float m_height_mm        = 100.f;
    int   m_target_colors    = 4;

    // Projection axis selector: 0=Top(-Z), 1=Front(-Y), 2=Right(-X).
    int   m_projection_axis  = 1;

    // Job infrastructure.
    std::unique_ptr<Worker>            m_worker;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    bool                               m_job_running = false;
    std::string                        m_status_text;

    // Volume tracked when gizmo opened — used to detect deselection.
    ObjectID m_tracked_volume_id;
};

} // namespace Slic3r::GUI
