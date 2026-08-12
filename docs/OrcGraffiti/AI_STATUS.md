# OrcGraffiti AI Status

## Current Phase

Phase 6+ mapping quality green. **Agent Surface AS-1 and AS-2 implemented and tested.**

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
- `orcgraffiti.exe`: `version`, `help`, `info`, `paint --dry-run` — links libslic3r only
- ctest: 19/19 green (15 ImagePaint core + 4 orcgraffiti CLI contract tests)
- Sample image: `C:\Users\hardc\OneDrive\Pictures\garth.jpg`

## Active Task

AS-3: `orcgraffiti paint --out <path.3mf>` — write mode. Currently rejected
with a clear "not yet implemented" error (see `cmd_paint` in
`src/orcgraffiti_cli/orcgraffiti.cpp`).

**Technical plan for AS-3** (scoped out this loop — higher risk, needs care):
1. `Model::read_from_file` must be called with real `DynamicPrintConfig*`,
   `ConfigSubstitutionContext*`, `PlateDataPtrs*`, `vector<Preset*>*` — not
   the current `nullptr` defaults — so the loaded project's config/plates
   can be handed back to `store_bbs_3mf` unchanged (INV-008: no printer/
   profile mutation).
2. Reuse `ImagePaintJob::finalize`'s apply pattern
   (`src/slic3r/GUI/Jobs/ImagePaintJob.cpp:69-82`): build a
   `TriangleSelector` from the target mesh, `deserialize()` existing
   `mmu_segmentation_facets` first (preserve paint outside the image
   footprint), overlay non-`kStateNone` plan states via `set_facet`, then
   `vol->mmu_segmentation_facets.set(selector)`.
3. Refuse `--out` == input path unless `--allow-in-place` (§11.1); require
   `--force` to overwrite an existing `--out`.
4. Call `store_bbs_3mf(StoreParams&)` with the model, the config/plate data
   loaded in step 1, and `SaveStrategy::Zip64`.
5. `release_PlateData_list(plate_data)` before returning (raw-pointer
   ownership — verify no double-free against the loaded `Model`).
6. **AS-3's true exit gate is a human GUI reopen proof** — the CLI can
   self-verify round-trip via its own `info` command (fingerprint/triangle
   count/`has_mmu_paint` unchanged after write), but that is a proxy, not
   the gate itself. Flag this clearly when AS-3 lands.

## Next Three Tasks

1. AS-3 paint write path per the technical plan above
2. Once AS-3 lands: ask for a manual GUI reopen check on a real 3MF with
   `--out` paint applied
3. Keep PR #1 updated; GUI retest auto-fit paint with garth.jpg when convenient

## Open PRs

https://github.com/hardcoreerik/OrcGraffiti/pull/1

## Updated

2026-08-11 — AS-2 `paint --dry-run` landed (full pipeline wired: decode,
fit_planar_projection, run_image_paint, diagnostics/matches report) plus 4
ctest CLI contract tests. AS-1 (`info`) also landed this session.
v0.1.0-alpha tagged and released on GitHub earlier this session.
