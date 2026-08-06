---
title: OrcGraffiti Architecture
status: Proposed implementation architecture
last_researched: 2026-08-05
note: Filename intentionally preserves the requested spelling: Architechture.md
---

# OrcGraffiti Architecture

## 1. Architectural Goal

Build Image Paint as a native OrcaSlicer feature with a GUI-independent core pipeline. The core receives immutable mesh/image/settings snapshots and returns an immutable face-paint plan. A UI-thread application service validates and writes the plan into Orca's existing MMU facet data.

```text
GUI / Gizmo
    -> Controller and immutable snapshot
        -> libslic3r ImagePaint pipeline
            -> FacePaintPlan
        -> UI-thread validation and apply
            -> TriangleSelector
            -> ModelVolume::mmu_segmentation_facets
            -> normal Orca 3MF persistence
```

A narrow Python `orca.host.paint` bridge may be added later.

## 2. Layer Boundaries

### GUI Layer

Responsibilities:

- selection;
- file dialog;
- placement controls;
- preview;
- palette mapping UI;
- warnings;
- Apply/Cancel;
- main-thread lifecycle.

Must not contain image quantization or color science.

### Application Service

Responsibilities:

- snapshot live target;
- assign request generation;
- start/cancel worker;
- validate result;
- resolve stable IDs;
- undo capture;
- apply states;
- dirty/slice/render refresh.

### Core Pipeline

Responsibilities:

- image decode;
- coordinate projection;
- face sampling;
- color conversion;
- clustering;
- filament matching;
- adjacency;
- cleanup;
- diagnostics;
- topology fingerprint.

Must not include wxWidgets, ImGui, OpenGL, Plater, or live model pointers.

### Existing Orca Model Layer

Use:

- TriangleMesh;
- TriangleSelector;
- EnforcerBlockerType;
- ModelVolume;
- mmu_segmentation_facets;
- existing 3MF serializer/parser.

## 3. Proposed Source Layout

```text
src/libslic3r/ImagePaint/
    ImagePaintTypes.hpp
    ImagePaintErrors.hpp
    ImageDecoder.hpp
    ImageDecoder.cpp
    ColorSpace.hpp
    ColorSpace.cpp
    ColorDifference.hpp
    ColorDifference.cpp
    Projection.hpp
    Projection.cpp
    FaceSampler.hpp
    FaceSampler.cpp
    FaceAdjacency.hpp
    FaceAdjacency.cpp
    ColorQuantizer.hpp
    ColorQuantizer.cpp
    FilamentMatcher.hpp
    FilamentMatcher.cpp
    RegionCleaner.hpp
    RegionCleaner.cpp
    PaintDiagnostics.hpp
    PaintDiagnostics.cpp
    TopologyFingerprint.hpp
    TopologyFingerprint.cpp
    ImagePaintPipeline.hpp
    ImagePaintPipeline.cpp
    PaintStateMerge.hpp
    PaintStateMerge.cpp

src/slic3r/GUI/ImagePaint/
    ImagePaintController.hpp
    ImagePaintController.cpp
    ImagePaintJob.hpp
    ImagePaintJob.cpp
    ImagePaintUiState.hpp
    ImagePaintPreview.hpp
    ImagePaintPreview.cpp

src/slic3r/GUI/Gizmos/
    GLGizmoImagePainter.hpp
    GLGizmoImagePainter.cpp

tests/libslic3r/
    test_image_paint_decoder.cpp
    test_image_paint_projection.cpp
    test_image_paint_sampling.cpp
    test_image_paint_color.cpp
    test_image_paint_regions.cpp
    test_image_paint_fingerprint.cpp
    test_image_paint_pipeline.cpp
    test_image_paint_apply.cpp
    test_image_paint_3mf_roundtrip.cpp

tests/data/image_paint/
    images/
    meshes/
    expected/
```

## 4. Core Data Types

```cpp
namespace Slic3r::ImagePaint {

using FaceIndex = std::uint32_t;
using FilamentIndex = std::uint16_t;
using SelectorState = std::uint8_t;

struct ColorRgb8 {
    std::uint8_t r, g, b;
};

struct ColorRgba8 {
    std::uint8_t r, g, b, a;
};

struct ColorRgbf {
    float r, g, b;
};

struct ColorLab {
    double l, a, b;
};

struct TopologyFingerprint {
    std::uint64_t connectivity_hash = 0;
    std::uint64_t geometry_hash = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;

    bool operator==(const TopologyFingerprint&) const = default;
};

}
```

