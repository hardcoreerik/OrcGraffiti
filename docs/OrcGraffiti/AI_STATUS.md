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

**Decision (proceeding under existing MVP scope, not a new phase):**
document this as a known, by-design MVP limitation — Image Paint quality
is bounded by triangle density in the painted region; works acceptably on
denser/organic meshes, poorly on primitives like test cubes/cylinders.
A CDT-based "remesh for paint" mode is a legitimate idea for a **post-MVP
phase** (new topology, undo, and 3MF implications — real scope, not a
quick patch) but is explicitly out of scope right now per the existing
invariant, and has not been started.

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
