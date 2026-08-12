#pragma once

#include "ImagePaintTypes.hpp"
#include "ImagePaintErrors.hpp"
#include "Projection.hpp"
#include "FaceSampler.hpp"
#include "ColorQuantizer.hpp"
#include "RegionCleaner.hpp"
#include "PaintStateMerge.hpp"
#include "TopologyFingerprint.hpp"

#include "libslic3r/Point.hpp"

#include "ImagePaintCompat.hpp"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::ImagePaint {

// Complete input for a headless image-paint pass.
// The mesh snapshot (vertices + indices) must be provided by the caller;
// no live ModelVolume* is touched by this module.
struct ImagePaintRequest {
    // Image path — used only by the path-based overload of run_image_paint.
    std::string       image_path;
    ImageDecodeLimits decode_limits;

    // Projection onto the mesh surface. Defaults to a default-constructed
    // PlanarProjectionSettings (the variant's first alternative) so existing
    // callers that never touch curved projection see unchanged behavior.
    ProjectionSettings projection;

    // Colour palette from the active project.
    std::vector<FilamentColor> filaments;
    QuantizationSettings       quantization;

    // Post-processing.
    RegionCleanupSettings cleanup;
    SamplingQuality       quality       = SamplingQuality::Gaussian7;
    MergePolicy           merge_policy  = MergePolicy::OverwriteInsideMask;

    // Opt-in fine-detail subdivision (0 = disabled, matches all pre-existing
    // callers/tests byte-for-byte). When > 0, every original face this pass
    // paints is additionally subdivided — via TriangleSelector's own virtual
    // split tree, never the underlying TriangleMesh — down to this edge
    // length (mm) and each resulting leaf is colored independently, instead
    // of the whole original face getting one averaged color. This is what
    // lets Image Paint reproduce per-color-patch detail finer than the
    // source mesh's own triangle density without remeshing — see
    // docs/OrcGraffiti/AI_STATUS.md, "GUI gizmo rework" section.
    double detail_edge_length_mm = 0.0;

    // Immutable mesh snapshot (caller copies from TriangleMesh::its).
    std::vector<Vec3f>   vertices;
    std::vector<Vec3i32> indices;

    // Current per-face paint state (from mmu_segmentation_facets).
    // May be empty (treated as all kStateNone).
    std::vector<SelectorState> existing_states;
};

// The result of a completed paint pass, ready to write to mmu_segmentation_facets.
struct FacePaintPlan {
    // Merged per-face selector states (one per triangle).
    std::vector<SelectorState> states;

    // Cluster-to-filament matching result for the UI to display.
    std::vector<ClusterMatch> matches;

    // Counters for the UI status bar.
    PaintDiagnostics diagnostics;

    // Mesh fingerprint at plan time — must match target before Apply is called.
    TopologyFingerprint fingerprint;

    // Fine-detail leaves, present only when the request set detail_edge_length_mm
    // > 0. One entry per original face this pass painted; each face's leaf
    // states are in the exact order TriangleSelector::collect_leaves() visits
    // them after TriangleSelector::subdivide_facet_uniform(face, detail_edge_length_mm)
    // on that same (unsplit) face — deterministic, so Apply can replay the
    // identical split on the live mesh and zip leaf-for-leaf without needing
    // the source image again. `states` above still carries a whole-face
    // fallback value for every touched face, for callers that don't apply
    // fine detail (e.g. the CLI's --dry-run diagnostics).
    std::vector<std::pair<FaceIndex, std::vector<SelectorState>>> detail_leaf_states;
};

// Run the full image-paint pipeline from a file path.
Expected<FacePaintPlan, ImagePaintError>
run_image_paint(const ImagePaintRequest&     request,
                const std::function<bool()>& cancel = {});

// Same pipeline but with a pre-decoded image (for preview and testing).
Expected<FacePaintPlan, ImagePaintError>
run_image_paint(const ImagePaintRequest&     request,
                const DecodedImage&          image,
                const std::function<bool()>& cancel = {});

} // namespace Slic3r::ImagePaint
