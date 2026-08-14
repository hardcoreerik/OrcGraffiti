#pragma once

#include "libslic3r/ImagePaint/ImagePaintPipeline.hpp"
#include "libslic3r/ObjectID.hpp"

#include "slic3r/GUI/Jobs/Job.hpp"

#include <atomic>
#include <memory>
#include <optional>

namespace Slic3r { namespace GUI {

// Background job that runs the image-paint pipeline on a worker thread
// and applies the resulting FacePaintPlan to mmu_segmentation_facets on finalize.
//
// Thread ownership: process() runs on the worker thread (read-only inputs).
//                   finalize() runs on the main/UI thread (writes to model).
class ImagePaintJob : public Job
{
public:
    struct Input {
        Slic3r::ImagePaint::ImagePaintRequest    request;
        Slic3r::ObjectID                          volume_id;
        Slic3r::ImagePaint::TopologyFingerprint   expected_fingerprint;
    };

    explicit ImagePaintJob(Input input,
                           std::shared_ptr<std::atomic<bool>> cancel,
                           wxWindow* parent_window);

    void process(Ctl& ctl) override;
    void finalize(bool canceled, std::exception_ptr& eptr) override;

private:
    Input m_input;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    wxWindow* m_parent_window;
    std::optional<Slic3r::ImagePaint::FacePaintPlan> m_plan;
};

}} // namespace Slic3r::GUI
