#include "ImagePaintJob.hpp"

#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ImagePaint/TopologyFingerprint.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"         // show_error
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"  // get_model_volume

#include <cassert>

namespace Slic3r { namespace GUI {

ImagePaintJob::ImagePaintJob(Input                              input,
                              std::shared_ptr<std::atomic<bool>> cancel,
                              wxWindow*                          parent_window)
    : m_input(std::move(input))
    , m_cancel(std::move(cancel))
    , m_parent_window(parent_window)
{}

void ImagePaintJob::process(Ctl& ctl)
{
    auto cancel_fn = [this, &ctl]() -> bool {
        return m_cancel->load() || ctl.was_canceled();
    };

    auto result = Slic3r::ImagePaint::run_image_paint(m_input.request, cancel_fn);
    if (!result) {
        const std::string msg = result.error().user_message;
        ctl.call_on_main_thread([this, msg]() {
            show_error(m_parent_window, wxString::FromUTF8(msg));
        });
        return;
    }

    m_plan = std::move(*result);
}

void ImagePaintJob::finalize(bool canceled, std::exception_ptr& eptr)
{
    if (canceled || !m_plan)
        return;

    auto* plater = wxGetApp().plater();
    if (!plater) return;

    // Find the target volume by ObjectID.
    ModelVolume* vol = get_model_volume(m_input.volume_id,
                                        plater->model().objects);
    if (!vol) {
        show_error(m_parent_window, _L("Image Paint: target object was deleted — apply aborted."));
        return;
    }

    // Reject stale plans (mesh was remeshed or modified between compute and apply).
    const auto live_fp = Slic3r::ImagePaint::fingerprint(vol->mesh());
    if (live_fp != m_plan->fingerprint) {
        show_error(m_parent_window,
                   _L("Image Paint: mesh changed since computing — apply aborted."));
        return;
    }

    plater->take_snapshot(_u8L("Image Paint"));

    // Deserialize existing paint first so faces outside the image footprint are preserved.
    TriangleSelector selector(vol->mesh());
    selector.deserialize(vol->mmu_segmentation_facets.get_data(),
                         /*needs_reset=*/true,
                         EnforcerBlockerType::ExtruderMax);

    // Overlay plan states — only non-None entries (faces the image touched).
    const auto& states = m_plan->states;
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (states[i] != Slic3r::ImagePaint::kStateNone)
            selector.set_facet(static_cast<int>(i),
                               static_cast<EnforcerBlockerType>(states[i]));
    }
    vol->mmu_segmentation_facets.set(selector);

    plater->update();
}

}} // namespace Slic3r::GUI
