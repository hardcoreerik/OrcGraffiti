# OrcGraffiti AI Status

## Current Phase

Phase 6+ mapping quality green. **Agent Surface AS-1/AS-2/AS-4 implemented
and tested; AS-3 write path (`paint --out`) DISABLED — confirmed broken
against real slicer software twice.**
Phase 7: cylindrical/spherical projection math complete AND wired into the
sampling pipeline (FaceSampler/ImagePaintPipeline) — not yet exposed via
any CLI flag or GUI control.
GUI Image Paint gizmo reworked this session to match MakerWorld's Mesh
Graffiti workflow (View preset + Size% + Rotate, camera stays fixed and the
model is moved into position) — see "GUI gizmo rework" section below for
the interaction model AND a hard finding about why per-triangle painting
can't visually match MakerWorld's fine-detail results without violating
the no-remesh MVP invariant.

## Current Branch

feature/image-paint-phase1-types

## Docs just added (2026-08-06)

| Doc | Purpose |
|---|---|
| `docs/OrcGraffiti/Agent_Surface.md` | Full agent/CLI/MCP design (research-backed) |
| `docs/OrcGraffiti/skills/orcgraffiti-cli/SKILL.md` | Agent skill template (binary TBD) |
| `docs/OrcGraffiti/schemas/paint_report.schema.json` | JSON schema sketch for paint reports |
| `DECISIONS.md` ADR-0010 | Hybrid CLI first |
| `Roadmap.md` §20 | AS-0…AS-7 gates |

## Working Build

- ImagePaint unit tests: **93/93** (463 assertions; cylindrical + spherical
  projection math + pipeline integration + all 6 `--view` presets golden-tested)
- Full app: Release `orca-slicer.exe` builds — **GUI DLL rebuilt and
  verified this session** after the projection-variant wiring (touches
  `GLGizmoImagePainter.cpp`), launched for the user to test the Image
  Paint gizmo live
- `orcgraffiti.exe`: `version`, `help`, `info`, `paint --dry-run` — links libslic3r only. **`paint --out` is disabled**, refuses with a clear error.
- ctest: **30/30 green** (22 ImagePaint core + 8 orcgraffiti CLI contract tests)
- Sample image: `C:\Users\hardc\OneDrive\Pictures\garth.jpg`

## Phase 7: cylindrical/spherical projection now wired into the pipeline

`FaceSampler`/`ImagePaintPipeline` previously hardcoded
`PlanarProjectionSettings`. Generalized via a new
`ProjectionSettings = std::variant<Planar, Cylindrical, Spherical>` in
`Projection.hpp`, with dispatch functions (`project()`,
`outward_direction()`, `alpha_threshold()`, etc.) so the sampling loop in
`FaceSampler.cpp` works identically regardless of projection type.

The one genuinely new piece of logic: front-facing testing differs per
type. Planar uses one fixed camera direction; cylindrical/spherical need
the *local* radial-outward direction at each face's centroid, since a
wraparound projector has no single "camera direction." `outward_direction()`
dispatches accordingly, and the 3 new pipeline integration tests
specifically prove this — two faces at different angles around a
synthetic cylinder/sphere both paint correctly, which a single fixed
direction could never achieve for both.

`ImagePaintRequest::projection` changed type, which broke every direct
field-access call site (`req.projection.width_mm = ...` style) across
`orcgraffiti.cpp`, `GLGizmoImagePainter.cpp` (the live GUI gizmo), and the
pipeline test file — all updated to set fields on the concrete
`PlanarProjectionSettings` before assigning into the variant. Verified in
order: full Catch2 suite unchanged (93/93, zero regressions), CLI builds,
**GUI DLL builds** (confirmed via a real incremental rebuild, not
assumed — this is the file the user's live paint-gizmo testing depends
on), then added and passed 3 new end-to-end integration tests.

**Not yet reachable by any user** — no `--view cylindrical` CLI flag, no
GUI gizmo mode selector. This is pipeline plumbing only; exposing it is
separate, later work.

## ⛔ AS-3 write path: DISABLED — two fix attempts each confirmed broken by real testing

`paint --out` is **deliberately disabled** as of 2026-08-11. Do not
re-enable it without either (a) a fix that's actually been verified
against real slicer software, not just this CLI's own tooling, or (b) a
materially different approach. Read the full chronology below before
attempting either — the two bugs already found and fixed were real, but
clearly not the only structural problems.

### AS-3 bug history (2026-08-11, full chronology)

