# OrcGraffiti AI Status

## Current Phase

Phase 6+ mapping quality — **tests green**, full app rebuild with projection fixes.

## Current Branch

feature/image-paint-phase1-types (carries Phases 0–6 + mapping fixes)

## Current Commit

fix: mesh-local auto-fit projection + libjpeg path for JPG (see branch tip)

## Working Build Configuration

ALL DEPS BUILT (25). Main cmake CONFIGURED. PATH: CMake before Strawberry.

```
cmake --build build --config Release --target libslic3r_tests -- -m
ctest --test-dir build/tests/libslic3r -C Release -L ImagePaint --output-on-failure

cmake --build build --config Release --target OrcaSlicer -- -m
# launch: build\src\Release\orca-slicer.exe
```

## Most Recent Verified Behavior

**ctest -L ImagePaint: 69/69 PASSED** (includes garth.jpg integration).

User smoke (prior session): gizmo toolbar worked, app loaded recent items, but mapping quality was poor.

Root causes fixed overnight:
1. **Coordinate space bug** — mesh is volume-local; camera was world-space. Now transform look/up via `inverse(world_matrix).linear()`.
2. **Fixed 100×100 mm plane** — now `fit_planar_projection()` auto-sizes to mesh extent as seen from camera; preserves image aspect.
3. **JPEG failed silently** — OpenCV dep built with `WITH_JPEG=OFF`. Added **libjpeg-turbo** fallback in `ImageDecoder` (JPEG already linked to libslic3r). Verified with `garth.jpg` (692×994).
4. Gizmo UI: **Auto-fit to view** (default on) + **Fit now** button; caches image pixel size for aspect.

## Sample Image

`C:\Users\hardc\OneDrive\Pictures\garth.jpg` (also copied to `build/garth.jpg` for tests)

How to retest:
1. Launch `build\src\Release\orca-slicer.exe`
2. Load a model, select it
3. Open Image Paint gizmo
4. Browse → garth.jpg (Auto-fit checked)
5. Face the surface you want painted, click Apply
6. Use multicolor filaments for visible result

## Active Task

User retest mapping quality after sleep. Optional next: placement preview overlay, rotation control, world-space transform snapshot per Project_Truth §14 full chain.

## Next Three Tasks

1. User interactive retest of mapping with garth.jpg
2. Optional: on-canvas projector rectangle preview
3. Optional: rebuild OpenCV with WITH_JPEG=ON long-term (libjpeg path is fine for MVP)

## Known Failures

None in ImagePaint unit suite.

## Blockers

None.

## Open PRs

PR #1: https://github.com/hardcoreerik/OrcGraffiti/pull/1 (draft)

## Updated

2026-08-06 overnight mapping fix session
