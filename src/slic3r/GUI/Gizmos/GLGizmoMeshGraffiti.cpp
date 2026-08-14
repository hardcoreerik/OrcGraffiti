#include "GLGizmoMeshGraffiti.hpp"

#include "libslic3r/libslic3r.h"  // PI (global namespace, defined before Slic3r{})
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/ImagePaint/TopologyFingerprint.hpp"
#include "libslic3r/ImagePaint/Projection.hpp"

#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/Jobs/BoostThreadWorker.hpp"
#include "slic3r/GUI/Jobs/PlaterWorker.hpp"
#include "slic3r/GUI/Jobs/MeshRemeshJob.hpp"
#include "slic3r/GUI/3DScene.hpp"  // GLVolume::world_matrix

#include <imgui/imgui.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/string.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

namespace Slic3r::GUI {

// Size 100% = this fraction of the shorter canvas edge. Range goes to
// kSizeSliderMax so the overlay can still cover the whole view.
static constexpr float kSizeSliderMax         = 300.f;
static constexpr float kOverlayBaseFraction   = 0.45f;
static constexpr int   kOverlayAlpha          = 150;

static bool load_overlay_rgba(GLTexture& tex, const std::string& path, int& w, int& h)
{
    wxImage img;
    if (!img.LoadFile(wxString::FromUTF8(path), wxBITMAP_TYPE_ANY) || !img.IsOk())
        return false;
    w = img.GetWidth();
    h = img.GetHeight();
    if (w <= 0 || h <= 0)
        return false;

    std::vector<unsigned char> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    const unsigned char* rgb = img.GetData();
    const unsigned char* a   = img.HasAlpha() ? img.GetAlpha() : nullptr;
    const int n = w * h;
    for (int i = 0; i < n; ++i) {
        rgba[static_cast<std::size_t>(i) * 4 + 0] = rgb[i * 3 + 0];
        rgba[static_cast<std::size_t>(i) * 4 + 1] = rgb[i * 3 + 1];
        rgba[static_cast<std::size_t>(i) * 4 + 2] = rgb[i * 3 + 2];
        rgba[static_cast<std::size_t>(i) * 4 + 3] = a ? a[i] : 255;
    }
    return tex.load_from_raw_data(std::move(rgba),
                                  static_cast<unsigned int>(w),
                                  static_cast<unsigned int>(h),
                                  false);
}

// ---------------------------------------------------------------------------

GLGizmoMeshGraffiti::GLGizmoMeshGraffiti(GLCanvas3D&        parent,
                                         const std::string& icon_filename,
                                         unsigned int       sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{}

GLGizmoMeshGraffiti::~GLGizmoMeshGraffiti()
{
    cancel_job();
}

bool GLGizmoMeshGraffiti::on_init()
{
    return true;
}

std::string GLGizmoMeshGraffiti::on_get_name() const
{
    return _u8L("Mesh Graffiti");
}

bool GLGizmoMeshGraffiti::on_is_activable() const
{
    const Selection& sel = m_parent.get_selection();
    return sel.is_single_full_object() || sel.is_single_volume();
}

void GLGizmoMeshGraffiti::on_set_state()
{
    if (m_state == Off) {
        cancel_job();
        m_job_running = false;
        m_status_text.clear();
        m_overlay.reset();
        m_overlay_path.clear();
    }
}

void GLGizmoMeshGraffiti::cancel_job()
{
    m_cancel->store(true);
    if (m_worker)
        m_worker->cancel_all();
    m_cancel = std::make_shared<std::atomic<bool>>(false);
}

double GLGizmoMeshGraffiti::image_aspect_ratio() const
{
    if (m_image_px_w > 0 && m_image_px_h > 0)
        return static_cast<double>(m_image_px_w) / static_cast<double>(m_image_px_h);
    return 0.0;
}

bool GLGizmoMeshGraffiti::ensure_overlay_texture()
{
    const std::string path(m_image_path);
    if (path.empty()) {
        m_overlay.reset();
        m_overlay_path.clear();
        return false;
    }
    if (path == m_overlay_path && m_overlay.get_id() != 0)
        return true;

    int w = 0, h = 0;
    if (!load_overlay_rgba(m_overlay, path, w, h)) {
        m_overlay.reset();
        m_overlay_path.clear();
        return false;
    }
    m_overlay_path = path;
    m_image_px_w = w;
    m_image_px_h = h;
    return true;
}

void GLGizmoMeshGraffiti::overlay_pixel_size(float& out_w, float& out_h) const
{
    const Size cs = m_parent.get_canvas_size();
    const float shorter = static_cast<float>(std::max(1, std::min(cs.get_width(), cs.get_height())));
    const float scale = std::clamp(m_size_percent, 1.f, kSizeSliderMax) / 100.f;
    const float base  = shorter * kOverlayBaseFraction * scale;
    const double aspect = image_aspect_ratio();
    if (aspect > 1e-6) {
        if (aspect >= 1.0) {
            out_w = base;
            out_h = static_cast<float>(base / aspect);
        } else {
            out_h = base;
            out_w = static_cast<float>(base * aspect);
        }
    } else {
        out_w = base;
        out_h = base;
    }
}

void GLGizmoMeshGraffiti::render_screen_overlay()
{
    if (!ensure_overlay_texture())
        return;

    float ow = 0.f, oh = 0.f;
    overlay_pixel_size(ow, oh);
    if (ow < 1.f || oh < 1.f)
        return;

    const Size cs = m_parent.get_canvas_size();
    const ImVec2 center(static_cast<float>(cs.get_width()) * 0.5f,
                        static_cast<float>(cs.get_height()) * 0.5f);

    // Same 2D rotation as apply_rotation_mirror so the overlay and the
    // paint plane stay aligned when the Rotate slider moves.
    const double rad = static_cast<double>(m_rotation_deg) * PI / 180.0;
    const float  cos_r = static_cast<float>(std::cos(rad));
    const float  sin_r = static_cast<float>(std::sin(rad));
    auto rot = [&](float x, float y) -> ImVec2 {
        return ImVec2(center.x + cos_r * x + sin_r * y,
                      center.y - sin_r * x + cos_r * y);
    };

    const ImVec2 p1 = rot(-ow * 0.5f, -oh * 0.5f);
    const ImVec2 p2 = rot( ow * 0.5f, -oh * 0.5f);
    const ImVec2 p3 = rot( ow * 0.5f,  oh * 0.5f);
    const ImVec2 p4 = rot(-ow * 0.5f,  oh * 0.5f);

    ImVec2 uv1(0.f, 0.f), uv2(1.f, 0.f), uv3(1.f, 1.f), uv4(0.f, 1.f);
    if (m_mirror_u) {
        std::swap(uv1, uv2);
        std::swap(uv4, uv3);
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImTextureID tid = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(m_overlay.get_id()));
    dl->AddImageQuad(tid, p1, p2, p3, p4, uv1, uv2, uv3, uv4,
                     IM_COL32(255, 255, 255, kOverlayAlpha));
    dl->AddQuad(p1, p2, p3, p4, IM_COL32(255, 255, 255, 70), 1.0f);
}

// ---------------------------------------------------------------------------
// Camera-facing plane whose millimetre size matches the on-screen overlay
// at the selected volume's depth. Volume-local via world_matrix inverse.
// ---------------------------------------------------------------------------

std::optional<Slic3r::ImagePaint::PlanarProjectionSettings>
GLGizmoMeshGraffiti::build_camera_facing_projection(const GLVolume& glvol)
{
    Camera& cam = m_parent.get_camera();
    const Transform3d w2l = glvol.world_matrix().inverse();
    const Vec3d look_local = (w2l.linear() * cam.get_dir_forward()).normalized();
    const Vec3d up_local   = (w2l.linear() * cam.get_dir_up()).normalized();
    if (look_local.norm() < 1e-8 || up_local.norm() < 1e-8) {
        m_status_text = _u8L("Cannot build projector frame (degenerate view direction).");
        return std::nullopt;
    }

    const Vec3d cam_pos = cam.get_position();
    const Vec3d fwd     = cam.get_dir_forward();
    const Vec3d bbox_c  = glvol.transformed_bounding_box().center();
    const double dist   = (bbox_c - cam_pos).dot(fwd);
    if (dist <= 1e-3) {
        m_status_text = _u8L("Model is not in front of the camera.");
        return std::nullopt;
    }

    const Vec3d origin_local = w2l * (cam_pos + dist * fwd);
    auto frame = Slic3r::ImagePaint::make_projector_frame(look_local, up_local, origin_local);
    if (!frame) {
        m_status_text = frame.error().user_message;
        return std::nullopt;
    }

    float ow = 0.f, oh = 0.f;
    overlay_pixel_size(ow, oh);
    const Size cs = m_parent.get_canvas_size();
    const double vp_w = static_cast<double>(std::max(1, cs.get_width()));
    const double vp_h = static_cast<double>(std::max(1, cs.get_height()));

    double width_mm = 0.0, height_mm = 0.0;
    Slic3r::ImagePaint::screen_overlay_to_plane_mm(
        static_cast<double>(ow) / vp_w,
        static_cast<double>(oh) / vp_h,
        cam.get_near_width(), cam.get_near_height(), cam.get_near_z(),
        dist, cam.get_type() == Camera::EType::Perspective,
        width_mm, height_mm);
    if (width_mm < 1e-6 || height_mm < 1e-6) {
        m_status_text = _u8L("Overlay is too small to project.");
        return std::nullopt;
    }

    Slic3r::ImagePaint::PlanarProjectionSettings s;
    s.frame = *frame;
    s.width_mm  = width_mm;
    s.height_mm = height_mm;
    s.rotation_radians = static_cast<double>(m_rotation_deg) * PI / 180.0;
    s.mirror_u = m_mirror_u;
    s.front_face_cosine_threshold = 0.05;
    s.minimum_coverage = 0.05;
    return s;
}

// ---------------------------------------------------------------------------
// Apply — builds a MeshRemeshJob and hands it to the worker. Real geometry
// commit happens in MeshRemeshJob::finalize() on the UI thread.
// ---------------------------------------------------------------------------

void GLGizmoMeshGraffiti::apply()
{
    if (m_job_running)
        return;

    const std::string path(m_image_path);
    if (path.empty()) {
        m_status_text = _u8L("Select an image first.");
        return;
    }

    const Selection& sel   = m_parent.get_selection();
    const Model&     model = *sel.get_model();

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

    const TriangleMesh& mesh = mv->mesh();
    if (mesh.its.indices.empty()) {
        m_status_text = _u8L("Selected volume has no faces.");
        return;
    }

    MeshRemeshJob::Input input;
    input.image_path = path;

    input.vertices.reserve(mesh.its.vertices.size());
    for (const auto& v : mesh.its.vertices)
        input.vertices.push_back(v);

    input.indices.reserve(mesh.its.indices.size());
    for (const auto& t : mesh.its.indices)
        input.indices.push_back(t.cast<int32_t>());

    auto proj = build_camera_facing_projection(*glvol);
    if (!proj)
        return;
    input.projection = *proj;
    input.grid_resolution = m_grid_resolution;

    const auto& extruder_colors = wxGetApp().plater()->get_extruder_colors_from_plater_config();
    for (std::size_t i = 0; i < extruder_colors.size(); ++i) {
        Slic3r::ImagePaint::FilamentColor fc;
        fc.project_index = static_cast<Slic3r::ImagePaint::FilamentIndex>(i);
        fc.name = "Extruder " + std::to_string(i + 1);
        const std::string& hex = extruder_colors[i];
        if (hex.size() == 7 && hex[0] == '#') {
            unsigned r = 0, g = 0, b = 0;
            sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
            fc.display_rgb = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
        }
        input.filaments.push_back(std::move(fc));
    }
    if (input.filaments.empty()) {
        m_status_text = _u8L("No filaments configured.");
        return;
    }

    input.quantization.target_colors = static_cast<std::uint32_t>(std::max(1, std::min(m_target_colors, 16)));
    input.volume_id            = mv->id();
    input.expected_fingerprint = Slic3r::ImagePaint::fingerprint(mesh);

    m_cancel->store(true);
    if (m_worker)
        m_worker->cancel_all();
    m_cancel = std::make_shared<std::atomic<bool>>(false);

    wxWindow* parent_wnd = wxGetApp().plater();
    if (!m_worker)
        m_worker = std::make_unique<PlaterWorker<BoostThreadWorker>>(
            parent_wnd, nullptr, "MeshRemeshWorker");

    auto job = std::make_shared<MeshRemeshJob>(std::move(input), m_cancel, parent_wnd);

    m_job_running = true;
    m_status_text = _u8L("Remeshing...");
    m_worker->push(std::move(job));
}

// ---------------------------------------------------------------------------
// ImGui panel + screen-locked overlay.
// ---------------------------------------------------------------------------

void GLGizmoMeshGraffiti::on_render_input_window(float x, float y, float /*bottom_limit*/)
{
    if (m_job_running && m_worker && m_worker->is_idle()) {
        m_job_running = false;
        m_status_text = _u8L("Done.");
    }

    render_screen_overlay();

    const float unit = m_imgui->scaled(1.0f);

    m_imgui->push_common_window_style(m_parent.get_scale());
    m_imgui->begin(on_get_name(),
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse);

    m_imgui->text(_L("Image"));
    ImGui::SameLine(unit * 8.f);
    ImGui::PushItemWidth(unit * 18.f);
    ImGui::InputText("##img_path", m_image_path, sizeof(m_image_path));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (m_imgui->button(_L("Browse"))) {
        wxFileDialog dlg(wxGetApp().plater(),
                         _L("Open image"), "", "",
                         "Image files (*.jpg;*.jpeg;*.png;*.bmp)|*.jpg;*.jpeg;*.png;*.bmp",
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK) {
            const std::string sel = dlg.GetPath().ToUTF8().data();
            std::strncpy(m_image_path, sel.c_str(), sizeof(m_image_path) - 1);
            m_image_path[sizeof(m_image_path) - 1] = '\0';
            m_overlay.reset();
            m_overlay_path.clear();
            if (!ensure_overlay_texture())
                m_status_text = _u8L("Could not load image.");
        }
    }

    if (m_image_px_w > 0 && m_image_px_h > 0) {
        ImGui::SameLine();
        m_imgui->text_colored(ImVec4(0.55f, 0.55f, 0.55f, 1.f),
                              GUI::format("%1%x%2%", m_image_px_w, m_image_px_h));
    }

    ImGui::Separator();

    // Camera shortcuts only — they do not feed Apply.
    m_imgui->text(_L("Look"));
    static const char* view_labels[6] = {"Front", "Back", "Left", "Right", "Top", "Bottom"};
    static const char* view_camera_directions[6] = {"front", "rear", "left", "right", "top", "bottom"};
    for (int i = 0; i < 6; ++i) {
        if (i > 0) ImGui::SameLine();
        const bool selected = (m_look_preset == i);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (m_imgui->button(_(view_labels[i]))) {
            m_look_preset = i;
            m_parent.select_view(view_camera_directions[i]);
        }
        if (selected) ImGui::PopStyleColor();
    }

    m_imgui->text(_L("Size"));
    ImGui::SameLine(unit * 8.f);
    ImGui::PushItemWidth(unit * 14.f);
    ImGui::SliderFloat("##size", &m_size_percent, 1.f, kSizeSliderMax, "%.0f%%");
    ImGui::PopItemWidth();

    m_imgui->text(_L("Rotate"));
    ImGui::SameLine(unit * 8.f);
    ImGui::PushItemWidth(unit * 14.f);
    ImGui::SliderFloat("##rotate", &m_rotation_deg, 0.f, 360.f, "%.0f°");
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::Checkbox(_u8L("Flip").c_str(), &m_mirror_u);

    int max_colors = 16;
    if (auto* plater = wxGetApp().plater()) {
        const auto n = plater->get_extruder_colors_from_plater_config().size();
        if (n > 0)
            max_colors = static_cast<int>(n);
    }

    m_imgui->text(_L("Colors"));
    ImGui::SameLine(unit * 8.f);
    ImGui::PushItemWidth(unit * 5.f);
    ImGui::InputInt("##colors", &m_target_colors, 1, 1);
    ImGui::PopItemWidth();
    m_target_colors = std::max(1, std::min(m_target_colors, max_colors));

    m_imgui->text(_L("Resolution"));
    ImGui::SameLine(unit * 8.f);
    ImGui::PushItemWidth(unit * 14.f);
    ImGui::SliderInt("##resolution", &m_grid_resolution, 4, 24, "%d");
    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("How finely each remeshed face's color boundary is traced. "
                            "Higher = triangle edges follow the image detail more closely, "
                            "but more triangles."),
                         ImGui::GetFontSize() * 25.f);
    ImGui::PopItemWidth();
    m_grid_resolution = std::max(4, std::min(m_grid_resolution, 24));

    ImGui::Separator();

    const bool have_image = m_image_path[0] != '\0';
    m_imgui->disabled_begin(m_job_running || !have_image);
    if (m_imgui->button(_L("Apply")))
        apply();
    m_imgui->disabled_end();

    if (m_job_running) {
        ImGui::SameLine();
        if (m_imgui->button(_L("Cancel")))
            cancel_job();
    }

    if (!have_image && !m_job_running) {
        m_imgui->text_colored(ImVec4(0.5f, 0.5f, 0.5f, 1.f),
                              _u8L("Load an image. Orbit the model under it, then Apply."));
    }
    if (!m_status_text.empty()) {
        m_imgui->text_colored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), m_status_text);
    }

    m_imgui->text_colored(ImVec4(0.5f, 0.5f, 0.5f, 1.f),
                          _u8L("Image stays on screen. Move the model (or the camera) so the surface sits behind it. Replaces real geometry — Ctrl+Z undoes."));

    m_imgui->end();
    m_imgui->pop_common_window_style();
}

} // namespace Slic3r::GUI