1. **Fixed — `LoadStrategy` bug**, pre-existing since AS-1, affects any
   `.3mf` input: default `LoadStrategy` omits `LoadModel`/`LoadConfig`.
   Fixed via `full_load_strategy()`. *(This fix is fine and stays — it's
   about reading, unrelated to the write-path crashes below.)*
2. **First BBS-writer attempt: reverted.** Empty `<plate>` element for
   non-3MF inputs → reload saw zero objects. Fixed by synthesizing a
   default `PlateData`. Reload then failed differently (`obj_map` lookup)
   — root cause not isolated at the time; reverted.
3. **Scope narrowed to plain `store_3mf` (v1).** Verified geometry/
   fingerprint round-trip, but the user's GUI check failed: "just a cube,"
   mislabeled as Bambu Studio — paint wasn't recognized.
4. **User provided a real reference 3MF** to diff against (a personal
   file, not committed to the repo). Found a real root cause: the
   exporter only writes `<assemble_item>` for instances with an
   initialized assemble transform. Fixed.
5. **Self-verified** (`has_mmu_paint: true` round-trip) and sent
   `gui_check_bbs_v2.3mf` to the user.
6. **User's GUI check: CRASH** in both OrcaSlicer 2.4.2 and FlashForge
   Studio.
7. **Found a second real bug**: `_add_relationships_file_to_archive`
   (`bbs_3mf.cpp:6750`) unconditionally references
   `Metadata/plate_1.png`/`plate_1_small.png` in `_rels/.rels` even when
   no thumbnail was ever written — a dangling OPC package reference,
   confirmed via direct zip inspection (the crashing file's `namelist()`
   had no such PNGs). Fixed by supplying a minimal placeholder
   `ThumbnailData` so the referenced files actually get written.
8. **Self-verified again** (zip inspection confirmed all relationship
   targets now resolve to real files) and sent `gui_check_bbs_v3_fixed.3mf`
   to the user, explicitly asking permission first this time.
9. **User's GUI check: STILL BROKEN.** "no that didnt fix it. it stay
   stuck on this screen" — FlashForge Studio hung indefinitely on a
   "Loading..." dialog (screenshot evidence), rather than crashing
   outright. Different failure mode, same underlying conclusion: not safe.
10. **Disabled `--out` entirely** rather than attempt a third blind fix.
    The pattern across steps 5-9 is the core lesson: this CLI's own
    self-consistency checks (round-trip `has_mmu_paint`, matching
    fingerprints, zip structural inspection) are *necessary* but were
    proven, twice, **not sufficient** evidence that a written file is safe
    to hand to real software. A third attempt without a fundamentally
    different verification method (not just another guess-and-check
    cycle) would be repeating the same mistake.

### What's confirmed to actually work

- Mesh geometry and topology fingerprint round-trip exactly through the
  (disabled) write path — verified structurally, unaffected by the crash/
  hang bugs, which were both about package-level metadata, not the mesh
  itself.
- Reading `.3mf` files (`info`, `paint --dry-run`) works correctly,
  including the `LoadStrategy` fix — this is unrelated to the write-path
  problems and has no open questions.

### If revisiting AS-3's write path in the future

Consider a fundamentally different verification approach before another
attempt — e.g., requiring an actual human GUI test as part of the
acceptance criteria for any fix (not optional, not "self-check passed so
it's probably fine"), or investigating whether the underlying
`store_bbs_3mf`/`_BBS_3MF_Exporter` machinery is even reliably usable
from a freshly-constructed `Model` with no real `Plater`/`PartPlateList`
lifecycle behind it — both bugs found were in package-level metadata that
in the GUI's normal flow is populated by live application state
(`PartPlateList::store_to_3mf_structure`, real thumbnail rendering) that
a headless CLI has no equivalent for. There may be more such gaps.

## GUI gizmo rework: MakerWorld-style View/Size/Rotate + a hard scope finding

User asked for the Image Paint gizmo's interactive workflow to match
MakerWorld's "Mesh Graffiti" tool directly. Two rounds of misreading the
mechanic from screenshots (raycast-click-to-stamp was built and then fully
deleted) were corrected by the user with an actual screen-capture video:
the image stays fixed in the center of the screen; the user moves/rotates
the *model* into position behind it (standard orbit/pan/zoom), adjusts
Size% and Rotation sliders, then clicks Apply.

Implemented (`GLGizmoImagePainter.hpp/.cpp`, `ImagePaint/Projection.hpp/.cpp`):
- `ViewPreset` enum (Front/Back/Left/Right/Top/Bottom) + `view_preset_vectors()`
  in `Projection.hpp/.cpp` — single source of truth shared by the CLI's
  `--view` flag and the GUI's preset buttons, they cannot drift apart.
- Gizmo panel: 6 view-preset buttons, Size% slider (scales the auto-fit
  plane in place, centered), Rotate slider (drives the pre-existing
  `PlanarProjectionSettings::rotation_radians`, previously dead), Apply.
  Old camera-facing auto-fit path preserved as a collapsed "Advanced"
  section rather than deleted.
- Removed entirely: the `MeshRaycaster`-based click-to-stamp mechanic
  (hover cursor sphere, `on_mouse`, `stamp_at`) — built from a
  misunderstanding of the screenshots, fully deleted once the video showed
  the real (non-raycast) mechanic.
- Compiles clean (GUI DLL + CLI), 30/30 ctest, confirmed live via the
  user's own screen recording: buttons highlight, sliders respond, Apply
  fires a job and visibly changes a face color.

### Finding: flat-color Apply result is NOT a bug — it's the no-remesh invariant working as designed

User tested Apply on a low-poly (12-triangle) test cube with `garth.jpg`
and got one flat color block, not recognizable image content: "it colored
a side, but did not place anything resembling the images." Suspected
image vectorization was missing.

Investigated MakerWorld's own client (with the user's authenticated
session, via browser inspection of network requests + fetched JS
bundles — no credentials handled by the agent). Confirmed:

