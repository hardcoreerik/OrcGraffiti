# 2026-08-12 — Fine-detail paint resolution, no remeshing required

## The ask

After the previous session documented MakerWorld's CDT-remesh technique
as a fundamental MVP limitation, the user pushed back: "this is what we
need to do. Lets go for it." — specifically, per-color-patch triangles
that aren't capped by the source mesh's original triangle boundaries.

## The reframe

Re-reading the constraint more carefully before writing any code: OrcaSlicer's
`TriangleSelector` — the class every paint gizmo already writes into
(`mmu_segmentation_facets`) — already supports recursively subdividing a
face's *paint resolution*, entirely within its own virtual split tree.
The underlying `TriangleMesh` never changes; only how finely
`TriangleSelector` carves up that triangle for the purpose of assigning
colors. This is exactly how seam/support/fuzzy-skin/MMU brush painting
already achieves sub-mesh-resolution detail on any mesh, regardless of its
actual triangle count. Image Paint's `ImagePaintJob::finalize` simply
wasn't using this — it called `set_facet()` once per *original* triangle.

This means "precisely-shaped triangles per color patch" doesn't require
remeshing at all. It requires driving `TriangleSelector`'s existing split
machinery from image data instead of leaving it untouched.

## What was tried and rejected

`TriangleSelector::select_patch()` (the brush-cursor entry point every
interactive gizmo already uses) looked like a zero-new-code option —
until reading its edge-limit derivation: it auto-sets the split threshold
from the cursor radius as `min(radius/5, 0.05mm)`, tuned for close-up
interactive brush precision. Reusing it for a full-image bulk area fill
would have produced an uncontrolled, effectively unbounded triangle count
on any normal-sized painted region — precisely the kind of "passed my own
self-checks, would have failed in the real world" risk this project
already got burned by once during AS-3 (documented in the same
AI_STATUS.md file). Not repeating that mistake mattered more here than
saving an afternoon of writing three new methods.

## What was built instead

Three small, additive public methods on `TriangleSelector`
(`TriangleSelector.hpp/.cpp`), reusing the *same* `split_triangle()`/
`perform_split()` code `select_patch()` already relies on — no new
low-level splitting logic, just a new entry point driven by edge length
instead of cursor shape:

- `subdivide_facet_uniform(facet_idx, max_edge_length)`
- `collect_leaves(facet_idx)` → positions + stable index handles
- `set_leaf_state(leaf_index, state)`

5 new unit tests (`tests/libslic3r/test_triangle_selector.cpp`) prove the
edge-length bound, unrelated-facet isolation, deterministic/repeatable
leaf order across independent instances, and serialize/deserialize
round-tripping.

Pipeline (`ImagePaintPipeline.hpp/.cpp`): new opt-in
`ImagePaintRequest::detail_edge_length_mm` (0 = off, every existing
caller/test unaffected). When set, faces the coarse pass decided to paint
get subdivided and each leaf classified independently against the
already-computed color clusters — on the worker thread, using a throwaway
mesh/selector built from the immutable request snapshot, never a live
`ModelVolume*`. A 250,000-leaf hard cap falls remaining faces back to
flat color rather than risk an unbounded Apply-time hang.

Apply (`ImagePaintJob::finalize`, still UI-thread-only, still one undo
snapshot): replays the identical deterministic split on the live mesh
(cheap — no image resampling needed, the topology fingerprint check
already guarantees the live mesh matches what the worker computed
against) and zips leaves to the plan's recorded states.

GUI: new "Detail" slider (0–2mm, default 0.5mm) in
`GLGizmoImagePainter`'s panel.

## Proof

New test: a red/green split image projected onto the test cube's 2-triangle
top face produces 512 leaves spanning both colors — while the pre-existing
flat `states[]` array can only ever record one color per triangle. Concrete,
tested evidence the capability is real.

## Status

100/100 `[ImagePaint]`/`[TriangleSelector]` Catch2 cases (was 93), full
`ALL_BUILD` clean (GUI DLL + CLI + all suites), 559/559 ctest. Not yet
live-tested by the user in the running GUI — that's the next step.
