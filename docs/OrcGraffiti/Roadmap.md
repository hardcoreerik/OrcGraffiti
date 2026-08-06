---
title: OrcGraffiti Roadmap
status: Gated execution roadmap
last_researched: 2026-08-05
---

# OrcGraffiti Roadmap

## 1. Execution Rules

This roadmap is gated. Do not skip to a later phase because it produces a better demo. Each phase has an exit gate that must be demonstrated.

MVP consists of Phases 0–6.

Development principles:

- small mergeable PRs;
- tests before UI;
- persistence proof before interactive controls;
- native feature before plugin bridge;
- no new external dependency for MVP;
- explicit provenance;
- continuous upstream rebasing;
- no remeshing before annotation-safety work.

## 2. Milestones

| Phase | Name | Outcome |
|---:|---|---|
| 0 | Baseline | reproducible fork/build/tests/docs |
| 1 | Core identity | typed data and stale-result protection |
| 2 | Decode/projection | deterministic image-to-face samples |
| 3 | Printable color | quantization, matching, cleanup |
| 4 | Apply/persistence | undoable MMU paint and 3MF round trip |
| 5 | Interactive MVP | full planar Image Paint workflow |
| 6 | Hardening | performance, safety, platform release |
| 7 | Curved/occlusion | cylindrical, spherical, hidden-surface control |
| 8 | Textures/remeshing | UV conversion and controlled topology change |
| 9 | Plugin bridge | safe external face-state mutation |
| 10 | Release/upstream | public fork release and upstream strategy |
| AS-* | Agent Surface | headless CLI + skill + optional MCP (see §20) |

## 3. Branch Strategy

```text
upstream-main
main
feature/image-paint-*
release/*
```

Recommended PR sequence:

1. docs/build baseline;
2. types/errors;
3. topology fingerprint;
4. projection;
5. decoder;
6. sampling;
7. color conversion/Delta E;
8. quantizer/matcher;
9. adjacency/cleanup;
10. headless pipeline;
11. state read/write;
12. 3MF round trip;
13. controller/job lifecycle;
14. preview renderer;
15. gizmo controls;
16. hardening.

## 4. Phase 0 — Repository and Build Baseline

### Objective

Create a reproducible development environment and preserve the design decisions.

### Tasks

- fork OrcaSlicer;
- configure upstream remote;
- record baseline commit;
- commit the three project documents;
- create provenance/test-matrix docs;
- complete Windows release build;
- build/run tests;
- add Linux/Windows/macOS CI skeleton;
- define development-log convention.

Example:

```bash
git remote add upstream https://github.com/OrcaSlicer/OrcaSlicer.git
git fetch upstream
git checkout -b feature/image-paint-baseline upstream/main
```

Windows:

```bat
build_release_vs.bat
build_release_vs.bat tests
ctest --test-dir tests -C Release --output-on-failure
```

### Deliverables

```text
docs/OrcGraffiti/Project_Truth.md
docs/OrcGraffiti/Architechture.md
docs/OrcGraffiti/Roadmap.md
docs/OrcGraffiti/Port_Provenance.md
docs/OrcGraffiti/Test_Matrix.md
```

### Tests

- clean clone build;
- existing tests;
- launch Orca;
- open/save/reopen a basic project;
- no behavior change.

### Exit Gate

Another developer can reproduce the baseline build and tests using documented commands.

## 5. Phase 1 — Core Types and Topology Safety

### Objective

Create data contracts and stale-result detection.

### Tasks

- add explicit ID/index/color/state types;
- add request/result/error skeleton;
- implement deterministic connectivity hash;
- implement deterministic geometry hash;
- validate mesh indices/counts;
- add unit tests.

### Deliverables

```text
ImagePaintTypes.hpp
ImagePaintErrors.hpp
TopologyFingerprint.hpp/.cpp
```

### Tests

- identical mesh fingerprints equal;
- vertex change alters geometry hash;
- topology change alters connectivity hash;
- triangle reorder detected;
- invalid index rejected;
- overflow path rejected.

### Exit Gate

A completed result can be proven to match or not match the current mesh without live pointers.

## 6. Phase 2 — Image Decode, Planar Projection, Sampling

### Objective

Return deterministic RGBA/coverage samples per mesh face.

### Tasks