- MakerWorld loads a Go/Rust WASM module explicitly logged as
  `[CDT WASM]`, fetched as `cdt_wasm_bg.wasm` from
  `.../makerlab/content-generator/cdt_wasm_bg.wasm`.
- The surrounding JS bundle
  (`_next/static/chunks/4134a352.*.js`, 1.5MB) has heavy `triangulat`
  (46 hits) and `vectoriz` (36 hits) keyword density alongside the CDT
  module reference.
- Conclusion: MakerWorld vectorizes the uploaded image into contours,
  then runs **Constrained Delaunay Triangulation to insert new mesh edges
  along those contours** — i.e. it retriangulates (remeshes) the surface
  in the painted region so each color patch gets its own precisely-shaped
  triangles, independent of the original mesh's triangle density.

This is a fundamentally different technique from per-existing-triangle
painting, and it directly conflicts with AGENTS.md / Project_Truth.md's
explicit MVP invariant: **"Do not remesh in MVP — no subdivision, repair,
or edge collapse."** Our pipeline correctly refuses to do this. The flat
block on the test cube is the expected, correct output of a no-remesh
per-face painter on a 12-triangle mesh — not a defect in `apply()`,
sampling, or quantization.

**Superseded — user greenlit pursuing per-color-patch detail directly, and
it turned out not to require remeshing at all.** See the next section.

## Fine-detail paint resolution — implemented, no remeshing required

Re-reading MakerWorld's approach more carefully: OrcaSlicer/OrcaSlicer's
own `TriangleSelector` (the class already backing `mmu_segmentation_facets`
for every paint gizmo — seam, support, fuzzy skin, MMU brush) already
supports subdividing a face's *paint resolution* recursively, entirely
within its own virtual split tree — the underlying `TriangleMesh` is never
touched. This is exactly the mechanism every existing brush-painting
gizmo already relies on for fine on-screen detail regardless of the base
mesh's triangle count. Image Paint just wasn't using it — `ImagePaintJob::finalize`
called `TriangleSelector::set_facet()` once per *original* triangle, one
flat color per face, no matter how much detail the source image had.

Investigated whether the existing brush-cursor path (`select_patch()` +
`Sphere`/`Circle` cursor) could be reused for bulk area-fill, and found it
unsuitable: `select_patch()` auto-derives its edge-length limit from
cursor radius (`min(radius/5, 0.05mm)`), which is tuned for interactive
close-up brush precision, not bulk full-image painting — reusing it as-is
risked an uncontrolled, unbounded triangle explosion on any
normal-sized painted area (the same class of risk the AS-3 saga already
burned this project on once; not something to reintroduce carelessly).

Added a small, additive API to `TriangleSelector` instead
(`TriangleSelector.hpp/.cpp`), driven purely by edge length rather than a
brush cursor, reusing the *same* underlying `split_triangle()`/
`perform_split()` machinery `select_patch()` already relies on (so no new
low-level splitting logic, just a new entry point into code already
proven correct):

- `subdivide_facet_uniform(facet_idx, max_edge_length)` — recursively
  splits one original facet until every leaf's longest edge is
  `<= max_edge_length`. Deterministic: same facet + same edge length on a
  fresh facet always yields the same tree shape and leaf order.