Use explicit types rather than ambiguous arrays at public boundaries.

## 5. Image Decoder

Use OpenCV already linked by Orca. Normalize all input to interleaved RGBA8.

```cpp
struct DecodedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    bool has_alpha = false;
    std::string source_name;
};

struct ImageDecodeLimits {
    std::uint32_t max_width = 16384;
    std::uint32_t max_height = 16384;
    std::uint64_t max_pixels = 100'000'000;
    std::uint64_t max_decoded_bytes = 400'000'000;
};

Expected<DecodedImage, ImagePaintError>
decode_image(const std::filesystem::path& path,
             const ImageDecodeLimits& limits);
```

Rules:

- validate dimensions before allocation;
- overflow-check `width * height * 4`;
- normalize BGR/BGRA to RGBA;
- reject unsupported channels;
- preserve alpha;
- JPEG is opaque;
- document orientation behavior;
- no network access.

## 6. Topology Fingerprint

Fingerprint inputs:

- vertex count;
- triangle count;
- triangle-index bytes;
- vertex-position bytes.

Recommended:

```cpp
TopologyFingerprint fingerprint(const TriangleMesh& mesh);
```

Do not use `std::hash` for stable identity. Use a deterministic in-project implementation. Separate connectivity and geometry hashes.

Before Apply, compare:

- object ID;
- volume ID;
- counts;
- fingerprint;
- transform snapshot where placement depends on world/camera space;
- filament snapshot identity.

## 7. Coordinate Spaces

Named spaces:

- image pixel;
- normalized image UV;
- mesh-local;
- object;
- instance/world;
- projector.

Conceptual matrices:

```text
mesh_local_to_object = volume.get_matrix()
object_to_world = instance.get_matrix()
mesh_local_to_world = object_to_world * mesh_local_to_object
world_to_projector = inverse(projector_to_world)
```

Result face indices always refer to the original mesh-local triangle order.

## 8. Planar Projection

```cpp
struct ProjectorFrame {
    Vec3d origin;
    Vec3d axis_u;
    Vec3d axis_v;
    Vec3d normal;
};

struct PlanarProjectionSettings {
    ProjectorFrame frame;
    double width_mm = 100.0;
    double height_mm = 100.0;
    double rotation_radians = 0.0;
    bool mirror_u = false;
    bool mirror_v = false;
    double alpha_threshold = 0.05;
    double minimum_coverage = 0.5;
    double front_face_cosine_threshold = 0.0;
    bool paint_through = false;
};
```

Projection:

```text
d = p - origin
x = dot(d, axis_u)
y = dot(d, axis_v)
z = dot(d, normal)

u = x / width_mm + 0.5
v = 0.5 - y / height_mm
depth = z
```

Rotate/mirror around `(0.5, 0.5)`.

```cpp
ProjectedPoint project_planar(
    const Vec3d& p,
    const PlanarProjectionSettings& s);
```

Frame construction must reject degenerate normal/up combinations and produce a right-handed orthonormal basis.

## 9. Face Sampling

```cpp
enum class SamplingQuality {
    FastCentroid,
    ThreePoint,
    Gaussian7
};

struct FaceSample {
    FaceIndex face_index = 0;
    ColorRgbf linear_rgb {};
    float alpha = 0.0f;
    float coverage = 0.0f;
    float average_depth = 0.0f;
    bool front_facing = false;
    bool inside = false;
    bool occluded = false;
};
```

Preview uses centroid. Final Apply uses deterministic Gaussian7.

Seven-point barycentric sampling:

```cpp
static constexpr std::array<std::array<float, 3>, 7> kBary = {{
    {1.f/3.f, 1.f/3.f, 1.f/3.f},
    {0.059715871f, 0.470142064f, 0.470142064f},
    {0.470142064f, 0.059715871f, 0.470142064f},
    {0.470142064f, 0.470142064f, 0.059715871f},
    {0.797426985f, 0.101286507f, 0.101286507f},
    {0.101286507f, 0.797426985f, 0.101286507f},
    {0.101286507f, 0.101286507f, 0.797426985f},
}};

static constexpr std::array<float, 7> kWeight = {
    0.225f,
    0.132394152f, 0.132394152f, 0.132394152f,
    0.125939181f, 0.125939181f, 0.125939181f
};
```

If adapted from Bambu, include provenance.

