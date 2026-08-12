# OrcGraffiti AI Status

## Current Phase

Phase 6+ mapping quality green. **Agent Surface AS-1, AS-2, AS-3 (v1 scope) implemented and tested.**

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

- ImagePaint unit tests: **69/69** (incl. garth.jpg integration when present)
- Full app: Release `orca-slicer.exe` builds
- `orcgraffiti.exe`: `version`, `help`, `info`, `paint --dry-run`, `paint --out` — links libslic3r only
- ctest: **21/21 green** (15 ImagePaint core + 6 orcgraffiti CLI contract tests)
- Sample image: `C:\Users\hardc\OneDrive\Pictures\garth.jpg`

## Active Task

AS-3 landed at **v1 scope** (user-decided, not autonomous): `paint --out`
writes a plain (non-BBS) 3MF via `Slic3r::store_3mf`, not the BBS-native
`store_bbs_3mf`. Next candidate work is AS-4 (skill file) or revisiting
AS-3's BBS-format gap — see "Known limitation" below before starting either.

**Two bugs found and resolved this session (2026-08-11):**

1. **Fixed — `LoadStrategy` bug, pre-existing since AS-1, affects ANY `.3mf`
   input:** `Model::read_from_file`'s default `LoadStrategy`
   (`AddDefaultInstances` alone) omits `LoadModel`/`LoadConfig`, so the BBS
   3MF importer silently returns zero objects for `.3mf` inputs — no error,
   just `"The supplied file couldn't be read because it's empty."` Fixed
   via `full_load_strategy()` (matches `OrcaSlicer.cpp`'s own CLI flags),
   used by both `info` and `paint`. Locked in by
   `orcgraffiti_cli_info_3mf_fixture` in `src/CMakeLists.txt`, against
   `tests/orcgraffiti_cli/fixtures/sample_project.3mf`.
2. **Not fixed — BBS 3MF writer (`store_bbs_3mf`) round-trip bug:**
   attempted in an earlier pass this session, found two issues (missing
   `<plate>` element for non-3MF inputs; then an unresolved
   `_BBS_3MF_Importer::_load_model_from_file` `obj_map` lookup failure even
   after fixing the first). **User decided to narrow AS-3 v1 scope** rather
   than keep debugging: write plain (non-BBS) 3MF via `store_3mf` instead.
   The BBS-format writer path remains unimplemented; if it's ever needed
   (e.g. to preserve BBS/Orca project profile/plate data on `--out`), the
   investigation notes are preserved in git history
   (`948a91071f` "docs: AS-3 investigation findings").

**Known limitation of the v1 scope (document, don't silently accept):**
`store_3mf` writes the Prusa/Slic3r-family `slic3rpe:mmu_segmentation`
triangle attribute. This CLI's own `info`/`paint --dry-run` always read
`.3mf` via `Model::read_from_file` → `load_bbs_3mf`, which only recognizes
the BBS-native `paint_color` attribute — so `has_mmu_paint` reads back
`false` on a CLI-painted `--out` file even though the attribute is present
(hand-verified in the raw zip: `triangle ... slic3rpe:mmu_segmentation="0C"`).
**Tried switching the CLI's own reader to `Model::read_from_archive`** (the
real GUI "Open Project" path in `Plater.cpp:6897`, which does Prusa/generic-
3MF detection via `PrusaFileParser::check_3mf_from_prusa` and would read
this attribute correctly) **to fix the self-check — it segfaults**, even on
a known-good pre-existing fixture, before and after any of our changes.
Reverted that attempt immediately. **A manual GUI reopen check is currently
the only way to confirm a `--out` file actually shows paint.** Mesh
geometry and topology fingerprint DO round-trip exactly — verified via
`info` on the CLI's own output matching the input STL's fingerprint
bit-for-bit.

## Next Three Tasks

1. Ask for a manual GUI reopen check on a real `paint --out` file (e.g.
   `orcgraffiti paint tests/orcgraffiti_cli/fixtures/unit_cube.stl --image
   <path> --filaments tests/orcgraffiti_cli/fixtures/filaments.json --out
   /tmp/test.3mf`, then open `/tmp/test.3mf` in Orca) to confirm the
   `slic3rpe:mmu_segmentation` paint is actually visible — this is AS-3's
   real exit gate, not the CLI's own (currently broken) self-check.
2. AS-4: write `docs/OrcGraffiti/skills/orcgraffiti-cli/SKILL.md`'s real
   content (currently a template) now that `info`/`paint --dry-run`/`paint
   --out` all exist and are tested.
3. Keep PR #1 updated; GUI retest auto-fit paint with garth.jpg when convenient

## Open PRs

https://github.com/hardcoreerik/OrcGraffiti/pull/1

## Updated

2026-08-11 — Fixed a `LoadStrategy` bug affecting all `.3mf` reads (pre-
existing since AS-1). Landed AS-3 at v1 scope (plain 3MF write via
`store_3mf`, user-decided after the BBS writer proved buggy) — geometry/
fingerprint round-trip verified; paint-attribute self-check remains broken
(documented) pending a `read_from_archive` segfault fix or a manual GUI
check. v0.1.0-alpha tagged and released on GitHub earlier this session.
