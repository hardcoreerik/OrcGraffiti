# OrcGraffiti AI Status

## Current Phase

Phase 6+ mapping quality green. **Agent Surface AS-1 through AS-4 implemented and tested.**
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

- ImagePaint unit tests: **86/86** (412 assertions; cylindrical + spherical projection, 17 new)
- Full app: Release `orca-slicer.exe` builds
- `orcgraffiti.exe`: `version`, `help`, `info`, `paint --dry-run`, `paint --out` — links libslic3r only
- ctest: **24/24 green** (15 ImagePaint core + 9 orcgraffiti CLI contract tests)
- Sample image: `C:\Users\hardc\OneDrive\Pictures\garth.jpg`

## ⚠️ AS-3 write path: CRASH FOUND AND FIXED — re-verification needed before trusting again

`paint --out` produced a file that **crashed the user's OrcaSlicer 2.4.2
and FlashForge Studio on open**, reported directly after they tested
`gui_check_bbs_v2.3mf`. Root cause found and a fix has been built and
tested by this CLI's own tooling — **but the fix has NOT yet been
re-verified by a human opening a file in real slicer software.** Do not
generate and hand off another `--out` file without explicit user
permission — they already hit one crash from this feature.

### The crash and its fix (2026-08-11)

`_BBS_3MF_Exporter::_add_relationships_file_to_archive` (`bbs_3mf.cpp:6750`)
unconditionally writes `_rels/.rels` entries referencing
`Metadata/plate_1.png` and `Metadata/plate_1_small.png` whenever no
thumbnail path was set in its internal `PackingTemporaryData` — it does
NOT skip the relationship when there's no file to back it. Since
`orcgraffiti` never supplied `StoreParams::thumbnail_data`, the written
archive had `Relationship` entries pointing at files that were never
actually written into the zip — a dangling OPC package reference.
Confirmed via direct zip inspection (Python `zipfile`): the crashing
file's `namelist()` had no `plate_1*.png` despite `_rels/.rels`
referencing them by exact path.

**Fix:** populate `StoreParams::thumbnail_data` with a minimal valid
16×16 white `ThumbnailData` before calling `store_bbs_3mf`.
`_add_thumbnail_file_to_archive` (miniz-based PNG encoder, no wx/GUI
dependency) then actually writes `Metadata/plate_1.png` and an
auto-generated `_small.png` companion, so every relationship target
exists. Re-inspected the written zip after the fix: both PNGs present
with valid signatures (87 and 143 bytes respectively), all 4 relationship
entries resolve to real files. `has_mmu_paint` round-trip and 24/24 ctest
still pass.

**What this fix does NOT prove:** that the file no longer crashes real
slicer software. This CLI's own reader/writer pair agreeing with each
other was also true of the *previous, crashing* version — self-consistency
is not the same as external validity. The next step is asking the user if
they're willing to test again, not assuming this is resolved.

### AS-3 bug history (2026-08-11, full chronology — read before touching the writer again)

1. **Fixed — `LoadStrategy` bug**, pre-existing since AS-1, affects any
   `.3mf` input: default `LoadStrategy` omits `LoadModel`/`LoadConfig`.
   Fixed via `full_load_strategy()`.
2. **First BBS-writer attempt: reverted.** Empty `<plate>` element for
   non-3MF inputs → reload saw zero objects. Fixed by synthesizing a
   default `PlateData`. Reload then failed differently (`obj_map` lookup)
   — root cause not isolated at the time; reverted.
3. **Scope narrowed to plain `store_3mf` (v1).** Verified geometry/
   fingerprint round-trip, but the user's GUI check failed: "just a cube,"
   mislabeled as Bambu Studio.
4. **User provided a real reference 3MF** to diff against (a personal
   file, not committed to the repo). Found the actual root cause: the
   exporter only writes `<assemble_item>` for instances with an
   initialized assemble transform.
5. **Fixed and self-verified** — `has_mmu_paint: true` round-trip. Sent
   `gui_check_bbs_v2.3mf` to the user.
6. **User's GUI check: CRASH** in both OrcaSlicer 2.4.2 and FlashForge
   Studio. Immediately reverted the "this is fixed" framing and
   investigated rather than sending a third blind attempt.
7. **Found and fixed the dangling-thumbnail-relationship bug** (this
   entry) — see above. **Not yet re-verified by a human.**

**Still unverified beyond the crash-fix status above:** only tested
against a single-object, single-instance, single-volume model
(`unit_cube.stl`). Multi-object/instance/plate inputs are unexplored.

## Next Three Tasks

1. **Ask the user if they're willing to test the crash fix** — do not
   generate and hand off a file without that explicit go-ahead. If they
   decline, leave AS-3 documented as "self-consistent but not
   human-verified" rather than claiming it works.
2. If confirmed working: AS-3 is genuinely done; consider AS-5.
3. Otherwise: continue Phase 7 — both cylindrical and spherical projection
   math are landed and tested; next is wiring either into
   `FaceSampler`/`ImagePaintPipeline`, or occlusion (same roadmap section).

## Open PRs

https://github.com/hardcoreerik/OrcGraffiti/pull/1

## Updated

2026-08-11 — **AS-3 crash found and fixed**: a file `paint --out` wrote
crashed the user's OrcaSlicer 2.4.2 and FlashForge Studio (dangling 3MF
package relationships referencing thumbnail files that were never
written). Fixed by supplying a minimal placeholder thumbnail so every
relationship target exists. Human re-verification still needed before
trusting this — see the warning banner above. Also landed Phase 7's
cylindrical AND spherical projection math (17 new tests, 86/86 passing),
not yet wired into the sampling pipeline. AS-4 (skill file) and the
`LoadStrategy` fix landed earlier this session; v0.1.0-alpha tagged and
released on GitHub.
