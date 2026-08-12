# OrcGraffiti AI Status

## Current Phase

Phase 6+ mapping quality green. **Agent Surface AS-1 through AS-4 implemented and tested.**
Phase 7 (cylindrical/spherical projection) started: cylindrical projection
math landed.

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

- ImagePaint unit tests: **78/78** (388 assertions; incl. cylindrical projection, 9 new)
- Full app: Release `orca-slicer.exe` builds
- `orcgraffiti.exe`: `version`, `help`, `info`, `paint --dry-run`, `paint --out` — links libslic3r only
- ctest: **24/24 green** (15 ImagePaint core + 9 orcgraffiti CLI contract tests)
- Sample image: `C:\Users\hardc\OneDrive\Pictures\garth.jpg`

## Active Task

AS-3's BBS-native writer bug is **fixed** (root cause found via a real
reference 3MF the user provided — see below). `paint --out` now writes via
`store_bbs_3mf` and this CLI's own `info`/`paint --dry-run` correctly
report `has_mmu_paint: true` on a file it just wrote — locked in as an
automated ctest regression guard
(`orcgraffiti_cli_paint_out_write` → `orcgraffiti_cli_paint_out_reload_has_paint`).

**A fresh test file (`gui_check_bbs_v2.3mf`) was generated and sent to the
user for the actual GUI reopen check — this is still pending confirmation
at time of writing.** Do not claim AS-3's real exit gate is met until that
comes back positive.

### AS-3 bug history (2026-08-11, this session — read before touching the writer again)

1. **Fixed — `LoadStrategy` bug, pre-existing since AS-1, affects ANY
   `.3mf` input:** `Model::read_from_file`'s default `LoadStrategy`
   (`AddDefaultInstances` alone) omits `LoadModel`/`LoadConfig`, so the BBS
   3MF importer silently returns zero objects for `.3mf` inputs. Fixed via
   `full_load_strategy()` in `orcgraffiti.cpp`. Locked in by
   `orcgraffiti_cli_info_3mf_fixture`.
2. **First BBS-writer attempt (loop 3): reverted.** Empty `PlateDataPtrs`
   for non-3MF inputs wrote no `<plate>` element at all → reload saw zero
   objects. Fixed by synthesizing a single default `PlateData` when empty.
   After that fix, reload *still* failed differently:
   `"can not find object from plate's obj_map, id=2, skip this object"`
   from `_BBS_3MF_Importer::_load_model_from_file` (`bbs_3mf.cpp:2363`) —
   root cause not isolated at the time. Reverted rather than ship broken.
3. **Scope narrowed to plain `store_3mf` (v1), per user decision** after
   option 2's dead end. Verified geometry/fingerprint round-trip — but
   **the user's actual GUI check on this file failed**: "just a cube",
   Orca mislabeled the file's origin as Bambu Studio. Confirmed the plain
   writer does not satisfy AS-3's real exit gate, not just the CLI's own
   already-known-broken self-check.
4. **User provided a real reference 3MF** (a single-object project saved
   by this fork's own GUI, `D:\3D models\prostike arrow\sample.3mf` — not
   committed to the repo, personal file, used only for local diffing).
   Diffing it against the loop-3 BBS attempt's output found the actual
   root cause: `bbs_3mf.cpp`'s exporter only writes an `<assemble_item>`
   for instances with `ModelInstance::is_assemble_initialized() == true`
   (`bbs_3mf.cpp:8186`). A freshly-loaded model's instances never have this
   set, so the exporter silently wrote an empty `<assemble></assemble>` —
   which the reference file's populated `<assemble_item>` did NOT have.
5. **Fixed and verified.** `cmd_paint`'s write path now: (a) synthesizes a
   default plate when `plate_data` is empty (loop-3 fix, kept), (b) calls
   `instance->set_assemble_transformation(instance->get_transformation())`
   on every instance before writing (this loop's fix), (c) uses
   `SaveStrategy::Zip64 | SaveStrategy::UseLoadedId` (matching
   `OrcaSlicer.cpp`'s own `export_project`). `paint --out` → `info` on the
   same file now reports `has_mmu_paint: true`. Fingerprint/geometry still
   round-trip exactly.

**Still unverified:** only tested against a single-object,
single-instance, single-volume model (`unit_cube.stl`). Multi-object,
multi-instance, or multi-plate inputs are unexplored — the assemble-init
loop handles them mechanically (iterates all objects/instances) but has
had zero real testing against such a case.

## Next Three Tasks

1. Get the user's GUI confirmation on `gui_check_bbs_v2.3mf` (already
   sent) — this is AS-3's actual exit gate. If it fails, do NOT attempt
   another blind fix — ask for a fresh reference file/diff, as this
   session's breakthrough only came from a real comparison, not guessing.
2. If confirmed: consider AS-5 (MCP thin wrap), now legitimately unblocked
   rather than nominally-gated-past.
3. Otherwise: continue Phase 7 (cylindrical projection math landed this
   session — next: wire it into `FaceSampler`/`ImagePaintPipeline`, or move
   to spherical projection / occlusion per `Roadmap.md` §11).

## Open PRs

https://github.com/hardcoreerik/OrcGraffiti/pull/1

## Updated

2026-08-11 — AS-3's BBS 3MF writer bug found and fixed (missing
`<assemble_item>` — root cause via a real reference 3MF the user provided
after their GUI check caught the plain-writer approach failing). Own
CLI self-check now passes; human GUI confirmation on a fresh file still
pending. Also landed Phase 7's first task: cylindrical projection math
(`project_cylindrical`/`make_cylinder_frame`), 9 new tests, not yet wired
into the sampling pipeline. AS-4 (skill file) and the `LoadStrategy` fix
landed earlier this session; v0.1.0-alpha tagged and released on GitHub.
