---
title: OrcGraffiti Project Truth
status: Authoritative
last_researched: 2026-08-05
orcaslicer_baseline: b97ca3c0ace8cb04eb520d86417fbe13b7ddbdde
bambustudio_reference: f43cbe4296b16cb6089a47af8fe82d2b79d2442f
license: AGPL-3.0-compatible derivative work
---

# OrcGraffiti — Project Truth

## 1. Purpose

This file is the authoritative source of truth for the OrcGraffiti project. Every human or AI contributor must read it before planning or modifying the codebase. When another document, issue, comment, or generated plan conflicts with this file, this file wins unless a maintainer explicitly changes it.

OrcGraffiti is an OrcaSlicer-native tool that projects a 2D image onto a 3D model and converts that image into OrcaSlicer-compatible per-face multicolor painting. It must work with the user's active printer, process, filament, and plate configuration rather than importing assumptions from an external project.

The first production version is a native C++ feature in an OrcaSlicer fork. A safe Python plugin bridge may be added later.

**Agent / automation surface (design):** headless CLI + optional skill/MCP so AI tools can paint and inspect without the GUI. Normative design lives in `docs/OrcGraffiti/Agent_Surface.md` (ADR-0010). Implementation is deferred; the GUI-independent ImagePaint core is the shared engine for both gizmo and future CLI.

## 2. Problem Statement

MakerLab Mesh Graffiti proves that image-to-surface coloring is useful, but the external workflow has limitations:

- The exported 3MF may contain settings that do not match the user's printer.
- Placement and palette decisions happen outside OrcaSlicer.
- Revisions require export/import cycles.
- The tool cannot use Orca's current filament list, printer profile, or slice preview while authoring.
- Mesh density and printable feature size are not handled in a printer-aware way.
- A generic 3MF patcher would duplicate fragile internal logic.
- Orca's current Python host exposes read-only mesh snapshots and cannot safely mutate painted face data.

OrcGraffiti solves the problem in the slicer itself.

## 3. Working Terminology

- **OrcGraffiti**: the complete project.
- **Image Paint**: the user-facing tool.
- **Face state**: the filament/extruder assignment attached to one triangle.
- **Paint mask**: which faces may be changed.
- **Paint plan**: an immutable computed result waiting to be applied.
- **Projection**: mapping from 3D surface points to 2D image coordinates.
- **Palette**: printable target filament colors.
- **Topology fingerprint**: deterministic mesh identity used to prevent stale apply.
- **MMU painting**: Orca's existing per-face multicolor mechanism; it is not Bambu-only hardware.

## 4. Product Vision

The user should be able to:

1. Open or create an OrcaSlicer project.
2. Select one model volume.
3. Open Image Paint.
4. Load PNG, JPG/JPEG, or BMP.
5. Position, scale, rotate, mirror, crop, and project the image.
6. Reduce the image to the current project's available filament colors.
7. Preview actual triangle-level filament assignments.
8. Remove tiny unprintable islands.
9. Receive warnings when the mesh is too coarse.
10. Apply the result as one undoable operation.
11. Slice normally with the current printer and process settings.
12. Save a standard Orca project 3MF whose painted facets reopen correctly.

## 5. Core Truths

### 5.1 Existing Orca painting is the output format

The MVP must write the same painted-facet representation as Orca's existing Color Painting tool.

Primary existing integration points:

```text
src/slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.hpp
src/slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.cpp
src/slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp
src/libslic3r/TriangleSelector.hpp
src/libslic3r/TriangleSelector.cpp
src/libslic3r/Model.hpp
src/libslic3r/Model.cpp
src/libslic3r/Format/3mf.cpp
src/libslic3r/MultiMaterialSegmentation.cpp
```

The relevant model field is:

```cpp
ModelVolume::mmu_segmentation_facets
```

The MVP must not invent a second color format.

### 5.2 Printer and process settings are not changed

Applying an image must not replace, reset, or import:

