# OrcGraffiti AI Status

## Current Phase

Phase 4 complete (code). Phase 5 — Interactive Gizmo is next.

## Current Branch

feature/image-paint-phase1-types (carries Phases 0–4)

## Current Commit

2e28de6845 feat: Phase 4 — PaintStateMerge, ImagePaintPipeline, 11 pipeline tests

## Working Build Configuration

Dep build RUNNING in background.
cmake -S deps -B deps/build_x64 -G "Visual Studio 17 2022" -A x64 succeeded.
cmake --build deps/build_x64 --config Release -j 8 running (~30-60 min).
Log: devlogs/OrcGraffiti/deps_build.log

## Most Recent Verified Behavior

SOURCE ONLY — 66 unit tests written, build still running.

Phase 4 pipeline: ImagePaintRequest -> FacePaintPlan end-to-end headless.
Stages: decode -> fingerprint -> geometry -> sample -> quantize -> match -> assign -> clean -> merge.
Merge policies: PreserveExisting (paint only kStateNone faces), OverwriteInsideMask.
Golden-cube integration tests: solid-colour image, transparent image, PreserveExisting,
diagnostics, determinism, error cases (empty mesh, no filaments).

## Sample Image

User image: C:\Users\hardc\OneDrive\Pictures\garth.jpg
Will be wired into the Phase 5 gizmo file-picker (image_path in ImagePaintRequest).

## Active Task

Phase 5 — Interactive Gizmo:
  GLGizmoImagePainter (renders placement handles + preview overlay)
  ImagePaintController (coordinates background job + Apply)
  ImagePaintJob (worker: wraps run_image_paint, posts FacePaintPlan to UI thread)

## Next Three Tasks

1. ImagePaintJob.hpp/.cpp — background worker that calls run_image_paint
2. ImagePaintController.hpp/.cpp — state machine: Idle -> Projecting -> Applied
3. GLGizmoImagePainter.hpp — gizmo scaffold (Phase 5 day 1, GUI code allowed here)

## Known Failures

Build not yet complete — dep build running in background.
Tests are source-only pending build completion.

## Blockers

Dep build in progress. Once complete, attempt main source build.
ctest --test-dir build/tests -C Release -R image_paint --output-on-failure

## Open PRs

PR #1: https://github.com/hardcoreerik/OrcGraffiti/pull/1
  Covers: Phases 0-4 (all source, 66 tests)
  Status: Draft, build pending

## Loop Schedule

CronJob 7cdaa6af — fires every 30 min at :03 and :33 past the hour.

## Last Loop Summary

Loop 4 (2026-08-05):
- Implemented Phase 4: PaintStateMerge (merge policies) + ImagePaintPipeline (end-to-end)
- 11 new unit tests (4 merge-policy + 7 golden-cube integration)
- 66 total unit tests across 4 files (Phases 1-4)
- Updated PR #1 to cover Phases 0-4
- Pushed to origin: feature/image-paint-phase1-types

## Updated

2026-08-05 Loop 4
