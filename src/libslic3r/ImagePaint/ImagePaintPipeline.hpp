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
