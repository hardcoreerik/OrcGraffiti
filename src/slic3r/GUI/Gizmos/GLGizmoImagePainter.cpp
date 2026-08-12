#include "GLGizmoImagePainter.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/ImagePaint/TopologyFingerprint.hpp"
#include "libslic3r/ImagePaint/Projection.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include "slic3r/GUI/GLShader.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "slic3r/GUI/Jobs/BoostThreadWorker.hpp"
#include "slic3r/GUI/Jobs/PlaterWorker.hpp"
#include "slic3r/GUI/Jobs/ImagePaintJob.hpp"
#include "slic3r/GUI/3DScene.hpp"  // GLVolume::world_matrix

#include <glad/gl.h>
#include <imgui/imgui.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/string.h>

#include <cassert>
#include <cstring>
#include <vector>

namespace Slic3r::GUI {

// ---------------------------------------------------------------------------

GLGizmoImagePainter::GLGizmoImagePainter(GLCanvas3D&        parent,
                                           const std::string& icon_filename,
                                           unsigned int       sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{}

GLGizmoImagePainter::~GLGizmoImagePainter()
{
    cancel_job();
}

bool GLGizmoImagePainter::on_init()
{
    return true;
}

std::string GLGizmoImagePainter::on_get_name() const
{
    return _u8L("Image Paint");
}

bool GLGizmoImagePainter::on_is_activable() const
{
    const Selection& sel = m_parent.get_selection();
    return sel.is_single_full_object() || sel.is_single_volume();
}

void GLGizmoImagePainter::on_set_state()
{
    if (m_state == Off) {
        cancel_job();
        m_job_running = false;
        m_status_text.clear();
        m_raycaster.reset();
        m_hover_valid = false;
    }
}

void GLGizmoImagePainter::on_render()
{
    update_hover();
    if (m_hover_valid)
        render_cursor();
}

bool GLGizmoImagePainter::on_mouse(const wxMouseEvent& mouse_event)
{
    // Left-click stamps at the current hover point; every other event is
    // left for the base class / camera navigation. Only a plain click (no
    // drag) commits — Dragging is deliberately not handled here, so orbit/
    // pan gestures that happen to start over the model don't fire repeated
    // stamps.
    if (mouse_event.LeftDown() && m_hover_valid && !m_job_running) {
        stamp_at(m_hover_hit_local, m_hover_normal_local);
        return true;
    }
    return false;
}

void GLGizmoImagePainter::cancel_job()
{
    m_cancel->store(true);
    if (m_worker)
        m_worker->cancel_all();
    m_cancel = std::make_shared<std::atomic<bool>>(false);
}

double GLGizmoImagePainter::image_aspect_ratio() const
{
    if (m_image_px_w > 0 && m_image_px_h > 0)
        return static_cast<double>(m_image_px_w) / static_cast<double>(m_image_px_h);
    return 0.0;
}

// ---------------------------------------------------------------------------
// Fit projector to the selected mesh as seen from the current camera.
// All math in volume-local space (Project_Truth §14).
// ---------------------------------------------------------------------------

bool GLGizmoImagePainter::fit_to_view()
{
    const Selection& sel = m_parent.get_selection();
    const Model* model = sel.get_model();
    if (!model) {
        m_status_text = _u8L("No model.");
        return false;
    }

    const auto& vols = sel.get_volume_idxs();
    if (vols.empty()) {
        m_status_text = _u8L("No volume selected.");
        return false;
    }
    const GLVolume* glvol = sel.get_volume(*vols.begin());
    if (!glvol) {
        m_status_text = _u8L("No volume selected.");
        return false;
    }

    const ModelVolume* mv = get_model_volume(*glvol, *model);
    if (!mv) {
        m_status_text = _u8L("Cannot find selected volume.");
        return false;
    }

    const TriangleMesh& mesh = mv->mesh();
    if (mesh.its.vertices.empty()) {
        m_status_text = _u8L("Selected volume has no faces.");
        return false;
    }

    // World camera → mesh-local directions (linear part only, then re-normalize).
    const Camera&    cam   = m_parent.get_camera();
    const Transform3d w2l  = glvol->world_matrix().inverse();
    const Vec3d look_local = (w2l.linear() * cam.get_dir_forward()).normalized();
    const Vec3d up_local   = (w2l.linear() * cam.get_dir_up()).normalized();

    if (look_local.norm() < 1e-8 || up_local.norm() < 1e-8) {
        m_status_text = _u8L("Cannot build projector frame (degenerate view direction).");
        return false;
    }

    // Snapshot vertices as Span for fit (mesh-local).
    const auto& verts = mesh.its.vertices;
    Slic3r::ImagePaint::Span<const Vec3f> span(verts.data(), verts.size());

    auto fitted = Slic3r::ImagePaint::fit_planar_projection(
        span, look_local, up_local, image_aspect_ratio(), /*margin=*/1.02);
    if (!fitted) {
        m_status_text = fitted.error().user_message;
        return false;
    }

    m_width_mm  = static_cast<float>(fitted->width_mm);
    m_height_mm = static_cast<float>(fitted->height_mm);
    m_status_text = _u8L("Fitted to view.");
    return true;
}

// ---------------------------------------------------------------------------
// Shared job submission — filaments, quantization, and ImagePaintJob dispatch
// are identical between the click-to-stamp and legacy Apply-button paths;
// only the projection differs. Called with req.projection already set.
// ---------------------------------------------------------------------------

static void submit_paint_request(Slic3r::ImagePaint::ImagePaintRequest req,
                                  const ModelVolume* mv,
                                  const TriangleMesh& mesh,
                                  int target_colors,
                                  std::string& status_text,
                                  bool& job_running,
                                  std::unique_ptr<Worker>& worker,
                                  std::shared_ptr<std::atomic<bool>>& cancel_flag)
{
    // Filaments from the active project.
    const auto& extruder_colors = wxGetApp().plater()->get_extruder_colors_from_plater_config();
    for (std::size_t i = 0; i < extruder_colors.size(); ++i) {
        Slic3r::ImagePaint::FilamentColor fc;
        fc.project_index = static_cast<Slic3r::ImagePaint::FilamentIndex>(i);
        fc.name = "Extruder " + std::to_string(i + 1);
        const std::string& hex = extruder_colors[i];
        if (hex.size() == 7 && hex[0] == '#') {
            unsigned r = 0, g = 0, b = 0;
            sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
            fc.display_rgb = {static_cast<uint8_t>(r),
                               static_cast<uint8_t>(g),
                               static_cast<uint8_t>(b)};
        }
        req.filaments.push_back(std::move(fc));
    }
    if (req.filaments.empty()) {
        status_text = _u8L("No filaments configured.");
        return;
    }

    req.quantization.target_colors = static_cast<std::uint32_t>(
        std::max(1, std::min(target_colors, 16)));
    req.quality        = Slic3r::ImagePaint::SamplingQuality::Gaussian7;
    req.merge_policy   = Slic3r::ImagePaint::MergePolicy::OverwriteInsideMask;
    req.cleanup.enabled = true;

    ImagePaintJob::Input job_input;
    job_input.volume_id            = mv->id();
    job_input.expected_fingerprint = Slic3r::ImagePaint::fingerprint(mesh);
    job_input.request              = std::move(req);

    cancel_flag->store(true);
    if (worker)
        worker->cancel_all();
    cancel_flag = std::make_shared<std::atomic<bool>>(false);

    wxWindow* parent_wnd = wxGetApp().plater();
    if (!worker)
        worker = std::make_unique<PlaterWorker<BoostThreadWorker>>(
            parent_wnd, nullptr, "ImagePaintWorker");

    auto job = std::make_shared<ImagePaintJob>(
        std::move(job_input), cancel_flag, parent_wnd);

    job_running = true;
    status_text = _u8L("Computing...");
    worker->push(std::move(job));
}

// ---------------------------------------------------------------------------
// Apply — legacy full-surface path: camera-facing auto-fit or manual size.
// ---------------------------------------------------------------------------

void GLGizmoImagePainter::apply()
{
    if (m_job_running)
        return;

    const std::string path(m_image_path);
    if (path.empty()) {
        m_status_text = _u8L("Select an image first.");
        return;
    }

    // Retrieve the currently selected ModelVolume.
    const Selection& sel    = m_parent.get_selection();
    const Model&     model  = *sel.get_model();

    const GLVolume* glvol = nullptr;
    {
        const auto& vols = sel.get_volume_idxs();
        if (vols.empty()) {
            m_status_text = _u8L("No volume selected.");
            return;
        }
        glvol = sel.get_volume(*vols.begin());
    }

    const ModelVolume* mv = get_model_volume(*glvol, model);
    if (!mv) {
        m_status_text = _u8L("Cannot find selected volume.");
        return;
    }

    // Immutable mesh snapshot — volume-local coordinates.
    const TriangleMesh& mesh = mv->mesh();
    if (mesh.its.indices.empty()) {
        m_status_text = _u8L("Selected volume has no faces.");
        return;
    }

    // Build the request.
    Slic3r::ImagePaint::ImagePaintRequest req;
    req.image_path = path;

    req.vertices.reserve(mesh.its.vertices.size());
    for (const auto& v : mesh.its.vertices)
        req.vertices.push_back(v);

    req.indices.reserve(mesh.its.indices.size());
    for (const auto& t : mesh.its.indices)
        req.indices.push_back(t.cast<int32_t>());

    // --- Projection in mesh-local space ---
    // Transform world camera into mesh-local so look/up match local vertices.
    const Camera&     cam  = m_parent.get_camera();
    const Transform3d w2l  = glvol->world_matrix().inverse();
    const Vec3d look_local = (w2l.linear() * cam.get_dir_forward()).normalized();
    const Vec3d up_local   = (w2l.linear() * cam.get_dir_up()).normalized();

    if (m_auto_fit_to_view || m_width_mm <= 0.f || m_height_mm <= 0.f) {
        Slic3r::ImagePaint::Span<const Vec3f> span(req.vertices.data(), req.vertices.size());
        auto fitted = Slic3r::ImagePaint::fit_planar_projection(
            span, look_local, up_local, image_aspect_ratio(), /*margin=*/1.02);
        if (!fitted) {
            m_status_text = fitted.error().user_message;
            return;
        }
        // Allow somewhat oblique faces; 0.1 was excluding useful surface on organic meshes.
        fitted->front_face_cosine_threshold = 0.05;
        fitted->minimum_coverage = 0.25;
        req.projection = *fitted;
        m_width_mm  = static_cast<float>(fitted->width_mm);
        m_height_mm = static_cast<float>(fitted->height_mm);
    } else {
        // Manual size: still place frame with fit for origin/axes, then override size.
        Slic3r::ImagePaint::Span<const Vec3f> span(req.vertices.data(), req.vertices.size());
        auto fitted = Slic3r::ImagePaint::fit_planar_projection(
            span, look_local, up_local, /*aspect=*/0.0, /*margin=*/1.0);
        if (!fitted) {
            m_status_text = fitted.error().user_message;
            return;
        }
        fitted->width_mm  = m_width_mm;
        fitted->height_mm = m_height_mm;
        // Allow somewhat oblique faces; 0.1 was excluding useful surface on organic meshes.
        fitted->front_face_cosine_threshold = 0.05;
        fitted->minimum_coverage = 0.25;
        req.projection = *fitted;
    }

    submit_paint_request(std::move(req), mv, mesh, m_target_colors,
                         m_status_text, m_job_running, m_worker, m_cancel);
}

// ---------------------------------------------------------------------------
// Stamp workflow — click on the model surface to place the image there.
// ---------------------------------------------------------------------------

const ModelVolume* GLGizmoImagePainter::refresh_raycaster()
{
    const Selection& sel   = m_parent.get_selection();
    const Model*     model = sel.get_model();
    if (!model)
        return nullptr;

    const auto& vols = sel.get_volume_idxs();
    if (vols.empty())
        return nullptr;
    const GLVolume* glvol = sel.get_volume(*vols.begin());
    if (!glvol)
        return nullptr;

    const ModelVolume* mv = get_model_volume(*glvol, *model);
    if (!mv || mv->mesh().its.indices.empty())
        return nullptr;

    if (!m_raycaster || m_raycaster_volume_id != mv->id()) {
        m_raycaster = std::make_unique<MeshRaycaster>(mv->mesh());
        m_raycaster_volume_id = mv->id();
    }
    return mv;
}

void GLGizmoImagePainter::update_hover()
{
    m_hover_valid = false;

    if (m_job_running || std::string(m_image_path).empty())
        return;

    const Selection& sel = m_parent.get_selection();
    const auto& vols = sel.get_volume_idxs();
    if (vols.empty())
        return;
    const GLVolume* glvol = sel.get_volume(*vols.begin());
    if (!glvol)
        return;

    if (!refresh_raycaster())
        return;

    const Camera& camera = wxGetApp().plater()->get_camera();
    Vec3f hit, normal;
    if (!m_raycaster->unproject_on_mesh(m_parent.get_local_mouse_position(),
                                        glvol->world_matrix(), camera, hit, normal))
        return;

    m_hover_valid         = true;
    m_hover_hit_local     = hit;
    m_hover_normal_local  = normal;
}

void GLGizmoImagePainter::render_cursor() const
{
    const Selection& sel = m_parent.get_selection();
    const auto& vols = sel.get_volume_idxs();
    if (vols.empty())
        return;
    const GLVolume* glvol = sel.get_volume(*vols.begin());
    if (!glvol)
        return;

    if (m_cursor_sphere == nullptr) {
        m_cursor_sphere = std::make_shared<GLModel>();
        m_cursor_sphere->init_from(its_make_sphere(1.0, double(PI) / 12.0));
    }

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    shader->start_using();

    const Camera& camera = wxGetApp().plater()->get_camera();
    // Marker radius: a fraction of the stamp size, just enough to show
    // where/how-large the next stamp will land — not a full decal outline.
    const double marker_radius = 0.15 * std::min(m_stamp_width_mm, m_stamp_height_mm);
    const Transform3d view_model_matrix = camera.get_view_matrix() * glvol->world_matrix() *
        Geometry::assemble_transform(m_hover_hit_local.cast<double>()) *
        Geometry::assemble_transform(Vec3d::Zero(), Vec3d::Zero(), marker_radius * Vec3d::Ones());

    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    const bool is_left_handed = Geometry::Transformation(view_model_matrix).is_left_handed();
    if (is_left_handed)
        glsafe(::glFrontFace(GL_CW));

    m_cursor_sphere->set_color(ColorRGBA(0.3f, 0.9f, 0.3f, 0.85f)); // green = "ready to stamp"
    m_cursor_sphere->render();

    if (is_left_handed)
        glsafe(::glFrontFace(GL_CCW));

    shader->stop_using();
}

// Build an up_hint that is never (near-)parallel to look_direction.
static Vec3d pick_up_hint(const Vec3d& look_direction)
{
    constexpr double kParallelEps = 0.05;
    const Vec3d candidates[] = {Vec3d::UnitZ(), Vec3d::UnitY(), Vec3d::UnitX()};
    for (const Vec3d& c : candidates) {
        if (c.cross(look_direction).norm() > kParallelEps)
            return c;
    }
    return Vec3d::UnitY(); // unreachable: three orthogonal axes can't all be parallel
}

void GLGizmoImagePainter::stamp_at(const Vec3f& hit_local, const Vec3f& normal_local)
{
    if (m_job_running)
        return;

    const std::string path(m_image_path);
    if (path.empty()) {
        m_status_text = _u8L("Select an image first.");
        return;
    }

    const ModelVolume* mv = refresh_raycaster();
    if (!mv) {
        m_status_text = _u8L("No volume selected.");
        return;
    }
    const TriangleMesh& mesh = mv->mesh();

    Slic3r::ImagePaint::ImagePaintRequest req;
    req.image_path = path;
    req.vertices.reserve(mesh.its.vertices.size());
    for (const auto& v : mesh.its.vertices)
        req.vertices.push_back(v);
    req.indices.reserve(mesh.its.indices.size());
    for (const auto& t : mesh.its.indices)
        req.indices.push_back(t.cast<int32_t>());

    // Frame the projector at the hit point, facing into the surface along
    // the OUTWARD normal's opposite (so the image projects onto, not away
    // from, the mesh) — matches fit_to_view()'s "look toward the surface"
    // convention (camera forward there plays the same role as -normal here).
    const Vec3d origin = hit_local.cast<double>();
    const Vec3d look    = (-normal_local).cast<double>().normalized();
    const Vec3d up_hint = pick_up_hint(look);

    auto frame = Slic3r::ImagePaint::make_projector_frame(look, up_hint, origin);
    if (!frame) {
        m_status_text = frame.error().user_message;
        return;
    }

    Slic3r::ImagePaint::PlanarProjectionSettings proj;
    proj.frame  = *frame;
    proj.width_mm  = m_stamp_width_mm;
    proj.height_mm = m_stamp_height_mm;
    proj.front_face_cosine_threshold = 0.05;
    proj.minimum_coverage = 0.25;
    req.projection = proj;

    submit_paint_request(std::move(req), mv, mesh, m_target_colors,
                         m_status_text, m_job_running, m_worker, m_cancel);
}

// ---------------------------------------------------------------------------
// ImGui panel
// ---------------------------------------------------------------------------

void GLGizmoImagePainter::on_render_input_window(float x, float y, float /*bottom_limit*/)
{
    // Detect job completion (worker becomes idle).
    if (m_job_running && m_worker && m_worker->is_idle()) {
        m_job_running = false;
        m_status_text = _u8L("Done.");
    }

    const float unit = m_imgui->scaled(1.0f);

    m_imgui->push_common_window_style(m_parent.get_scale());
    m_imgui->begin(on_get_name(),
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse);

    // --- Image path ---
    m_imgui->text(_L("Image"));
    ImGui::SameLine(unit * 8.f);
    ImGui::PushItemWidth(unit * 18.f);
    ImGui::InputText("##img_path", m_image_path, sizeof(m_image_path));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (m_imgui->button(_L("Browse"))) {
        // Parent must be a complete wxWindow type (wxGLCanvas is only forward-declared here).
        wxFileDialog dlg(wxGetApp().plater(),
                         _L("Open image"), "", "",
                         "Image files (*.jpg;*.jpeg;*.png;*.bmp)|*.jpg;*.jpeg;*.png;*.bmp",
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK) {
            const std::string sel = dlg.GetPath().ToUTF8().data();
            std::strncpy(m_image_path, sel.c_str(), sizeof(m_image_path) - 1);
            m_image_path[sizeof(m_image_path) - 1] = '\0';

            // Cache pixel size for aspect-correct fit (no full pipeline decode).
            m_image_px_w = 0;
            m_image_px_h = 0;
            wxImage probe;
            if (probe.LoadFile(wxString::FromUTF8(sel), wxBITMAP_TYPE_ANY) && probe.IsOk()) {
                m_image_px_w = probe.GetWidth();
                m_image_px_h = probe.GetHeight();
            }
        }
    }

    if (m_image_px_w > 0 && m_image_px_h > 0) {
        ImGui::SameLine();
        m_imgui->text_colored(ImVec4(0.55f, 0.55f, 0.55f, 1.f),
                              GUI::format("%1%x%2%", m_image_px_w, m_image_px_h));
    }

    ImGui::Separator();

    // --- Stamp (primary workflow) ---
    m_imgui->text(_L("Stamp size (mm)"));
    ImGui::SameLine(unit * 12.f);
    ImGui::PushItemWidth(unit * 6.f);
    ImGui::InputFloat("##sw", &m_stamp_width_mm, 1.f, 10.f, "%.1f");
    ImGui::SameLine();
    ImGui::InputFloat("##sh", &m_stamp_height_mm, 1.f, 10.f, "%.1f");
    ImGui::PopItemWidth();
    m_stamp_width_mm  = std::max(1.f, m_stamp_width_mm);
    m_stamp_height_mm = std::max(1.f, m_stamp_height_mm);

    // --- Colors ---
    m_imgui->text(_L("Colors"));
    ImGui::SameLine(unit * 12.f);
    ImGui::PushItemWidth(unit * 5.f);
    ImGui::InputInt("##colors", &m_target_colors, 1, 1);
    ImGui::PopItemWidth();
    m_target_colors = std::max(1, std::min(m_target_colors, 16));

    if (m_job_running) {
        ImGui::Separator();
        m_imgui->text_colored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), m_status_text);
        if (m_imgui->button(_L("Cancel")))
            cancel_job();
    } else {
        ImGui::Separator();
        m_imgui->text_colored(ImVec4(0.5f, 0.5f, 0.5f, 1.f),
                              _u8L("Click on the model to stamp the image there,\n"
                                   "oriented to the surface at that point."));
        if (!m_status_text.empty())
            m_imgui->text_colored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), m_status_text);
    }

    // --- Advanced: legacy full-surface Apply (camera-facing plane) ---
    if (ImGui::CollapsingHeader(_u8L("Advanced: full-surface projection").c_str())) {
        ImGui::Checkbox(_u8L("Auto-fit to view").c_str(), &m_auto_fit_to_view);
        ImGui::SameLine();
        m_imgui->disabled_begin(m_job_running);
        if (m_imgui->button(_L("Fit now")))
            fit_to_view();
        m_imgui->disabled_end();

        m_imgui->disabled_begin(m_auto_fit_to_view);
        m_imgui->text(_L("Width (mm)"));
        ImGui::SameLine(unit * 8.f);
        ImGui::PushItemWidth(unit * 8.f);
        ImGui::InputFloat("##w", &m_width_mm, 1.f, 10.f, "%.1f");
        ImGui::PopItemWidth();

        m_imgui->text(_L("Height (mm)"));
        ImGui::SameLine(unit * 8.f);
        ImGui::PushItemWidth(unit * 8.f);
        ImGui::InputFloat("##h", &m_height_mm, 1.f, 10.f, "%.1f");
        ImGui::PopItemWidth();
        m_imgui->disabled_end();

        m_imgui->disabled_begin(m_job_running);
        if (m_imgui->button(_L("Apply to whole view")))
            apply();
        m_imgui->disabled_end();
    }

    m_imgui->end();
    m_imgui->pop_common_window_style();
}

} // namespace Slic3r::GUI