- printer preset;
- process preset;
- filament presets;
- nozzle configuration;
- plate configuration;
- support settings;
- speeds;
- temperatures;
- start/end G-code;
- custom machine settings;
- calibrations.

The operation changes model face annotation only.

### 5.3 Native C++ MVP

The first production implementation is native C++ because it needs:

- safe live model mutation;
- undo/redo;
- selection and camera access;
- rendering;
- filament configuration;
- dirty-state and slice invalidation;
- normal Orca 3MF persistence.

The current Python plugin host is useful for inspection and experiments but intentionally exposes read-only model/mesh data.

### 5.4 No remeshing in MVP

The MVP paints the existing topology. It must not:

- subdivide;
- repair;
- collapse edges;
- add/delete vertices;
- reorder triangles;
- replace the mesh.

Topology changes can invalidate MMU, seam, support, and fuzzy-skin annotations. Optional localized remeshing is a later, separate operation.

### 5.5 Resolution is limited by triangles

A high-resolution image cannot create smooth edges on a coarse mesh. The final preview must show actual face boundaries, not only a smooth texture overlay.

The tool should calculate:

- total and painted face counts;
- approximate projected face size;
- connected region count;
- tiny region count;
- projected surface area;
- coarse-mesh warning.

### 5.6 Results must be stale-safe

Each paint plan must include:

- object stable ID;
- volume stable ID;
- vertex count;
- triangle count;
- topology/connectivity hash;
- geometry hash;
- projection settings;
- palette snapshot identity;
- request generation;
- algorithm version.

Before Apply, the live target is re-resolved and validated. A mismatch requires recomputation.

### 5.7 Thread ownership

Workers may process immutable snapshots. Workers must never mutate:

- Model;
- ModelObject;
- ModelVolume;
- TriangleMesh shared by the live graph;
- Plater;
- GUI widgets;
- OpenGL state.

The UI thread performs final validation, undo capture, mutation, invalidation, dirty marking, and rendering refresh.

## 6. Verified Upstream Facts

Research baseline:

```text
OrcaSlicer/OrcaSlicer
branch: main
commit: b97ca3c0ace8cb04eb520d86417fbe13b7ddbdde
date: 2026-08-05
```

Relevant plugin host files:

```text
src/slic3r/plugin/host/PluginHostBindings.hpp
src/slic3r/plugin/host/PluginHostModel.cpp
src/slic3r/plugin/host/PluginHostMesh.cpp
src/slic3r/plugin/host/PluginHostApp.cpp
```

At the baseline:

- ModelVolume.mesh() returns an immutable shared snapshot.
- Vertices can be exposed as a read-only zero-copy NumPy float32 array.
- Triangle indices can be exposed as a read-only zero-copy NumPy int32 array.
- Face normals are readable.
- No general safe model mutation API is exposed.
- The mesh binding explicitly warns that future mutation must use owned copies or validated handoff.

Orca already uses or links:

- Eigen3;
- OpenCV;
- CGAL;
- oneTBB;
- Boost;
- wxWidgets;
- ImGui;
- OpenGL;
- PNG/JPEG/Zlib;
- Catch2 v3.

No new external dependency is required for the MVP.

## 7. Bambu Studio Prior Art

Reference:

```text
bambulab/BambuStudio
commit: f43cbe4296b16cb6089a47af8fe82d2b79d2442f
```

Relevant files:

```text
src/libslic3r/TexturePainting.hpp
src/libslic3r/TexturePainting.cpp
src/libslic3r/TextureToColor/TextureToColor.hpp
src/libslic3r/TextureToColor/TextureToColor.cpp
src/libslic3r/TextureToColor/ColorUtils.hpp
src/libslic3r/TextureToColor/ColorUtils.cpp
src/slic3r/GUI/TextureImportDialog.hpp
src/slic3r/GUI/TextureImportDialog.cpp
src/libslic3r/Format/OBJ.cpp
src/libslic3r/Format/AssimpImport.cpp
```

Useful prior art includes:

