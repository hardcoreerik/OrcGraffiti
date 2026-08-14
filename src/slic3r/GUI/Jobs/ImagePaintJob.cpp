#include "ImagePaintJob.hpp"

#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ImagePaint/TopologyFingerprint.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"         // show_error
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"  // get_model_volume

#include <algorithm>
#include <cassert>
#include <vector>

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

    // Faces with fine-detail leaves get their per-leaf colors below instead
    // of a single flat set_facet() call.
    std::vector<bool> has_detail(m_plan->states.size(), false);
    for (const auto& [face_idx, leaf_states] : m_plan->detail_leaf_states)
        if (face_idx < has_detail.size())
            has_detail[face_idx] = true;

    // Overlay plan states — only non-None entries (faces the image touched).
    const auto& states = m_plan->states;
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (states[i] != Slic3r::ImagePaint::kStateNone && !has_detail[i])
            selector.set_facet(static_cast<int>(i),
                               static_cast<EnforcerBlockerType>(states[i]));
    }

    // Replay fine-detail subdivision: the split itself is deterministic (same
    // face + same edge length always produces the same tree — see
    // TriangleSelector::subdivide_facet_uniform's contract), so re-running it
    // here on the live mesh reproduces exactly the leaves the worker thread
    // classified against the source image, without needing the image again.
    for (const auto& [face_idx, leaf_states] : m_plan->detail_leaf_states) {
        selector.set_facet(static_cast<int>(face_idx), EnforcerBlockerType::NONE);
        selector.subdivide_facet_uniform(static_cast<int>(face_idx),
                                         static_cast<float>(m_input.request.detail_edge_length_mm));
        const auto leaves = selector.collect_leaves(static_cast<int>(face_idx));
        assert(leaves.size() == leaf_states.size());
        const std::size_t n = std::min(leaves.size(), leaf_states.size());
        for (std::size_t k = 0; k < n; ++k)
            selector.set_leaf_state(leaves[k].leaf_index,
                                    static_cast<EnforcerBlockerType>(leaf_states[k]));
    }

    vol->mmu_segmentation_facets.set(selector);

    plater->update();
}

}} // namespace Slic3r::GUI