Color aggregation:

```text
coverage = sum(weight * alpha)
linear_color = sum(weight * alpha * linear_rgb) / coverage
```

Face is outside mask when coverage is below the configured threshold.

## 10. Bilinear Image Sampling

Internal convention:

```text
u = 0 left, 1 right
v = 0 top, 1 bottom
```

```cpp
ColorRgbaFloat sample_bilinear(
    const DecodedImage& image,
    double u,
    double v);
```

Core behavior must not depend on OpenGL's texture orientation.

## 11. Front-Facing and Occlusion

MVP:

- front-facing filter;
- paint-through option;
- clear UI label that true occlusion is not yet enabled if it is absent.

Facing test:

```text
front = dot(face_normal, -projection_direction) >= threshold
```

Later true occlusion options:

1. projector-space depth raster;
2. AABB ray cast;
3. hybrid depth raster plus ambiguous-ray verification.

Do not claim hidden surfaces are protected until tested.

## 12. Color Processing

Recommended flow:

```text
sRGB8
 -> normalized sRGB
 -> linear RGB for averaging
 -> XYZ
 -> Lab
 -> quantization/matching
```

### Quantization

Default cluster-then-map mode:

1. ignore transparent/ineligible samples;
2. weight by projected area or sample weight;
3. deterministic initialization;
4. cluster in Lab space;
5. remove empty clusters;
6. stable sort;
7. match to filament colors.

```cpp
struct QuantizationSettings {
    std::uint32_t target_colors = 4;
    std::uint32_t max_iterations = 30;
    double convergence_epsilon = 0.01;
    bool one_to_one_filament_match = true;
};
```

Median cut can seed deterministic Lab k-means. Random seeds are forbidden unless fixed and tested.

### CIEDE2000

```cpp
double delta_e_2000(const ColorLab& a, const ColorLab& b);
```

Use published reference pairs in tests.

## 13. Filament Snapshot and Matching

```cpp
struct FilamentColor {
    FilamentIndex project_index = 0;
    std::string name;
    std::string preset_id;
    ColorRgb8 display_rgb;
};

struct SourceCluster {
    std::uint32_t id = 0;
    ColorRgb8 representative;
    std::uint64_t sample_count = 0;
    double projected_area_mm2 = 0.0;
};

struct ClusterMatch {
    std::uint32_t cluster_id = 0;
    FilamentIndex filament_index = 0;
    double delta_e = 0.0;
    bool user_overridden = false;
};
```

Default behavior should avoid assigning multiple clusters to one filament when enough distinct filaments exist. A Hungarian assignment can solve one-to-one matching; independent closest matching remains an option.

All 0/1-based conversion is centralized.

## 14. Face Adjacency

Build an edge map keyed by sorted vertex pair.

```cpp
struct FaceAdjacency {
    std::vector<std::uint32_t> offsets;
    std::vector<FaceIndex> neighbors;
};
```

Optionally retain:

- shared edge length;
- dihedral angle;
- geometry-edge flag;
- projected edge length.

Handle boundary and non-manifold edges deterministically.

## 15. Tiny-Region Cleanup

```cpp
struct RegionCleanupSettings {
    bool enabled = true;
    std::uint32_t min_faces = 2;
    double min_surface_area_mm2 = 0.0;
    double min_projected_area_mm2 = 0.0;
    double max_cross_edge_angle_degrees = 45.0;
    std::uint32_t max_iterations = 3;
};
```

Initial merge rule:

1. identify same-state connected component;
2. if below threshold, inspect neighboring states;
3. reject candidates across protected sharp edges;
4. select longest shared boundary;
5. tie-break by lowest Delta E;
6. final tie-break by lowest filament index.

No random behavior.

## 16. Pipeline Request and Result