- bounded PNG/JPEG/BMP decoder;
- RGBA normalization;
- projector frame creation;
- camera/object-axis presets;
- local/object/world/projector transforms;
- planar projection;
- bilinear sample;
- centroid preview;
- Gaussian7 final sample;
- front-facing filter;
- alpha/coverage rule;
- cancellation checkpoints.

### Fixtures

- 2x2 RGBA;
- checkerboard;
- alpha circle;
- JPEG gradient;
- BMP bars;
- corrupt file;
- cube/quad meshes.

### Tests

- channel order;
- alpha;
- known projection coordinates;
- 90-degree rotation;
- mirrors;
- degenerate frame;
- back face;
- bilinear interpolation;
- deterministic sampling;
- cancellation;
- mirrored instance transform.

### Exit Gate

A headless test projects an image onto a mesh and returns correct per-face samples.

## 7. Phase 3 — Palette, Matching, Cleanup

### Objective

Convert samples to valid printable Orca face states.

### Tasks

- sRGB/linear/XYZ/Lab;
- CIEDE2000 with provenance;
- deterministic Lab quantization;
- project filament snapshot;
- automatic matching;
- optional one-to-one assignment;
- manual override model;
- central selector-state conversion;
- adjacency graph;
- component cleanup;
- diagnostics/coarse warning.

### Tests

- color reference conversions;
- official Delta E pairs;
- deterministic clusters;
- empty cluster removal;
- duplicate filament colors;
- color-count clamping;
- one-to-one matching;
- boundary/non-manifold adjacency;
- tiny island merge;
- sharp-edge preservation;
- valid state range.

### Exit Gate

A headless image+mesh request produces a complete validated FacePaintPlan.

## 8. Phase 4 — Apply, Undo, 3MF Persistence

### Objective

Write a plan safely into Orca and prove it survives.

### Tasks

- inspect/reuse existing color-painter update path;
- typed read of existing MMU states;
- merge policies;
- stable-ID target resolution;
- UI-thread validation;
- exact topology check;
- filament-snapshot check;
- one undo snapshot;
- TriangleSelector state write;
- dirty marking;
- slice invalidation;
- object-list/canvas refresh;
- 3MF round-trip integration test;
- profile-preservation test;
- verify seam/support/fuzzy annotations unchanged.

### Tests

- successful apply;
- invalid face count;
- stale topology;
- deleted target;
- invalid selector state;
- preserve existing;
- overwrite inside mask;
- undo;
- redo;
- save/reload;
- settings unchanged.

### Exit Gate

An automated test proves Apply, Undo, Redo, Save, Reload, and profile preservation.

## 9. Phase 5 — Interactive Planar MVP

### Objective

Deliver the full user workflow.

### Tasks

#### Gizmo registration

- add icon/resources;
- register separate Image Paint gizmo;
- FFF/multicolor activation rules;
- one model-part volume requirement.

#### Target lifecycle

- snapshot target on open;
- detect selection change/deletion;
- cancel uncommitted work safely.

#### Image

- file chooser;
- format filters;
- decode/error/progress;
- reset.

#### Placement

- camera and six axes;
- width/height;
- aspect lock;
- rotate;
- translate;
- mirror;
- center/fit/reset;
- direct 3D manipulation.

#### Preview jobs

- debounce;
- generation counter;
- cancel old jobs;
- centroid during drag;
- Gaussian7 when idle;
- only newest result published.

#### Preview modes

- source image overlay;
- printable filament colors;
- mask/change view;
- wireframe.

#### Palette UI

- source cluster;
- target filament name/color;
- Delta E;
- manual mapping;
- auto match;
- target color count.

#### Cleanup UI

- enable;
- minimum faces/area;
- sharp-edge threshold.

#### Existing paint policy

- preserve existing;
- paint unpainted;
- overwrite inside mask.

#### Diagnostics

- painted faces;
- components;
- regions removed;
- coarse mesh;
- back-surface behavior.

#### Apply/Cancel

- final-quality result required;
- Apply once;
- Cancel produces no mutation;
- clear preview render state.

### Tests

- controller lifecycle;
- rapid drag/cancel;
- stale callbacks;
- delete target;
- reorder filaments;
- apply/undo;
- save/reopen;
- DPI/theme/localization;
- manual platform matrix.

