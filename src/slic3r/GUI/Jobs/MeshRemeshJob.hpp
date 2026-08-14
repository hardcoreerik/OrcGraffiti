#pragma once

#include "libslic3r/ImagePaint/MeshRemesh.hpp"
#include "libslic3r/ImagePaint/TopologyFingerprint.hpp"
#include "libslic3r/ObjectID.hpp"

#include "slic3r/GUI/Jobs/Job.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <string>

namespace Slic3r { namespace GUI {

// Background job that runs remesh_by_color_boundary() + validate_baked_mesh()
// on a worker thread, then commits the result by REPLACING the volume's real
// mesh geometry — unlike ImagePaintJob, which only writes per-face paint
// state onto the existing mesh.
//
// Thread ownership: process() runs on the worker thread (read-only inputs,
//                   no live ModelVolume* touched).
//                   finalize() runs on the main/UI thread and follows the
//                   same commit pattern Simplify/MeshBoolean/Cut already
//                   use for real mesh replacement (see
//                   docs/OrcGraffiti/MeshGraffiti_Bake_Plan.md): snapshot,
//                   save_painting(), set_mesh(), write the new per-triangle
//                   colors directly (already known — no remap needed),
//                   restore_painting() to carry over any OTHER paint layers
//                   (seam/support/fuzzy, or MMU paint outside the remeshed
//                   region), set_new_unique_id(), hull/bbox invalidation,
//                   changed_mesh().
class MeshRemeshJob : public Job
{
public:
    struct Input {
        std::string                                   image_path;
        Slic3r::ImagePaint::ImageDecodeLimits         decode_limits;
        std::vector<Vec3f>                             vertices;
        std::vector<Vec3i32>                           indices;
        Slic3r::ImagePaint::PlanarProjectionSettings   projection;
        std::vector<Slic3r::ImagePaint::FilamentColor> filaments;
        Slic3r::ImagePaint::QuantizationSettings       quantization;
        int                                              grid_resolution = 12;

        Slic3r::ObjectID                        volume_id;
        Slic3r::ImagePaint::TopologyFingerprint expected_fingerprint;
    };

    explicit MeshRemeshJob(Input input,
                           std::shared_ptr<std::atomic<bool>> cancel,
                           wxWindow* parent_window);

    void process(Ctl& ctl) override;
    void finalize(bool canceled, std::exception_ptr& eptr) override;

private:
    Input m_input;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    wxWindow* m_parent_window;
    std::optional<Slic3r::ImagePaint::BakedMesh> m_baked;
    std::string m_error_message;
};

}} // namespace Slic3r::GUI