```cpp
struct ImagePaintRequest {
    std::uint64_t object_id = 0;
    std::uint64_t volume_id = 0;
    TopologyFingerprint topology;

    std::vector<Vec3f> local_vertices;
    std::vector<Vec3i> triangles;
    Transform3d local_to_projection_space;

    std::vector<SelectorState> existing_states;
    DecodedImage image;
    PlanarProjectionSettings projection;
    std::vector<FilamentColor> filaments;
    QuantizationSettings quantization;
    RegionCleanupSettings cleanup;

    std::uint64_t request_generation = 0;
};

struct PaintDiagnostics {
    std::uint64_t total_faces = 0;
    std::uint64_t candidate_faces = 0;
    std::uint64_t painted_faces = 0;
    std::uint64_t transparent_faces = 0;
    std::uint64_t back_facing_faces = 0;
    std::uint64_t occluded_faces = 0;
    std::uint64_t connected_components = 0;
    std::uint64_t tiny_components = 0;
    double painted_surface_area_mm2 = 0.0;
    bool coarse_mesh_warning = false;
    std::vector<std::string> warnings;
};

struct FacePaintPlan {
    std::uint64_t object_id = 0;
    std::uint64_t volume_id = 0;
    TopologyFingerprint topology;
    std::vector<SelectorState> proposed_states;
    std::vector<std::uint8_t> apply_mask;
    std::vector<SourceCluster> clusters;
    std::vector<ClusterMatch> matches;
    PaintDiagnostics diagnostics;
    std::uint64_t request_generation = 0;
    std::string algorithm_version;
};
```

Pipeline:

```text
validate
 -> transform
 -> project/sample
 -> eligible samples
 -> quantize
 -> match
 -> assign proposed states
 -> adjacency
 -> cleanup
 -> diagnostics
 -> immutable plan
```

## 17. Cancellation and Progress

```cpp
using CancelCheck = std::function<bool()>;
using ProgressCallback =
    std::function<void(float, std::string_view)>;
```

Cancellation checkpoints:

- after validation;
- after decode;
- during batched face sampling;
- between cluster iterations;
- during component traversal;
- before result publication.

Cancellation is not an error dialog.

## 18. Existing-State Merge

Use a separate mask so state zero is not overloaded.

```cpp
enum class ExistingPaintPolicy {
    PreserveExisting,
    PaintOnlyUnpainted,
    OverwriteInsideMask
};

Expected<std::vector<SelectorState>, ImagePaintError>
merge_face_states(
    std::span<const SelectorState> existing,
    std::span<const SelectorState> proposed,
    std::span<const std::uint8_t> mask,
    ExistingPaintPolicy policy);
```

Unmasked faces always preserve existing state.

## 19. Selector-State Conversion

Central utility:

```cpp
Expected<EnforcerBlockerType, ImagePaintError>
filament_index_to_selector_state(FilamentIndex index)
{
    const int value =
        static_cast<int>(EnforcerBlockerType::Extruder1) +
        static_cast<int>(index);

    const auto state = static_cast<EnforcerBlockerType>(value);

    if (state < EnforcerBlockerType::Extruder1 ||
        state > EnforcerBlockerType::ExtruderMax)
        return unexpected(...);

    return state;
}
```

Do not spread `+1/-1` arithmetic through the codebase.

## 20. UI-Thread Apply

Required order:

1. assert UI thread;
2. resolve object/volume by stable ID;
3. confirm model-part type;
4. recompute fingerprint;
5. verify face count;
6. verify filament configuration;
7. read existing states;
8. merge;
9. validate every state;
10. create one Orca undo snapshot;
11. construct/update TriangleSelector;
12. set `mmu_segmentation_facets`;
13. mark project dirty;
14. invalidate slice;
15. refresh object-list filament indicators;
16. refresh 3D paint render data;
17. complete atomically.

Conceptual code:

```cpp
bool ImagePaintController::apply_plan(const FacePaintPlan& plan)
{
    assert(wxIsMainThread());

    ModelVolume* volume =
        find_volume_by_stable_ids(m_plater.model(),
                                  plan.object_id,
                                  plan.volume_id);

    if (!volume || !volume->is_model_part())
        return fail("Target no longer exists.");

    if (fingerprint(volume->mesh()) != plan.topology)
        return fail("Model changed; recompute preview.");

    const auto existing = read_mmu_face_states(*volume);
    auto merged = merge_face_states(
        existing,
        plan.proposed_states,
        plan.apply_mask,
        m_ui_state.existing_paint_policy);

    if (!merged)
        return fail(merged.error().message);

    // Use exact current Orca snapshot API used by painting gizmos.
    take_single_undo_snapshot("Apply image painting");

    TriangleSelector selector(volume->mesh());
    for (std::size_t i = 0; i < merged->size(); ++i)
        selector.set_facet(static_cast<int>(i),
                           decode_selector_state((*merged)[i]));

    volume->mmu_segmentation_facets.set(selector);
    invalidate_and_refresh_after_paint();
    return true;
}
```

Exact APIs must be verified in current source.

## 21. Controller and Job Lifecycle