### Exit Gate

A release build completes the flat-logo workflow entirely inside Orca.

## 10. Phase 6 — Production Hardening

### Objective

Prepare the MVP for external users.

### Benchmark Matrix

- 10k/100k/1M faces;
- 1MP/4MP/16MP images;
- 2/4/8/16 colors;
- cleanup on/off;
- preview/final sampling.

Record:

- decode time;
- transform time;
- sample time;
- quantization time;
- cleanup time;
- total;
- peak memory;
- cancellation latency;
- hardware/build type.

### Tasks

- profile and optimize measured bottlenecks;
- TBB parallel loops;
- cache adjacency/transformed vertices;
- image/memory limits;
- cancellation hardening;
- malformed-image testing;
- degenerate/non-manifold mesh tests;
- cross-platform builds;
- high-DPI/dark-light/accessibility;
- localization;
- user guide;
- developer guide;
- provenance review;
- release candidate.

### Exit Gate

No known corruption issue, bounded resources, reliable cancellation, cross-platform builds, complete docs, and passing regression tests.

## 11. Phase 7 — Cylindrical, Spherical, Occlusion

### Cylindrical Projection

For axis `A`, origin `O`, radial basis `R`, tangent `T`:

```text
d = p - O
height = dot(d, A)
radial = d - height*A
theta = atan2(dot(radial,T), dot(radial,R))
u = (theta - theta_start) / wrap_angle
v = 0.5 - height / height_mm
```

Controls:

- axis;
- origin;
- seam angle;
- wrap angle;
- height;
- inside/outside filtering.

### Spherical Projection

```text
direction = normalize(p-center)
longitude = atan2(dot(direction,T), dot(direction,R))
latitude = asin(dot(direction,A))
```

### Occlusion

Implement/test:

- projector depth raster;
- tolerance;
- thin walls;
- concavity;
- overlapping shells;
- optional AABB ray verification.

### Exit Gate

Cup and sphere fixtures receive predictable images without painting hidden/inside surfaces by default.

## 12. Phase 8 — UV Textures and Optional Remeshing

### Objective

Convert textured models and improve boundary resolution with explicit topology change.

### Tasks

- define TexturedMesh;
- investigate current Orca import stack;
- evaluate Assimp only if necessary;
- support UV/material texture mapping;
- adapt Bambu TextureToColor pieces;
- multi-texture atlas;
- adaptive subdivision;
- optional repair;
- replacement-mesh plan type;
- annotation migration report;
- exact transform/source-offset handling;
- reset/migrate stale seam/support/fuzzy/MMU annotations;
- undo and 3MF tests.

### Critical Rule

Topology replacement is never hidden inside a face-state-only plan.

### Exit Gate

Textured conversion is reproducible and topology changes cannot leave stale face-indexed annotations.

## 13. Phase 9 — Safe Plugin Mutation Bridge

### Objective

Expose a narrow validated operation to Orca Python plugins.

### API

```python
orca.host.paint.selected_targets()
orca.host.paint.read_face_states(...)
orca.host.paint.topology_fingerprint(...)
orca.host.paint.apply_face_states(...)
orca.host.paint.clear_face_states(...)
```

### Tasks

- add paint registrar;
- ModelMutation capability;
- NumPy validation;
- stable IDs/topology;
- main-thread requirement;
- undo;
- dirty/slice/render refresh;
- Python exceptions;
- example plugin;
- audit tests.

### Security Tests

- wrong dtype;
- wrong rank;
- wrong length;
- invalid state;
- stale hash;
- deleted target;
- worker-thread call;
- missing capability;
- shutdown/reentrant call;
- exception path.

### Exit Gate

A sample plugin paints safely and invalid/unauthorized input cannot corrupt the project.

## 14. Phase 10 — Release and Upstream

### Deliverables

- versioned fork build;
- installers/artifacts;
- source archive;
- AGPL compliance;
- release notes;
- known limitations;
- user docs;
- issue templates;
- crash triage;
- upstream PR strategy.

### Upstreamable Slices

- topology fingerprint;
- face-state helpers;
- color utility;
- core pipeline;
- gizmo;
- plugin face-paint API.

Smaller upstream PRs are preferred over one giant fork diff.

## 15. Risk Register

