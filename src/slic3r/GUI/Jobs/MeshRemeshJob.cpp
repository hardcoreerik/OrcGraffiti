#include "MeshRemeshJob.hpp"

#include "libslic3r/ImagePaint/ImageDecoder.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"         // show_error
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"  // get_model_volume

#include <cassert>

namespace Slic3r { namespace GUI {

MeshRemeshJob::MeshRemeshJob(Input                              input,
                             std::shared_ptr<std::atomic<bool>> cancel,
                             wxWindow*                          parent_window)
    : m_input(std::move(input))
    , m_cancel(std::move(cancel))
    , m_parent_window(parent_window)
{}

void MeshRemeshJob::process(Ctl& ctl)
{
    auto cancel_fn = [this, &ctl]() -> bool {
        return m_cancel->load() || ctl.was_canceled();
    };

    auto image = Slic3r::ImagePaint::decode_image(m_input.image_path, m_input.decode_limits);
    if (!image) {
        m_error_message = image.error().user_message;
        return;
    }

    auto result = Slic3r::ImagePaint::remesh_by_color_boundary(
        m_input.vertices, m_input.indices, *image, m_input.projection,
        m_input.filaments, m_input.quantization, m_input.grid_resolution, cancel_fn);
    if (!result) {
        m_error_message = result.error().user_message;
        return;
    }

    auto validated = Slic3r::ImagePaint::validate_baked_mesh(std::move(*result));
    if (!validated) {
        m_error_message = validated.error().user_message;
        return;
    }

    m_baked = std::move(*validated);
}

void MeshRemeshJob::finalize(bool canceled, std::exception_ptr& eptr)
{
    if (canceled)
        return;

    if (!m_baked) {
        if (!m_error_message.empty())
            show_error(m_parent_window, wxString::FromUTF8(m_error_message));
        return;
    }

    auto* plater = wxGetApp().plater();
    if (!plater) return;

    ModelVolume* vol = get_model_volume(m_input.volume_id, plater->model().objects);
    if (!vol) {
        show_error(m_parent_window, _L("Mesh Graffiti: target object was deleted — remesh aborted."));
        return;
    }

    // Reject stale results (mesh was modified between compute and apply) —
    // same guard ImagePaintJob uses before writing paint data.
    const auto live_fp = Slic3r::ImagePaint::fingerprint(vol->mesh());
    if (live_fp != m_input.expected_fingerprint) {
        show_error(m_parent_window, _L("Mesh Graffiti: mesh changed since computing — remesh aborted."));
        return;
    }

    ModelObject* obj = vol->get_object();
    int object_idx = -1;
    const auto& objects = plater->model().objects;
    for (size_t i = 0; i < objects.size(); ++i)
        if (objects[i] == obj) { object_idx = static_cast<int>(i); break; }
    if (object_idx < 0) {
        show_error(m_parent_window, _L("Mesh Graffiti: target object was deleted — remesh aborted."));
        return;
    }

    plater->take_snapshot(_u8L("Mesh Graffiti (Remesh)"));

    // Preserve seam/support/fuzzy (and any MMU paint outside the remeshed
    // region) across the geometry swap — same pattern
    // Simplify/MeshBoolean/Cut already use for real mesh replacement.
    auto saved_painting = vol->save_painting();

    TriangleMesh new_mesh(m_baked->mesh);
    vol->set_mesh(std::move(new_mesh));

    // Write the new mesh's MMU colors directly — they're already known
    // exactly (this bake computed them), so there's no need for
    // restore_painting()'s spatial remap heuristic here; that's reserved
    // for the OTHER paint layers below, which this job never touched.
    TriangleSelector selector(vol->mesh());
    for (std::size_t i = 0; i < m_baked->triangle_states.size(); ++i) {
        const auto state = m_baked->triangle_states[i];
        if (state != Slic3r::ImagePaint::kStateNone)
            selector.set_facet(static_cast<int>(i), static_cast<EnforcerBlockerType>(state));
    }
    vol->mmu_segmentation_facets.set(selector);

    // Merge in remapped seam/support/fuzzy (and any untouched-region MMU
    // paint) from the old mesh, keeping what was just set above.
    vol->restore_painting(saved_painting, /*keep_existing_paint=*/true);

    vol->calculate_convex_hull();
    vol->invalidate_convex_hull_2d();
    vol->set_new_unique_id();
    obj->invalidate_bounding_box();
    obj->ensure_on_bed();

    plater->changed_mesh(object_idx);
}

}} // namespace Slic3r::GUI
