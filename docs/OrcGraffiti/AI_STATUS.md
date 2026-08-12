# OrcGraffiti AI Status

## Current Phase

Phase 6+ mapping quality green. **Agent Surface AS-1/AS-2/AS-4 implemented
and tested; AS-3 write path (`paint --out`) DISABLED — confirmed broken
against real slicer software twice.**
Phase 7 (cylindrical/spherical projection) math complete for both projections.

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

- ImagePaint unit tests: **90/90** (444 assertions; cylindrical + spherical
  projection + all 6 `--view` presets now golden-tested)
- Full app: Release `orca-slicer.exe` builds
- `orcgraffiti.exe`: `version`, `help`, `info`, `paint --dry-run` — links libslic3r only. **`paint --out` is disabled**, refuses with a clear error.
- ctest: **27/27 green** (19 ImagePaint core + 8 orcgraffiti CLI contract tests)
- Sample image: `C:\Users\hardc\OneDrive\Pictures\garth.jpg`

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

## Next Three Tasks

1. Wiring cylindrical/spherical projection into `FaceSampler`/
   `ImagePaintPipeline` was scoped and deliberately deferred this session:
   it requires changing `ImagePaintRequest::projection`'s type (currently
   concrete `PlanarProjectionSettings`) to a variant, which breaks direct
   field-access call sites in `GLGizmoImagePainter.cpp` (GUI, 4 lines),
   `orcgraffiti.cpp`, and both pipeline/projection test files (~10+ call
   sites total) — feasible (a full GUI DLL rebuild is available to verify
   against, `OrcaSlicer.dll` already builds in this env) but is real,
   bigger work deserving its own focused pass, not a tack-on. Also: with
   no CLI flag or GUI control yet exposing curved projection selection,
   wiring it in now has no reachable consumer — arguably premature until
   occlusion (same roadmap section) is at least scoped, since the Phase 7
   exit gate ("predictable images without painting hidden surfaces") needs
   occlusion to mean anything for a cup/sphere fixture.
2. AS-5 (MCP thin wrap) remains blocked behind AS-3 per Roadmap.md §20's
   own gate rule ("Do not implement AS-5 before AS-3") — and AS-3 is now
   explicitly disabled, not just unverified, so this is further blocked
   than before.
3. Keep PR #1 updated; GUI retest auto-fit paint with garth.jpg when convenient.

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
rather than risk a third silent failure. Also landed Phase 7's cylindrical
AND spherical projection math (not yet wired into the sampling pipeline —
deliberately deferred, see "Next Three Tasks"), plus golden tests locking
in all 6 `--view` presets (previously only front/top were tested; back/
left/right/bottom were "documented by inspection" per multiple prior
devlogs). 90/90 ImagePaint test cases, 27/27 ctest. AS-4 (skill file) and
the `LoadStrategy` fix (unaffected by the AS-3 issues above, reading works
fine) landed earlier this session; v0.1.0-alpha tagged and released on
GitHub.