- `collect_leaves(facet_idx)` — returns each current leaf's mesh-space
  vertex positions and a stable-until-next-split index handle.
- `set_leaf_state(leaf_index, state)` — colors one leaf independently.

5 new unit tests in `tests/libslic3r/test_triangle_selector.cpp` verify
the edge-length bound holds for every returned leaf, that unrelated
facets are untouched, that `collect_leaves()` order is repeatable across
independent instances (required for the worker/apply-thread split below),
and that per-leaf states round-trip through `serialize()`/`deserialize()`.

**Pipeline wiring** (`ImagePaintPipeline.hpp/.cpp`): new opt-in
`ImagePaintRequest::detail_edge_length_mm` (0 = off, byte-for-byte
unchanged behavior for every pre-existing caller/test). When set, after
the existing coarse per-face pass + cleanup + merge decide *which*
original faces get new paint, each such face is subdivided (worker
thread, using a throwaway `TriangleMesh`/`TriangleSelector` built from the
immutable request snapshot — no live `ModelVolume*` touched) and every
leaf is classified independently against the already-computed color
clusters. Leaf states are recorded in `FacePaintPlan::detail_leaf_states`,
keyed by original face index, in `collect_leaves()`'s deterministic order.
A hard cap (`kMaxDetailLeaves = 250'000` total leaves) falls back
remaining faces to their flat coarse color rather than risk an unbounded
Apply-time hang — this is the same class of safety margin the AS-3
post-mortem argued for, applied proactively this time.

**Apply wiring** (`ImagePaintJob::finalize`, UI thread, unchanged
threading contract — one undo snapshot, worker thread never touches the
live model): for faces with detail leaves, `set_facet(NONE)` resets any
prior split on that face, then `subdivide_facet_uniform()` + `collect_leaves()`
are replayed on the *live* mesh (cheap — pure geometry, no image
resampling needed since the split is deterministic and the topology
fingerprint already guarantees the live mesh matches the snapshot the
worker computed against) and zipped leaf-for-leaf with the plan's
recorded states via `set_leaf_state()`.

**GUI**: new "Detail" slider in `GLGizmoImagePainter`'s panel (0–2mm,
"Off" at 0, default 0.5mm), wired straight to
`req.detail_edge_length_mm`.

**Proof it actually works** — new pipeline test
`run_image_paint detail_edge_length_mm paints two colors onto one
original triangle` (`test_image_paint_pipeline.cpp`): a vertically split
red/green image projected onto the unit-cube fixture's top face (2
triangles) produces 512 leaves spanning *both* colors, while the flat
`states[]` array — the pre-existing ceiling — can only record one color
per triangle (`states[2]==1`, `states[3]==2`, each a single value). This
is the concrete, tested proof that per-color-patch detail finer than the
source mesh's triangle density is now real, without remeshing.

Verified: 100/100 `[ImagePaint]`/`[TriangleSelector]` Catch2 cases pass
(was 93 before this entry — +5 TriangleSelector unit tests, +2 detail
pipeline tests), full `ALL_BUILD` compiles clean (GUI DLL, CLI, all test
suites), 559/559 ctest.

## Baked-mesh architecture plan — PLANNING ONLY, not started

User challenged the "no remeshing" assumption with real evidence (MakerWorld
outputs slice fine in OrcaSlicer, worst case needs Fix Model) and asked for a
full reassessment. Researched three areas via parallel Explore agents:
existing mesh-replacing gizmos (Simplify/MeshBoolean/Cut), existing mesh
repair/validation infra (CGAL, admesh), and undo/redo + 3MF handling of
mesh-topology changes. Finding: OrcaSlicer already has a complete, tested
pattern for exactly this (`set_mesh` + `save_painting`/`restore_painting` +
`take_snapshot` + `changed_mesh`), used by three existing features — the
earlier "too risky" conclusion assumed we'd build that from scratch, which
was wrong. Full 14-point plan, with exact file/class citations, written to
`docs/OrcGraffiti/MeshGraffiti_Bake_Plan.md`. Recommendation: keep this
session's `TriangleSelector` virtual-subdivision work as the interactive
preview layer (Stage A), add a real "bake to mesh" transaction (Stage B)
reusing OrcaSlicer's existing mesh-replace pattern, staged incrementally,
starting with the cheapest option (materialize the already-computed virtual
leaves as real geometry — no new subdivision math needed for v1). Explicitly
NOT started — plan awaits review before any implementation.