The controller owns:

- target identity;
- decoded image;
- placement settings;
- palette snapshot;
- current generation;
- cancel token;
- latest plan;
- diagnostics;
- Apply state.

Generation pattern:

```cpp
const auto generation = ++m_generation;
cancel_previous_job();
start_job(snapshot_request(generation));
```

Completion:

```cpp
if (result.request_generation != m_generation.load())
    discard_result();
else
    publish_preview_on_ui_thread();
```

Workers receive owned snapshots, never live pointers.

## 22. GUI Design

Recommended separate gizmo: `GLGizmoImagePainter`.

Panel:

```text
Image
  Choose Image
  Dimensions / alpha
  Crop / reset / mirror

Projection
  Planar
  Camera / Front / Back / Left / Right / Top / Bottom
  Width / Height / Aspect lock
  Rotation
  Fit Selection / Center

Surface
  Front-facing only
  Paint through
  Alpha/coverage threshold

Colors
  Target color count
  Cluster -> filament mapping
  Auto Match / Reset

Cleanup
  Tiny islands
  Minimum faces / area
  Preserve sharp edges

Existing Painting
  Preserve / Paint unpainted / Overwrite inside image

Diagnostics
  Painted faces
  Regions
  Coarse-mesh warning

Cancel / Apply
```

Preview modes:

- source overlay;
- printable filament colors;
- change mask;
- wireframe.

The final preview must use the same proposed states Apply will write.

## 23. Direct Manipulation

3D controls:

- U/V translation;
- uniform and independent scale;
- rotation around projector normal;
- mirror;
- reset;
- camera alignment.

During drag:

- debounce;
- centroid sampling;
- cancel older generation.

After drag:

- Gaussian7 final preview;
- diagnostics update.

## 24. Dependencies

MVP dependency table:

| Dependency | Existing | Purpose |
|---|---:|---|
| Eigen3 | yes | transforms/math |
| OpenCV | yes | image decode/process |
| oneTBB | yes | parallel sampling |
| Boost.Log | yes | diagnostics |
| TriangleMesh | yes | geometry |
| TriangleSelector | yes | face state |
| wxWidgets | yes | file dialogs |
| ImGui | yes | gizmo controls |
| OpenGL | yes | preview |
| Catch2 v3 | yes | tests |
| PNG/JPEG/Zlib | yes | codecs |

No new external MVP dependency.

Potential later dependencies:

- Assimp for textured OBJ/glTF/GLB, only after investigation;
- Bambu TextureToColor source, ported with attribution;
- optional Python NumPy/Pillow for experimental plugins.

## 25. CMake Samples

Add core files to `src/libslic3r/CMakeLists.txt`.

```cmake
ImagePaint/ImagePaintTypes.hpp
ImagePaint/ImagePaintErrors.hpp
ImagePaint/ImageDecoder.hpp
ImagePaint/ImageDecoder.cpp
ImagePaint/ColorSpace.hpp
ImagePaint/ColorSpace.cpp
ImagePaint/ColorDifference.hpp
ImagePaint/ColorDifference.cpp
ImagePaint/Projection.hpp
ImagePaint/Projection.cpp
ImagePaint/FaceSampler.hpp
ImagePaint/FaceSampler.cpp
ImagePaint/FaceAdjacency.hpp
ImagePaint/FaceAdjacency.cpp
ImagePaint/ColorQuantizer.hpp
ImagePaint/ColorQuantizer.cpp
ImagePaint/FilamentMatcher.hpp
ImagePaint/FilamentMatcher.cpp
ImagePaint/RegionCleaner.hpp
ImagePaint/RegionCleaner.cpp
ImagePaint/PaintDiagnostics.hpp
ImagePaint/PaintDiagnostics.cpp
ImagePaint/TopologyFingerprint.hpp
ImagePaint/TopologyFingerprint.cpp
ImagePaint/ImagePaintPipeline.hpp
ImagePaint/ImagePaintPipeline.cpp
ImagePaint/PaintStateMerge.hpp
ImagePaint/PaintStateMerge.cpp
```

Orca currently finds OpenCV core and links `opencv_world`. If explicit components are required, test:

```cmake
find_package(OpenCV REQUIRED core imgcodecs imgproc)
```

Do not change blindly; verify all platforms.

## 26. Bambu Port Boundary

Port early only when useful:

- CIEDE2000;
- Gaussian sampling constants;
- deterministic color utilities;
- selected algorithm concepts.