| ID | Risk | Impact | Mitigation |
|---|---|---:|---|
| R-001 | upstream painter APIs change | high | isolate adapter and tests |
| R-002 | selector off-by-one | critical | central conversion |
| R-003 | stale plan | critical | IDs + hash + generation |
| R-004 | worker touches live model | critical | immutable snapshots |
| R-005 | 3MF loses paint | critical | Phase 4 gate |
| R-006 | coarse mesh | medium | wireframe/warning |
| R-007 | back-surface paint | high | front-face default, Phase 7 |
| R-008 | noisy colors | medium | cleanup, no MVP dithering |
| R-009 | memory spike | high | limits/benchmarks |
| R-010 | remesh breaks annotations | critical | separate operation/report |
| R-011 | dependency portability | high | defer and CI |
| R-012 | plugin corruption | critical | narrow capability API |
| R-013 | screen/print color mismatch | medium | explicit limitations/manual map |
| R-014 | GUI regression | high | separate gizmo |
| R-015 | lost provenance | high | provenance file and headers |

## 16. First 20 Issues

1. clean Windows build/test baseline;
2. docs/provenance;
3. ImagePaint types/errors;
4. topology fingerprint;
5. mesh validation;
6. bounded image decoder;
7. color-space utilities;
8. planar projector frame;
9. transform tests;
10. bilinear sampler;
11. Gaussian7 sampler;
12. front-facing filter;
13. deterministic quantizer;
14. CIEDE2000;
15. filament snapshot/matcher;
16. adjacency;
17. tiny-region cleaner;
18. end-to-end headless golden test;
19. MMU state read/merge/apply;
20. 3MF round-trip/profile-preservation.

GUI work is blocked until issue 20 is complete.

## 17. Definition of Done

Any task is done only when:

- relevant target compiles;
- tests are added and pass;
- existing failures are disclosed;
- coordinate space is documented;
- thread ownership is documented;
- errors/cancellation are handled;
- undo/persistence impact is addressed;
- no printer-vendor assumption is introduced;
- docs/provenance are updated;
- commands and results are recorded;
- exit-gate evidence is stated.

## 18. AI Planning Template

```markdown
# Goal
# Current Roadmap Phase
# Requirements and Invariants
# Files to Inspect
# Files to Add/Change
# Data Flow
# Coordinate Spaces
# Thread Ownership
# Error and Cancellation Paths
# Tests to Add
# Commands to Run
# Risks
# Non-Goals
# Exit-Gate Evidence
```

## 19. Immediate Next Action

Perform Phase 0, then Phase 1.

The first useful demonstration is not a GUI. It is a deterministic unit test that projects a known image onto a known cube and produces an expected face-state array.

## 20. Agent Surface Track (parallel to GUI phases)

Normative design: [`Agent_Surface.md`](Agent_Surface.md). Decision: ADR-0010 in `DECISIONS.md`.

This track exposes Image Paint (and documents existing Orca slice CLI) for **AI agents and automation**. It does **not** replace Phases 0–6; it depends on the GUI-independent ImagePaint core (already required by ADR-0004).

| Phase | Name | Exit gate (summary) |
|---|---|---|
| **AS-0** | Spec freeze | `Agent_Surface.md` accepted |
| **AS-1** | CLI skeleton | `orcgraffiti info` JSON on fixture |
| **AS-2** | Paint dry-run | `paint --dry-run` report ok |
| **AS-3** | Paint write | 3MF reopens with MMU paint in GUI |
| **AS-4** | Skill | `skills/orcgraffiti-cli/SKILL.md` cookbook works |
| **AS-5** | MCP thin wrap | info/paint tools return report JSON |
| **AS-6** | Slice chapter | skill documents paint→`orca-slicer --slice` |
| **AS-7** | Hardening | CLI tests in CI |

**Rules:**

- No GUI automation.
- CLI paint must not mutate printer/filament profiles (INV-008).
- Prefer new `orcgraffiti` binary for paint/info; keep slice on existing `orca-slicer`.
- MCP/Python wrap CLI; do not fork business logic.

**Implementation status:** deferred until explicitly scheduled. Core unit tests remain the quality gate for algorithms.

The second demonstration is a 3MF round trip that proves the paint survives while printer/process/filament settings remain unchanged.