**Update — implementation started, per explicit user go-ahead.** User
pushed back further on one point: the "warn if paint remap drops
significant area" safety net (plan §9) was deferred rather than built
proactively — "let's see if this happens before you worry so much about
it," given nearly every MakerWorld output the user has tested has produced
accurate results. Correct call: that check can be added later if real
testing ever shows the problem, not before.

**Stage 1 (plan §12) landed**: `bake_candidate_mesh()` in new
`src/libslic3r/ImagePaint/MeshBake.{hpp,cpp}` materializes a
`FacePaintPlan`'s existing `detail_leaf_states` as real geometry — Option 1
from plan §4 (reuse Stage A's leaves verbatim, no new subdivision math).
Implementation turned out simpler than the plan anticipated: replays the
same deterministic `subdivide_facet_uniform`/`collect_leaves`/`set_leaf_state`
sequence Apply already uses, then exports via
`TriangleSelector::get_facets_strict(state)` — a function that already
triangulates T-junctions correctly across face boundaries (confirmed by
reading its implementation: it walks every original facet's current split
state and stitches boundaries via `get_facets_split_by_tjoints`, the exact
"genuinely difficult" problem plan §8/§13 flagged as having no reusable
general-purpose utility — turns out one exists, scoped to `TriangleSelector`'s
own tree, which is exactly what we're exporting from). No barycentric math
had to be hand-written — edge bisection in `subdivide_facet_uniform`
already guarantees new vertices stay on the original face's plane.

4 new tests (`tests/libslic3r/test_mesh_bake.cpp`): no-detail-leaves
fallback reproduces original topology exactly; detail leaves materialize
as real, `its_num_open_edges() == 0` (still manifold) geometry with both
painted colors present as real triangles; every new vertex lands on one of
the cube's six original planes (shape-preservation proof); empty-mesh
error case. 104/104 `[ImagePaint]`/`[TriangleSelector]`/`[MeshBake]`
Catch2 cases, full `ALL_BUILD` clean.

**Not yet done**: Stage 2 (validate/repair pipeline — `its_num_open_edges`/
`MeshBoolean::cgal::repair()` wiring, currently only exercised implicitly
by the test fixture happening to stay manifold) and Stage 3 (the actual
commit transaction into a live `ModelVolume` — `set_mesh`/`save_painting`/
`restore_painting`/`take_snapshot`/`changed_mesh` — plus GUI "Bake" action).
`bake_candidate_mesh()` is pure, headless, and not wired into
`ImagePaintJob`/`GLGizmoImagePainter` yet — nothing user-visible has
changed in the running app from this update.

## Live GUI testing — mirrored image, uncapped colors/detail found and fixed

User tested the built Detail slider on a real reference model
(`m5tab5`'s textured PEI plate) with `garth.jpg`. Screen recording review
findings:

- **Visual result at Detail=0.11mm/Colors=7 is genuinely good** — "EAT THE
  RICH" text and the face silhouette are clearly legible on the real mesh,
  a full turnaround from the earlier flat-block result. Confirms the
  fine-detail subdivision work is doing its job on real geometry, not just
  the unit-cube test fixture.
- **Image came out mirrored** on a side view. Fixed: `GLGizmoImagePainter`
  now has a "Flip" checkbox wired to the already-existing (previously
  unexposed) `PlanarProjectionSettings::mirror_u`.
- **Colors input was uncapped at a flat 16**, independent of the actual
  printer. Fixed: now clamped to the selected printer's real extruder
  count via `get_extruder_colors_from_plater_config()` — same source of
  truth `submit_paint_request()` already uses to build the filament list.
- **Real, serious problem found at the Preview/slicing stage**: at
  Detail=0.11mm the slice produced **254 tool changes** and threw `A
  G-code path goes beyond plate boundaries`. Investigated the root cause
  (see below) rather than guess-patching it.

### Root cause: pre-existing OrcaSlicer wipe-tower sizing gap, newly exposed

Confirmed via code investigation, not assumption: `PartPlate::estimate_wipe_tower_size()`
sizes/positions the wipe tower ONCE, early, from `plate_extruder_size`
(how many distinct filaments are used anywhere on the plate) — it has no
idea how many tool changes any single layer will need. The REAL depth is
computed later, per-layer, inside `WipeTower::plan_tower_new()`
(`update_all_layer_depth`), and grows **linearly and unboundedly** with
that layer's tool-change count. Nothing re-validates or re-clamps the
tower's placement after that real number is known. A thin, finely-painted
texture concentrates hundreds of tool changes onto just a few top layers —
far more than any prior coarse painting tool could ever produce — so the
real per-layer wipe-tower depth blows past the early estimate, and the
resulting tower geometry can extend past the bed edge on that layer. The
post-slice `BuildVolume::all_paths_inside()` check correctly (if
unhelpfully — it doesn't say "it's the wipe tower") flags this as the
boundary error. **Not a bug in our diff** — this estimate-then-clamp-once
architecture predates Image Paint; our feature is just the first thing
capable of generating tool-change densities extreme enough to hit it.
Rewriting OrcaSlicer's wipe-tower placement/validation is out of scope
right now.

### Fix: prevent the pathological input rather than chase the symptom

Added a **Detail floor tied to the selected printer's nozzle diameter**
(`ConfigOptionFloats "nozzle_diameter"`, same access pattern
`GLGizmoBrimEars.cpp` already uses) — detail finer than the nozzle can
physically resolve only multiplies tool changes and print time without
adding anything visible, so the slider now snaps any nonzero value below
that floor back up to it (0 = Off remains reachable as a distinct
sentinel). This directly targets the mechanism that produced 254 tool
changes; per user's explicit call, the "warn if paint remap drops
significant area" safety net from the bake plan remains deliberately
un-built until real testing shows it's actually needed — same reasoning
applies here: fix the concrete problem that was actually observed, not a
hypothetical one.

Not yet addressed: even at a nozzle-diameter floor, a busy image can still
produce a lot of small same-color regions. Bake plan §4 Option 2 (merge
adjacent same-color leaves) becomes more clearly worth prioritizing given
this real-world evidence — noted, not started.

GUI DLL rebuilt, full `ALL_BUILD` clean, 104/104 ImagePaint tests still
green (no core pipeline logic touched, only gizmo-side UI clamping).

## Bake Stage 2 landed: validate/repair pipeline

`validate_baked_mesh()` added to `MeshBake.hpp/.cpp` — takes
`bake_candidate_mesh()`'s output and, before any commit is attempted:

1. Drops zero-area triangles, **in lockstep with `triangle_states`** (a
   hand-rolled filter, not `its_remove_degenerate_faces()`, specifically
   because that function doesn't expose which indices it dropped — would
   have silently desynced geometry from color).
2. Checks manifoldness via `its_num_open_edges() == 0`.
3. Checks self-intersection via `MeshBoolean::cgal::does_self_intersect()`.
4. Returns a new `ImagePaintErrorCode::BakeInvalidGeometry` if either check
   still fails.

**Deliberately did not wire in automatic CGAL repair** (plan §8 step 4,
`MeshBoolean::cgal::repair()`), despite it being called out as available
infrastructure. Reason found while implementing, not anticipated in the
plan: `repair()`'s pipeline does a boolean self-union to keep only the
outer shell — it does not preserve a stable per-triangle correspondence
with its input. There is currently no safe way to carry `triangle_states`
through it without risking triangles ending up with the WRONG color,
which would be a silent correctness bug, strictly worse than a loud
failure. Since `bake_candidate_mesh()` already extracts geometry through
`TriangleSelector::get_facets_strict()` — the same call every other paint
gizmo's export path relies on to produce manifold, T-junction-free output
from a manifold input — hitting this validation failure in practice is
expected to be rare. Documented as a real gap: revisit only with an actual
color-preserving remap design (something like `remap_painting`'s
spatial-overlap approach, but applied to raw per-triangle state), not by
blindly calling `repair()`.

3 new tests: valid mesh passes through unchanged; a degenerate triangle
with a distinctive bogus color gets dropped and does NOT leak into the
result (the specific desync bug the lockstep filter exists to prevent);
a deliberately non-manifold mesh (one triangle removed from a closed cube)
is correctly rejected with `BakeInvalidGeometry`. 107/107
ImagePaint+TriangleSelector+MeshBake cases, full `ALL_BUILD` clean.

Stage 3 (the actual commit transaction into a live `ModelVolume`) is next
and not yet started.

## Detail floor reverted — was overreach

User tested the previous fixes and reported: (a) results looked flat/
untextured in several frames of a new recording, and (b) the "Advanced:
camera-facing projection" legacy path showed inverted colors and poor
detail. Explicit instruction: revert the nozzle-diameter hard floor on
Detail — "there are things that users could do that would allow lower
settings, that may not be apparent in orca printer presets/settings."
Correct call: nozzle diameter is a reasonable *rule of thumb*, not a
reliable ceiling on what a given real-world setup can actually resolve
(calibrated flow, unusual nozzles, techniques the preset system doesn't
capture), and it wasn't this control's place to enforce it. Reverted the
hard clamp; the nozzle-diameter figure is now tooltip guidance only, never
blocks the slider.

Investigated whether the flat-block symptom was caused by the mirror
toggle added alongside the (now-reverted) floor: read
`apply_rotation_mirror()`'s implementation directly — `if (mirror_u) u =
-u;` — confirmed mathematically inert when the Flip checkbox is
unchecked (the default), so this is very unlikely to be a regression from
that change. Most likely explanation not yet confirmed: the on-screen 3D
viewport camera is independent of the chosen View preset button — the
preset only changes the *projection* direction, not where the user is
currently looking — so a screen recording can easily show the model from
a face that wasn't the one just painted, which reads as "nothing
happened" without being a bug. Possible real improvement flagged, not yet
built: auto-orient the viewport camera to match the selected View preset
on Apply, which would remove this whole class of confusion regardless of
whether it's the actual cause here.

The "Advanced camera-facing projection inverted colors" report is
**not yet investigated** — that legacy path wasn't touched by any recent
change; flagged as a real, separate report to chase next, not guessed at.

## Removed the "no-remesh" constraint per explicit user direction — new CDT remesh core landed

User explicitly authorized dropping the MVP no-remesh guardrail for a new,
separate exploration: "Let's build an additional gizmo that explores
remeshing the same way Mesh Graffiti does it." Pushback received and
applied: don't self-limit to "what's already built into OrcaSlicer" out of
triangle-count worry — bring in whatever's actually needed. In practice
what was needed already existed in-tree (not reused out of caution, but
because it's genuinely the right tool): `Slic3r::Triangulation` (CGAL
`Constrained_Delaunay_triangulation_2`, the same tool the Emboss/SVG tools
use) and `marchsq` (marching squares contour extraction, used by SLA raster
tooling) — both proven, neither written for this project.

**New: `src/libslic3r/ImagePaint/MeshRemesh.{hpp,cpp}` —
`remesh_by_color_boundary()`.** Per candidate face: samples a local NxN
grid in barycentric space, classifies each sample against the same color
clusters the rest of the pipeline uses, runs marching squares per cluster
to trace that cluster's boundary within the local grid, feeds the traced
rings as constraint edges into a constrained Delaunay triangulation, then
lifts every resulting triangle back to 3D via barycentric interpolation —
so, like `bake_candidate_mesh()`, every new vertex stays exactly on the
original surface. Unlike the uniform-grid bake path (Option 1), triangle
edges now actually follow the image's real color boundaries instead of a
regular grid.

### A real, reproducible crash found and fixed — not a hypothetical

Initial version crashed with SIGSEGV roughly 1 run in 3 (found by testing
repeatedly, not by inspection). Root cause: the outer triangle boundary
was added as an *explicit* CDT constraint, which overlaps-but-doesn't-
coincide with a cluster's own traced boundary wherever that cluster
touches the triangle edge — the common case, not a rare one. CGAL's
`Exact_predicates_tag` CDT requires constrained edges to never properly
cross except at shared endpoints; violating that is undefined behavior,
not a catchable exception, hence the non-deterministic crash rather than a
clean error. Fix: keep the 3 corners as points (still guarantees the
correct hull boundary, since Delaunay triangulation of a point set always
includes its convex-hull edges, and these 3 corners are always extreme
points of the local grid's u+v<=1 domain) but drop the redundant explicit
boundary-edge constraints — cluster contours alone now correctly bound the
domain. Also added an explicit pre-triangulation self-intersection check
(`get_intersections()`, the same helper `Triangulation.cpp`'s own
debug-only `assert` uses) so any future precondition violation degrades to
a safe per-face fallback instead of undefined behavior in a Release build,
where the assert is compiled out. 25/25 repeated runs clean after the fix,
versus crashing roughly 1 in 3 before.

### Known v1 limitation — tested, not just documented

Contour crossing points along a mesh edge shared by two faces are computed
independently by each face's own local grid, so adjacent faces aren't
guaranteed to agree exactly where a color boundary crosses that shared
edge. Piped the actual output through the existing `validate_baked_mesh()`
(Stage 2, already built) rather than assume: on the test fixture (color
boundary crossing the cube top face's diagonal shared edge) validation
sometimes accepts, sometimes correctly rejects with
`BakeInvalidGeometry` — never crashes, never silently commits a gap. Test
asserts on that actual, tested behavior rather than an assumption.
Documented in `MeshRemesh.hpp` as a real limitation to revisit (e.g. an
edge-crossing-point cache shared between adjacent faces), not treated as
disqualifying — the safety net already catches it.

**Not yet done**: no GUI gizmo wired up yet — `remesh_by_color_boundary()`
is pure, headless library code, tested directly, exactly like
`bake_candidate_mesh()` was before its own gizmo integration. Planar
projection only. 112/112 ImagePaint+TriangleSelector+MeshBake+MeshRemesh
tests green.

## Viewport camera now follows the View preset button

Built the concrete fix for the likely "looking at the wrong face" cause
above: each View preset button now also calls the existing
`GLCanvas3D::select_view(direction)` (the same function the toolbar's own
view-cube buttons use) with the matching world-axis direction string
(`Front→"front"`, `Back→"rear"`, `Left→"left"`, `Right→"right"`,
`Top→"top"`, `Bottom→"bottom"`). Viewport-only — does not touch the paint
math, which already resolves the projection frame in the volume's own
local space independent of camera orientation; this just means the user
is now actually looking at the face they just told the tool to paint.
GUI DLL rebuilt, full `ALL_BUILD` clean.

## "Too many constraints" — Size% rebased, coverage cutoff relaxed

User confirmed the hypothesis by testing: dropping Size to ~25-30% on the
large panel produced the correct, legible result; anything much smaller
made the image disappear, anything bigger went solid black. Root cause
confirmed exactly as suspected — Size 100% meant "cover the whole mesh
silhouette" (`fit_planar_projection` margin 1.02), and the actual usable
window sat in a narrow band near the bottom of a 1-100 slider. User's
framing: "seems like we have too many constraints" — right call, this was
a UX defect, not a tuning question for the user to fight through.

Two fixes in `build_view_preset_projection()`:

1. **Rebased what "100%" means.** New `kSizeReferenceScale = 0.30`
   multiplies the raw full-coverage fit before the user's Size% is
   applied, so the *default* (100%) lands where testing showed it
   actually works, instead of requiring users to hunt near the bottom of
   the range. Slider's range widened to 1-300% (`kSizeSliderMax`) so
   covering the whole object is still reachable for anyone who
   deliberately wants that — nothing lost, just re-centered.
2. **Lowered `minimum_coverage` from 0.25 to 0.05**, in both the
   View-preset and legacy camera-facing paths. 0.25 excluded a face
   *outright* once less than a quarter of it was covered by the image —
   which is why shrinking Size made the result vanish abruptly rather
   than fade out. 0.05 lets partial/edge coverage still paint whatever
   fraction of the image actually lands there.

GUI DLL rebuilt, full `ALL_BUILD` clean. Not yet re-tested by the user —
next recording should show a much wider comfortable range around the
default instead of a razor-thin sweet spot.

## Next Three Tasks

1. Waiting on the user's live GUI test of the Image Paint gizmo (launched
   this session, current build) — this is the real MVP verification that
   hasn't happened yet, separate from the (disabled) CLI write path.
2. Cylindrical/spherical projection is wired into the pipeline but has no
   user-facing entry point yet — a `--view` extension or new flag for the
   CLI, or a gizmo mode selector for the GUI, would be the next step to
   make it actually usable. Occlusion (same Roadmap §11 section) still
   needs design work before implementation — the exit gate ("predictable
   images without painting hidden surfaces") depends on it for cup/sphere
   fixtures to mean anything.
3. AS-5 (MCP thin wrap) remains blocked behind AS-3 per Roadmap.md §20's
   own gate rule — AS-3 is explicitly disabled, not just unverified.

## Open PRs

https://github.com/hardcoreerik/OrcGraffiti/pull/1

## Updated

2026-08-11 — **AS-3 `paint --out` disabled** after a second fix attempt
was also confirmed broken by direct human testing (FlashForge Studio hung
indefinitely, after the first attempt crashed both OrcaSlicer 2.4.2 and
FlashForge Studio outright). Both underlying bugs found (missing
`<assemble_item>`, dangling thumbnail relationship) were real and are
documented for any future attempt, but self-consistency checks proved
insufficient twice — `--out` now refuses cleanly with an explanatory error
rather than risk a third silent failure.

Also this session: golden tests locking in all 6 `--view` presets
(previously only front/top); Phase 7's cylindrical AND spherical
projection math; then **wired both into the sampling pipeline**
(`FaceSampler`/`ImagePaintPipeline`), verified against a real GUI DLL
rebuild since it touches the live paint gizmo, with 3 new end-to-end
integration tests. GUI launched for the user to test live — result
pending. 93/93 ImagePaint test cases, 30/30 ctest. AS-4 (skill file) and
the `LoadStrategy` fix (unaffected by the AS-3 issues above, reading works
fine) landed earlier this session; v0.1.0-alpha tagged and released on
GitHub.
