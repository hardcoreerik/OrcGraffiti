#include "GLGizmoMeshGraffiti.hpp"

#include "libslic3r/libslic3r.h"  // PI (global namespace, defined before Slic3r{})
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/ImagePaint/TopologyFingerprint.hpp"
#include "libslic3r/ImagePaint/Projection.hpp"

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

#include <cassert>
#include <cstring>
#include <vector>

namespace Slic3r::GUI {

// Same rebasing/range as GLGizmoImagePainter's Size slider — see that
// gizmo's kSizeReferenceScale comment. Kept identical on purpose so the two
// tools feel like the same workflow.
static constexpr float kSizeSliderMax = 300.f;

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

// ---------------------------------------------------------------------------
// View preset (Top/Back/Front/Left/Right/Bottom) + Size percent + Rotation.
// Identical logic/constants to GLGizmoImagePainter::build_view_preset_projection
// — see that function's comments for why Size is rebased and the coverage
// threshold is 0.05.
// ---------------------------------------------------------------------------

std::optional<Slic3r::ImagePaint::PlanarProjectionSettings>
GLGizmoMeshGraffiti::build_view_preset_projection(const std::vector<Vec3f>& vertices)
{
    if (m_view_preset < 0) {
        m_status_text = _u8L("Pick a view first.");
        return std::nullopt;
    }

    const auto [look, up] = Slic3r::ImagePaint::view_preset_vectors(
        static_cast<Slic3r::ImagePaint::ViewPreset>(m_view_preset));

    Slic3r::ImagePaint::Span<const Vec3f> span(vertices.data(), vertices.size());
    auto fitted = Slic3r::ImagePaint::fit_planar_projection(
        span, look, up, image_aspect_ratio(), /*margin=*/1.02);
    if (!fitted) {
        m_status_text = fitted.error().user_message;
        return std::nullopt;
    }

    constexpr double kSizeReferenceScale = 0.30;
    const double scale = std::clamp(static_cast<double>(m_size_percent), 1.0, static_cast<double>(kSizeSliderMax)) / 100.0
                        * kSizeReferenceScale;
    fitted->width_mm  *= scale;
    fitted->height_mm *= scale;
    fitted->rotation_radians = static_cast<double>(m_rotation_deg) * PI / 180.0;
    fitted->mirror_u = m_mirror_u;
    fitted->front_face_cosine_threshold = 0.05;
    fitted->minimum_coverage = 0.05;

    return *fitted;
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

    auto proj = build_view_preset_projection(input.vertices);
    if (!proj)
        return;
    input.projection = *proj;
    input.grid_resolution = m_grid_resolution;

    // Filaments from the active project — same source of truth
    // GLGizmoImagePainter's submit_paint_request uses.
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
// ImGui panel — deliberately the same shape as GLGizmoImagePainter's, minus
// the legacy camera-facing "Advanced" section (see header comment).
// ---------------------------------------------------------------------------

void GLGizmoMeshGraffiti::on_render_input_window(float x, float y, float /*bottom_limit*/)
{
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
        wxFileDialog dlg(wxGetApp().plater(),
                         _L("Open image"), "", "",
                         "Image files (*.jpg;*.jpeg;*.png;*.bmp)|*.jpg;*.jpeg;*.png;*.bmp",
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK) {
            const std::string sel = dlg.GetPath().ToUTF8().data();
            std::strncpy(m_image_path, sel.c_str(), sizeof(m_image_path) - 1);
            m_image_path[sizeof(m_image_path) - 1] = '\0';

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

    // --- View preset buttons — turns the viewport camera to match, same as
    // GLGizmoImagePainter's, so the user is looking at what they just told
    // the tool to remesh.
    m_imgui->text(_L("View"));
    static const char* view_labels[6] = {"Front", "Back", "Left", "Right", "Top", "Bottom"};
    static const char* view_camera_directions[6] = {"front", "rear", "left", "right", "top", "bottom"};
    for (int i = 0; i < 6; ++i) {
        if (i > 0) ImGui::SameLine();
        const bool selected = (m_view_preset == i);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (m_imgui->button(_(view_labels[i]))) {
            m_view_preset = i;
            m_parent.select_view(view_camera_directions[i]);
        }
        if (selected) ImGui::PopStyleColor();
    }

    // --- Size / Rotation / Flip ---
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

    // --- Colors ---
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

    // --- Resolution ---
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

    // --- Apply / Cancel ---
    m_imgui->disabled_begin(m_job_running || m_view_preset < 0);
    if (m_imgui->button(_L("Apply")))
        apply();
    m_imgui->disabled_end();

    if (m_job_running) {
        ImGui::SameLine();
        if (m_imgui->button(_L("Cancel")))
            cancel_job();
    }

    if (m_view_preset < 0 && !m_job_running) {
        m_imgui->text_colored(ImVec4(0.5f, 0.5f, 0.5f, 1.f), _u8L("Pick a view above to enable Apply."));
    }
    if (!m_status_text.empty()) {
        m_imgui->text_colored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), m_status_text);
    }

    m_imgui->text_colored(ImVec4(0.5f, 0.5f, 0.5f, 1.f),
                          _u8L("Replaces the model's real geometry — Ctrl+Z undoes it like any other edit."));

    m_imgui->end();
    m_imgui->pop_common_window_style();
}

} // namespace Slic3r::GUI