- UV sampling;
- bilinear filtering;
- seven-point triangular Gaussian sampling;
- color clustering;
- CIEDE2000;
- region smoothing;
- progress/cancellation;
- filament matching;
- applying paint through TriangleSelector;
- multi-texture atlases;
- optional subdivision and mesh repair.

For MVP, reuse concepts and small attributable utilities, not the entire topology-changing textured import path.

## 8. MVP Scope

### Inputs

- PNG;
- JPG/JPEG;
- BMP;
- alpha where present;
- one selected model-part volume;
- current project filament colors.

### Placement

- planar projection;
- current camera direction;
- object-axis presets;
- translation;
- scale;
- rotation;
- horizontal/vertical mirror;
- aspect lock;
- fit/fill;
- alpha threshold;
- front-facing filter;
- optional paint-through, default off.

### Color

- target colors based on configured project filaments;
- target color count limited by available filaments and Orca paint-state limits;
- automatic CIEDE2000 matching;
- manual cluster-to-filament remapping;
- transparent samples do not alter existing states;
- tiny-region cleanup;
- dithering disabled by default.

### Preview

- image overlay;
- actual face-state preview;
- wireframe;
- ignored/back-facing faces;
- palette mapping;
- painted-face count;
- connected regions;
- coarse-mesh warning.

### Apply

- one undo record;
- exact face count and fingerprint validation;
- preserves unmasked existing paint;
- marks project dirty;
- invalidates slice;
- refreshes view;
- persists through 3MF save/load;
- leaves project profiles untouched.

## 9. Post-MVP Scope

- cylindrical projection;
- spherical projection;
- true occlusion;
- UV texture conversion;
- textured OBJ/glTF/GLB;
- editable operation history;
- multiple images;
- surface-conforming decals;
- mesh-aware dithering;
- local subdivision/remeshing;
- plugin mutation bridge;
- multi-volume projection;
- purge-aware palette choices;
- translucency/material-aware color prediction.

## 10. Non-Goals for MVP

The MVP is not:

- a UV editor;
- Blender;
- an emboss/deboss tool;
- a continuous-tone printer;
- a full mesh repair application;
- a cloud service;
- a MakerLab clone;
- a generic 3MF patcher;
- a promise of photo-realistic four-color output;
- a generic Python mutable mesh API.

## 11. Functional Requirements

### Selection

- FR-SEL-001: require exactly one selected model volume.
- FR-SEL-002: reject negative volumes, modifiers, blockers, enforcers, and non-model parts.
- FR-SEL-003: store stable object and volume IDs.
- FR-SEL-004: detect deleted/replaced target before Apply.
- FR-SEL-005: handle transforms and mirrored instances explicitly.

### Image

- FR-IMG-001: decode PNG/JPEG/BMP.
- FR-IMG-002: preserve alpha.
- FR-IMG-003: reject corrupt/empty images cleanly.
- FR-IMG-004: enforce dimension and decoded-byte limits.
- FR-IMG-005: normalize channel order.
- FR-IMG-006: document JPEG orientation behavior.
- FR-IMG-007: never upload image/model data.

### Projection

- FR-PROJ-001: implement planar projection.
- FR-PROJ-002: define origin, U, V, and normal.
- FR-PROJ-003: support translate/scale/rotate/mirror.
- FR-PROJ-004: support front-facing filtering.
- FR-PROJ-005: default to no back-face painting.
- FR-PROJ-006: projection math must be GUI-independent and tested.

### Sampling

- FR-SAMP-001: fast centroid preview.
- FR-SAMP-002: deterministic quality sampling for final apply.
- FR-SAMP-003: transparent samples leave the face unchanged.
- FR-SAMP-004: document coverage threshold.
- FR-SAMP-005: same input/settings produce same result.

### Palette

- FR-COL-001: palette comes from current project filaments.
- FR-COL-002: automatic matching uses CIEDE2000.
- FR-COL-003: manual override is supported.
- FR-COL-004: state count never exceeds Orca's valid range.
- FR-COL-005: human 1-based labels and internal 0-based indices are centralized.

