#pragma once

// Include GLGizmoBase before I18N to avoid libigl/macro collision.
#include "GLGizmoBase.hpp"

#include "libslic3r/ImagePaint/MeshRemesh.hpp"
#include "libslic3r/ImagePaint/Projection.hpp"
#include "libslic3r/ObjectID.hpp"

#include "slic3r/GUI/GLTexture.hpp"
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
// MakerWorld workflow: the image is a screen-space overlay locked to the
// center of the 3D viewport. The user orbits/pans/zooms the camera (or
// moves the object) until the desired surface sits behind the image, then
// Apply. Size% and Rotate change the overlay live; paint math is the
// camera-facing plane at the object's current pose — not a View-preset
// silhouette. Look buttons are camera shortcuts only.
//
// Coordinate space: mesh snapshot is volume-local; camera look/up/origin
// are transformed through GLVolume::world_matrix().inverse() before
// make_projector_frame (INV-009). Overlay rendering is screen pixels.
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
    bool ensure_overlay_texture();
    void overlay_pixel_size(float& out_w, float& out_h) const;
    void render_screen_overlay();

    std::optional<Slic3r::ImagePaint::PlanarProjectionSettings>
    build_camera_facing_projection(const GLVolume& glvol);

    char  m_image_path[1024] = {};
    int   m_image_px_w = 0;
    int   m_image_px_h = 0;

    // Last Look-button press — camera shortcut highlight only, never
    // consumed by Apply.
    int   m_look_preset  = -1;
    float m_size_percent  = 100.f;
    float m_rotation_deg  = 0.f;
    bool  m_mirror_u      = false;
    int   m_target_colors = 4;

    // CDT grid resolution (NxN local barycentric grid per remeshed face).
    int   m_grid_resolution = 12;

    GLTexture  m_overlay;
    std::string m_overlay_path;

    std::unique_ptr<Worker>            m_worker;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    bool                               m_job_running = false;
    std::string                        m_status_text;

    ObjectID m_tracked_volume_id;
};

} // namespace Slic3r::GUI
