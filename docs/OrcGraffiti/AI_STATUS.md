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

AS-3: `orcgraffiti paint --out <path.3mf>` — write mode. **Attempted and
reverted this loop** — the naive implementation (steps 1-5 of the prior
plan) built and ran without crashing, but produces 3MF files that
`load_bbs_3mf` cannot reopen. Root cause is only partly isolated; see
findings below. Currently still rejected with a clear "not yet implemented"
error (see `cmd_paint` in `src/orcgraffiti_cli/orcgraffiti.cpp`) — reverted
rather than shipped broken.

**AS-3 investigation findings (2026-08-11, this loop):**

1. `Model::read_from_file` with real `DynamicPrintConfig*` /
   `ConfigSubstitutionContext*` / `PlateDataPtrs*` / `vector<Preset*>*` and
   `store_bbs_3mf(StoreParams&)` with `SaveStrategy::Zip64` do run without
   crashing, and the `paint_color` attributes land correctly in
   `3D/3dmodel.model` (verified by hand-inspecting the written zip).
2. **Bug A (confirmed, understood):** for non-3MF inputs (STL/OBJ), the
   loaded `PlateDataPtrs` is empty (no BBS plate structure exists in an
   STL). Passing an empty `plate_data_list` straight to `store_bbs_3mf`
   writes a `Metadata/model_settings.config` with **no `<plate>` element at
   all**. `load_bbs_3mf` requires a `<plate>` entry listing
   object/instance indices to populate `model.objects` on reopen — without
   one, the reload sees zero objects and reports
   `"The supplied file couldn't be read because it's empty."`
   **Fix attempted:** synthesize a single default `PlateData(0, {(object
   array-index, instance array-index)...}, false)` when `plate_data` is
   empty before calling `store_bbs_3mf`. `PartPlate.cpp:2755-2759`
   confirms `objects_and_instances` pairs are `model.objects[]` array
   indices, not `ObjectID`s — the synthesized plate matched this
   convention.
3. **Bug B (reproduced, NOT isolated):** even with Bug A's fix — a
   correctly-shaped `<plate>` element written, with
   `model_instance/object_id` matching the actual XML resource id used by
   `<build><item objectid="…">` (verified by hand-inspecting the zip a
   second time after the fix) — reload **still** fails, now with
   `"can not find object from plate's obj_map, id=2, skip this object"`
   from `_BBS_3MF_Importer::_load_model_from_file` (around
   `src/libslic3r/Format/bbs_3mf.cpp:2363`), then the same empty-model
   error. The importer builds its own `obj_map` (XML resource id → parsed
   object) while parsing `3D/3dmodel.model`, and for reasons not yet
   understood does not find id=2 in it even though `<object id="2">`
   exists in the same file. Plausible causes not yet checked: an
   `identify_id`/backup-id field our synthesized `ModelInstance`s lack
   (the written config showed `identify_id value="14"` — unclear if that's
   expected or a symptom); a `type=` attribute the importer expects on
   `<object>` that differs between the "part" object (id=1) and the
   "instance-wrapper" object (id=2); or an ordering/two-pass requirement
   in the importer that a single freshly-`new`'d `Model` (not gone through
   `Plater`'s live `PartPlateList`) doesn't satisfy.
4. Reverted both `src/orcgraffiti_cli/orcgraffiti.cpp` and
   `src/CMakeLists.txt` write-mode changes rather than commit a feature
   that silently produces unopenable project files. `git diff` was clean
   before the next task began.

**Recommended next approach:** debug `_BBS_3MF_Importer::_load_model_from_file`
interactively (breakpoint at the `bbs_3mf.cpp:2363` warning) comparing a
real Orca-saved single-object 3MF against our CLI-written one byte-for-byte
in `Metadata/model_settings.config` and `3D/3dmodel.model`, OR sidestep the
BBS plate format entirely for CLI-written output by using the plain
`store_3mf` (from `Format/3mf.hpp`, non-BBS) for STL/OBJ inputs where no
existing project structure needs preserving — trading "importable back into
this same fork's BBS-aware reader" for "importable by any 3MF-compliant
tool," which may be an acceptable v1 scope narrowing worth discussing with
the user rather than assuming.

## Next Three Tasks

1. Debug or scope-narrow AS-3 per the investigation findings above —
   this needs either interactive debugging of the BBS 3MF importer or a
   product decision (ask the user) about narrowing AS-3's v1 output format.
2. Once AS-3 lands: ask for a manual GUI reopen check on a real 3MF with
   `--out` paint applied
3. Keep PR #1 updated; GUI retest auto-fit paint with garth.jpg when convenient

## Open PRs

https://github.com/hardcoreerik/OrcGraffiti/pull/1

## Updated

2026-08-11 — AS-3 write path attempted, two bugs found in 3MF round-trip
(missing plate structure; unresolved `obj_map` lookup failure even after
fixing the first), reverted rather than shipped broken. AS-2
`paint --dry-run` (landed earlier this session) is unaffected and remains
the current CLI capability ceiling. v0.1.0-alpha tagged and released on
GitHub earlier this session.
