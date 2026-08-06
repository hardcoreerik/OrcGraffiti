#include "GLGizmoImagePainter.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/ImagePaint/TopologyFingerprint.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Jobs/BoostThreadWorker.hpp"
#include "slic3r/GUI/Jobs/PlaterWorker.hpp"
#include "slic3r/GUI/Jobs/ImagePaintJob.hpp"

#include <imgui/imgui.h>
#include <wx/filedlg.h>
#include <wx/string.h>

#include <cassert>
#include <cstring>

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
    }
}

void GLGizmoImagePainter::cancel_job()
{
    m_cancel->store(true);
    if (m_worker)
        m_worker->cancel_all();
    m_cancel = std::make_shared<std::atomic<bool>>(false);
}

// ---------------------------------------------------------------------------
// Apply — build ImagePaintRequest and submit ImagePaintJob.
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

    // Take an immutable mesh snapshot (copy on worker thread entry).
    const TriangleMesh& mesh = mv->mesh();
    if (mesh.its.indices.empty()) {
        m_status_text = _u8L("Selected volume has no faces.");
        return;
    }

    // Build the request.
    Slic3r::ImagePaint::ImagePaintRequest req;
    req.image_path = path;

    // Copy immutable mesh snapshot.
    req.vertices.reserve(mesh.its.vertices.size());
    for (const auto& v : mesh.its.vertices)
        req.vertices.push_back(v);  // stl_vertex == Vec3f

    req.indices.reserve(mesh.its.indices.size());
    for (const auto& t : mesh.its.indices)
        req.indices.push_back(t.cast<int32_t>());  // stl_triangle_vertex_indices: int→int32_t

    // Projection — use camera direction.
    const Camera& cam        = m_parent.get_camera();
    const Vec3d   look_dir   = cam.get_dir_forward();
    const Vec3d   up_dir     = cam.get_dir_up();
    const BoundingBoxf3 bbox = mesh.bounding_box();
    const Vec3d   origin     = bbox.center() - look_dir * (bbox.size().norm() * 0.5 + 5.0);

    auto frame_result = Slic3r::ImagePaint::make_projector_frame(look_dir, up_dir, origin);
    if (!frame_result) {
        m_status_text = _u8L("Cannot build projector frame (degenerate view direction).");
        return;
    }
    req.projection.frame      = *frame_result;
    req.projection.width_mm   = m_width_mm;
    req.projection.height_mm  = m_height_mm;
    req.projection.front_face_cosine_threshold = 0.1;

    // Filaments from the active project.
    const auto& extruder_colors = wxGetApp().plater()->get_extruder_colors_from_plater_config();
    for (std::size_t i = 0; i < extruder_colors.size(); ++i) {
        Slic3r::ImagePaint::FilamentColor fc;
        fc.project_index = static_cast<Slic3r::ImagePaint::FilamentIndex>(i);
        fc.name = "Extruder " + std::to_string(i + 1);
        // Parse hex color string "#RRGGBB" from extruder_colors[i].
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
        m_status_text = _u8L("No filaments configured.");
        return;
    }

    req.quantization.target_colors = static_cast<std::uint32_t>(
        std::max(1, std::min(m_target_colors, 16)));
    req.quality        = Slic3r::ImagePaint::SamplingQuality::Gaussian7;
    req.merge_policy   = Slic3r::ImagePaint::MergePolicy::OverwriteInsideMask;
    req.cleanup.enabled = true;

    // Copy existing paint state.
    TriangleSelector ts(mesh);
    // (existing states start empty — future Phase 6 will read live state here)

    // Build the job.
    ImagePaintJob::Input job_input;
    job_input.request             = std::move(req);
    job_input.volume_id           = mv->id();
    job_input.expected_fingerprint = Slic3r::ImagePaint::fingerprint(mesh);

    cancel_job();

    // Lazily create the worker.
    wxWindow* parent_wnd = wxGetApp().plater();
    if (!m_worker)
        m_worker = std::make_unique<PlaterWorker<BoostThreadWorker>>(
            parent_wnd, nullptr, "ImagePaintWorker");

    auto job = std::make_shared<ImagePaintJob>(
        std::move(job_input), m_cancel, parent_wnd);

    m_job_running = true;
    m_status_text = _u8L("Computing...");
    m_worker->push(std::move(job));
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
        wxFileDialog dlg(m_parent.wxglcanvas_as_wxwindow(),
                         _L("Open image"), "", "",
                         "Image files (*.jpg;*.jpeg;*.png;*.bmp)|*.jpg;*.jpeg;*.png;*.bmp",
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK) {
            const std::string sel = dlg.GetPath().ToUTF8().data();
            std::strncpy(m_image_path, sel.c_str(), sizeof(m_image_path) - 1);
            m_image_path[sizeof(m_image_path) - 1] = '\0';
        }
    }

    ImGui::Separator();

    // --- Projection size ---
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

    // --- Colors ---
    m_imgui->text(_L("Colors"));
    ImGui::SameLine(unit * 8.f);
    ImGui::PushItemWidth(unit * 5.f);
    ImGui::InputInt("##colors", &m_target_colors, 1, 1);
    ImGui::PopItemWidth();
    m_target_colors = std::max(1, std::min(m_target_colors, 16));

    ImGui::Separator();

    // --- Apply / Cancel ---
    m_imgui->disabled_begin(m_job_running);
    if (m_imgui->button(_L("Apply")))
        apply();
    m_imgui->disabled_end();

    if (m_job_running) {
        ImGui::SameLine();
        if (m_imgui->button(_L("Cancel")))
            cancel_job();
    }

    // --- Status ---
    if (!m_status_text.empty()) {
        ImGui::Separator();
        m_imgui->text_colored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), m_status_text);
    }

    m_imgui->end();
    m_imgui->pop_common_window_style();
}

} // namespace Slic3r::GUI
