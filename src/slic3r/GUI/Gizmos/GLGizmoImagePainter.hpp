#pragma once

// Include GLGizmoBase before I18N to avoid libigl/macro collision.
#include "GLGizmoBase.hpp"

#include "libslic3r/ImagePaint/ImagePaintPipeline.hpp"
#include "libslic3r/ImagePaint/Projection.hpp"
#include "libslic3r/ObjectID.hpp"

#include "slic3r/GUI/Jobs/Worker.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace Slic3r::GUI {

// Image-paint gizmo — projects a 2D image onto the selected volume's surface
// and writes the result as per-face MMU segmentation (mmu_segmentation_facets).
//
// Primary workflow (matches MakerWorld's "Mesh Graffiti" tool, which the user
// asked to be replicated): pick a View preset button (Top/Back/Front/Left/
// Right/Bottom — same six presets and vectors as the orcgraffiti CLI's
// --view flag, shared via ImagePaint::view_preset_vectors so the two never
// drift apart), adjust Size (percent of the auto-fit extent for that view)
// and Rotation, then Apply. No raycasting/mesh-click placement — the frame
// comes purely from the chosen view direction, matching MakerWorld's
// "image fixed on screen, move the model into position" mechanic more
// closely than an in-3D decal cursor would.
//
// A full crop/vectorize/posterize upload wizard and MakerWorld's custom
// "click a point to align" camera-orientation gizmo are NOT implemented —
// scoped out as later work; see AI_STATUS.md.
//
// Coordinate space (INV-009):
//   Mesh snapshot is volume-local. View presets are volume-local vectors
//   (Projection.hpp) — no camera transform needed, unlike the legacy
//   camera-facing Apply path (Advanced section), which still transforms
//   camera look/up into mesh-local via inverse(world_matrix).linear().
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
    // Returns false and sets m_status_text on failure. Legacy/Advanced path.
    bool fit_to_view();

    // Optional image aspect (width/height). 0 = unknown / use mesh extent only.
    double image_aspect_ratio() const;

    // Build a PlanarProjectionSettings from the selected View preset
    // (m_view_preset), auto-fit to the mesh, then scaled by m_size_percent
    // and rotated by m_rotation_deg. Returns nullopt and sets m_status_text
    // on failure (mirrors fit_to_view()'s error-reporting convention).
    std::optional<Slic3r::ImagePaint::PlanarProjectionSettings>
    build_view_preset_projection(const std::vector<Vec3f>& vertices);

    // UI state — persists while gizmo is open.
    char  m_image_path[1024] = {};
    float m_width_mm         = 100.f;
    float m_height_mm        = 100.f;
    int   m_target_colors    = 4;

    // When true (default), the legacy Advanced Apply path runs fit_to_view()
    // instead of a fixed 100x100mm plane.
    bool  m_auto_fit_to_view = true;

    // Cached image pixel size for aspect-correct fit (0 if unknown).
    int   m_image_px_w = 0;
    int   m_image_px_h = 0;

    // --- Primary workflow: View preset + Size/Rotate ---
    // -1 = no preset chosen yet (Apply falls back to camera-facing fit,
    // same as the legacy path, until the user picks a view).
    int   m_view_preset   = -1; // indexes Slic3r::ImagePaint::ViewPreset
    float m_size_percent  = 100.f; // percent of the auto-fit extent for that view
    float m_rotation_deg  = 0.f;

    // Horizontal flip of the image before projection (PlanarProjectionSettings::mirror_u).
    // Needed because a view preset's projector frame can end up mirrored
    // relative to how the user expects to read the image on that face —
    // e.g. text coming out backwards on a side view.
    bool m_mirror_u = false;

    // Fine-detail subdivision target edge length, mm. 0 = off (one flat
    // colour per original mesh triangle, the pre-existing behavior). Non-zero
    // subdivides each painted face's paint resolution — via TriangleSelector's
    // own virtual split tree, never the mesh itself — so a color patch isn't
    // capped by the source mesh's triangle density. See
    // ImagePaintRequest::detail_edge_length_mm and AI_STATUS.md.
    float m_detail_mm = 0.5f;

    // Job infrastructure.
    std::unique_ptr<Worker>            m_worker;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    bool                               m_job_running = false;
    std::string                        m_status_text;

    // Volume tracked when gizmo opened — used to detect deselection.
    ObjectID m_tracked_volume_id;
};

} // namespace Slic3r::GUI