Defer:

- topology replacement;
- adaptive subdivision;
- CGAL repair;
- multi-texture atlas;
- Assimp import;
- mixed virtual filaments;
- full TextureImportDialog.

Any topology-changing result must use a different type:

```cpp
struct TopologyChangingPaintPlan {
    TriangleMesh replacement_mesh;
    std::vector<SelectorState> face_states;
    AnnotationMigrationReport migration;
};
```

It must explicitly migrate or invalidate MMU, seam, support, and fuzzy-skin data.

## 27. Optional Plugin Bridge

Later module:

```python
orca.host.paint
```

Possible functions:

```python
selected_targets()
read_face_states(object_id, volume_id)
topology_fingerprint(object_id, volume_id)
apply_face_states(
    object_id,
    volume_id,
    states,
    mask=None,
    expected_topology=None,
    snapshot_name="Apply plugin face painting",
    overwrite_policy="overwrite_inside_mask",
)
clear_face_states(...)
```

Safety:

- explicit ModelMutation capability;
- UI-thread only;
- exact dtype/rank/length validation;
- valid state range;
- stable IDs and topology;
- undo;
- dirty/slice/render refresh;
- no mutable mesh pointer;
- no generic set_mesh.

## 28. Persistence

MVP uses existing path:

```text
FacePaintPlan
 -> TriangleSelector
 -> ModelVolume::mmu_segmentation_facets
 -> existing Orca 3MF writer
 -> existing Orca 3MF reader
```

No custom extension.

Future re-editable metadata is optional and non-authoritative. Loss of metadata must not lose applied face states.

## 29. Test Architecture

Unit tests:

- decoder formats/limits;
- projection known points;
- mirror/rotation;
- bilinear sample;
- Gaussian weights;
- alpha/coverage;
- color-space reference;
- CIEDE2000 reference;
- deterministic clustering;
- matching;
- adjacency;
- non-manifold edges;
- cleanup;
- state merge;
- fingerprint.

Integration:

- apply known plan;
- `is_mm_painted`;
- undo/redo;
- save 3MF;
- reload;
- compare state arrays;
- compare printer/process/filament config;
- stale target rejection;
- unrelated annotations unchanged.

Golden outputs should be face-state hashes/counts, not only screenshots.

## 30. Build/Test Commands

Windows:

```bat
build_release_vs.bat
build_release_vs.bat tests
ctest --test-dir tests -C Release --output-on-failure
```

Linux:

```bash
./build_linux.sh -dsti
./build_linux.sh -t
ctest --test-dir tests --output-on-failure
```

Focused:

```bash
ctest --test-dir tests -R image_paint --output-on-failure
```

## 31. Logging

Stage names:

```text
image_paint.decode
image_paint.snapshot
image_paint.project
image_paint.sample
image_paint.quantize
image_paint.match
image_paint.cleanup
image_paint.preview
image_paint.validate_apply
image_paint.apply
```

Log counts, dimensions, stage time, cancellation, and validation failures. Never log raw image data.

## 32. Performance Rules

- contiguous vectors;
- no per-face heap vectors;
- parallel face loops with TBB;
- UI/model mutation serial;
- cache adjacency and transformed vertices;
- preview may be lower quality;
- optimize only after profiling.

## 33. Error Model

```cpp
enum class ImagePaintErrorCode {
    NoSelection,
    InvalidTarget,
    UnsupportedVolume,
    ImageOpenFailed,
    ImageDecodeFailed,
    ImageTooLarge,
    InvalidProjection,
    NoEligibleFaces,
    NoAvailableFilaments,
    TooManyColors,
    QuantizationFailed,
    Canceled,
    TargetDeleted,
    TopologyChanged,
    FilamentConfigurationChanged,
    FaceCountMismatch,
    FilamentOutOfRange,
    ApplyFailed,
    InternalInvariantViolation
};
```

User message and technical detail should be separated.

## 34. First Implementation Slice

First PR:

1. types/errors;
2. topology fingerprint;
3. planar projection;
4. tests on in-memory triangle/quad/cube;
5. no GUI;
6. no decode;
7. no mutation.

Second vertical slice:

1. decode tiny PNG;
2. project onto cube;
3. produce mask and two states;
4. compare golden array.

Third:

1. apply known states to ModelVolume;
2. save/reload 3MF;
3. prove persistence.

Only then build the interactive gizmo.