### Cleanup

- FR-CLN-001: build face adjacency.
- FR-CLN-002: identify same-color connected components.
- FR-CLN-003: merge/remove tiny regions.
- FR-CLN-004: preserve sharp geometry boundaries when configured.
- FR-CLN-005: report cleanup changes.

### Apply

- FR-APP-001: preview does not mutate model.
- FR-APP-002: Apply is one undoable action.
- FR-APP-003: IDs, counts, topology, and filament snapshot are validated.
- FR-APP-004: unmasked states are preserved.
- FR-APP-005: existing-paint overwrite behavior is selectable.
- FR-APP-006: project dirty and slice invalidation occur.
- FR-APP-007: undo/redo restore exact arrays.

### Persistence

- FR-PER-001: paint survives project 3MF save/reload.
- FR-PER-002: project settings are unchanged.
- FR-PER-003: no custom 3MF extension is required for MVP.
- FR-PER-004: optional future metadata is non-authoritative.

## 12. Nonfunctional Requirements

### Reliability

- no out-of-range face writes;
- no worker-thread model mutation;
- no stale apply;
- no silent remeshing;
- deterministic output;
- exact undo/redo.

### Performance targets

Design targets:

- interactive preview near 100k faces on a modern desktop;
- final 100k-face/4MP processing around 2 seconds where practical;
- 1M faces around 10 seconds without remeshing;
- under 1 GB additional memory for 1M faces and a large image;
- cancellation observed at regular checkpoints.

Benchmarks must record hardware, build type, face count, image dimensions, colors, and settings.

### Portability

Windows, macOS, and Linux must remain supported.

### Maintainability

- algorithms live in libslic3r;
- GUI does not own color science;
- no printer-vendor assumptions;
- typed coordinate spaces;
- small testable stages;
- no global mutable image-paint singleton.

## 13. Invariants

- INV-001: one face state per current triangle.
- INV-002: zero follows Orca's existing unpainted state convention.
- INV-003: states remain within Extruder1..ExtruderMax.
- INV-004: target validated by stable IDs and topology fingerprint.
- INV-005: unmasked faces preserve previous state.
- INV-006: Cancel leaves model unchanged.
- INV-007: one undo snapshot per Apply.
- INV-008: no printer/profile mutation.
- INV-009: coordinate spaces are named.
- INV-010: thread ownership is explicit.
- INV-011: preview result is disposable.
- INV-012: topology-changing operations use a separate plan type.

## 14. Coordinate-Space Truth

The mesh is stored in volume-local coordinates.

Conceptual chain:

```text
mesh-local
  -> volume transform
object space
  -> instance transform
world space
  -> projector transform
projector space
  -> normalized image UV
```

Recommended MVP:

1. snapshot local vertices and triangles;
2. snapshot volume and instance transforms;
3. compute world/projector positions in worker-owned memory;
4. store results by original local triangle index;
5. apply states to the original ModelVolume.

Every API handling positions must identify its space.

## 15. Color Truth

Configured RGB filament colors are only approximations of printed color. Results depend on filament translucency, finish, lighting, contamination, layer height, nozzle width, and physical material behavior.

The UI should say “closest configured filament color,” not “exact print color.”

CIEDE2000 is the initial matching metric. Manual mapping remains authoritative.

## 16. Licensing and Provenance

OrcaSlicer and Bambu Studio use AGPL-3.0.

When adapting Bambu code:

- preserve copyright/license headers;
- record repository, path, and commit;
- add a provenance comment;
- update third-party notices;
- do not claim original authorship.

Recommended comment:

```cpp
// Portions adapted from Bambu Studio.
// Source: https://github.com/bambulab/BambuStudio
// Reference commit: f43cbe4296b16cb6089a47af8fe82d2b79d2442f
// Original path: src/libslic3r/TextureToColor/ColorUtils.cpp
// License: GNU Affero General Public License v3.0
```

Do not copy private MakerLab implementation code.

## 17. Safety and Security

The feature processes untrusted local files.

Required safeguards:

- bounded decode dimensions/bytes;
- overflow-safe multiplication;
- established codec use;
- no network request;
- no metadata execution;
- no raw mutable plugin pointers;
- no generic set_mesh plugin API;
- main-thread mutation only;
- undo-wrapped mutation;
- audit capability for future plugin writes.

## 18. Decisions Already Made

| Decision | Status |
|---|---|
| OrcaSlicer fork | accepted |
| Native C++ MVP | accepted |
| Existing MMU facet format | accepted |
| Planar first | accepted |
| No remeshing MVP | accepted |
| Project filaments as palette | accepted |
| CIEDE2000 matching | accepted |
| Worker compute/UI apply | accepted |
| Stable IDs + topology hash | accepted |
| Plugin bridge later | accepted |
| No new MVP dependency | accepted |
| AGPL provenance | accepted |

## 19. Failure Modes to Prevent

- applying to the wrong volume;
- stale result after model repair/change;
- off-by-one extruder mapping;
- unintended back-surface paint;
- transparent pixels erasing prior paint;
- clearing manual paint outside the image;
- partial undo;
- lost paint after reopen;
- imported 3MF settings replacing current profiles;
- worker dereferencing deleted live objects;
- decode overflow;
- misleading smooth preview on coarse mesh;
- cleanup crossing a sharp edge;
- mirrored orientation errors;
- filament reorder invalidating a pending plan;
- reentrant/multiple Apply;
- completion callback touching destroyed UI;
- failed apply leaving partial changes.

## 20. Definition of MVP Success

The MVP is complete only when:

- one model-part volume can be selected;
- PNG/JPEG/BMP can be loaded;
- planar placement works;
- alpha preserves unpainted faces;
- source colors map to current project filaments;
- actual face assignments are previewed;
- manual remapping works;
- tiny-region cleanup works;
- Apply is a single undo action;
- undo/redo are exact;
- slicing uses Orca's normal pipeline;
- 3MF save/reopen preserves states;
- printer/process/filament settings remain unchanged;
- tests cover projection, alpha, mapping, cleanup, stale validation, apply, undo, and round trip;
- provenance is documented.

A visual demo without persistence and undo proof is not completion.

## 21. AI Agent Rules

Every AI agent must:

1. read all three planning documents;
2. identify the current roadmap phase;
3. inspect current source before assuming baseline paths;
4. plan against requirement/invariant IDs;
5. add tests with behavior changes;
6. keep algorithms GUI-independent;
7. preserve all project settings and unrelated annotations;
8. avoid broad unrelated refactors;
9. record provenance;
10. run relevant builds/tests;
11. report verified and unverified work;
12. never weaken stale-result or thread checks;
13. never add a dependency without license/platform/build analysis;
14. explicitly state coordinate spaces and thread ownership.

Before coding, answer:

- Which face-indexed data can this change invalidate?
- What coordinate space is used?
- Who owns the data?
- Which thread runs it?
- What happens if the project changes?
- What proves 3MF persistence?
- What exactly does Undo restore?
- Is any printer-vendor assumption present?

## 22. References

OrcaSlicer:

- https://github.com/OrcaSlicer/OrcaSlicer
- https://www.orcaslicer.com/wiki/developer_guide/How_to_build/
- https://www.orcaslicer.com/wiki/developer_guide/Tests/
- https://www.orcaslicer.com/wiki/developer_reference/plugin_development/plugin_development.html

Bambu Studio:

- https://github.com/bambulab/BambuStudio
- https://github.com/bambulab/BambuStudio/releases

## 23. Final Truth

OrcGraffiti is a printer-independent OrcaSlicer image-to-face-painting tool. Its first responsibility is correctness: correct target, correct faces, correct filament mapping, exact undo, reliable persistence, and no damage to project settings.
