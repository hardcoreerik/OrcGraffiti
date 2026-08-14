# AGENTS.md — OrcGraffiti (OrcaSlicer fork)

OrcaSlicer — open-source C++17 3D slicer. wxWidgets GUI, CMake build system.
OrcGraffiti adds a native Image Paint feature to project images onto 3D mesh faces
as OrcaSlicer-compatible per-face multicolor painting.

## OrcGraffiti Agent Instructions

Every AI agent must read these documents before making any changes:

1. `docs/OrcGraffiti/Project_Truth.md` — wins over all other sources
2. `docs/OrcGraffiti/Architechture.md` — architectural design
3. `docs/OrcGraffiti/Roadmap.md` — gated execution phases
4. `docs/OrcGraffiti/AI_STATUS.md` — current phase and active task

### Core Rules

- Use existing Orca MMU facet painting: `ModelVolume::mmu_segmentation_facets`
- Do not alter printer, process, filament, plate, or support settings
- Do not remesh in MVP — no subdivision, repair, or edge collapse
- Do not add new external dependencies for MVP
- Do not include wxWidgets/ImGui/OpenGL/Plater/live ModelVolume* in core pipeline
- Centralize all 0-based/1-based filament index conversion in one utility
- Workers own immutable snapshots — never live model pointers
- Apply happens on the UI thread only, with one undo snapshot
- Validate stable object/volume IDs and TopologyFingerprint before Apply
- Add tests for every behavior change
- Preserve AGPL provenance; record Bambu adaptations in PORT_PROVENANCE.md

### Pre-Coding Checklist

1. Which face-indexed data can this change invalidate?
2. What coordinate space is used?
3. Who owns the data?
4. Which thread runs it?
5. What happens if the project changes between compute and Apply?
6. What proves 3MF persistence?
7. What exactly does Undo restore?

---

## Build Commands

```bash
# macOS
cmake --build build/arm64 --config RelWithDebInfo --target all --

# Linux
cmake --build build --config RelWithDebInfo --target all --

# Windows (replace %build_type% with Debug/Release/RelWithDebInfo)
cmake --build . --config %build_type% --target ALL_BUILD -- -m
```

## Testing

Catch2 framework. Tests in `tests/`; see [tests/AGENTS.md](tests/AGENTS.md) for where a new test belongs and the conventions to follow.

```bash
cd build && ctest --output-on-failure           # all tests
ctest --test-dir ./tests/libslic3r              # individual suite

# OrcGraffiti focused tests
ctest --test-dir build/tests -C Release -R image_paint --output-on-failure
```

## Code Style

- C++17, selective C++20. PascalCase classes, snake_case functions/variables
- `#pragma once` for headers. Smart pointers and RAII preferred
- Parallelization via TBB — be mindful of shared state
- Always use `SetSizerAndFit(sizer)` instead of `SetSizer(sizer)` on top level window.

## Key Entry Points

- App startup: `src/OrcaSlicer.cpp`
- Slicing pipeline: `src/libslic3r/Print.cpp`
- All print/printer/material settings: `src/libslic3r/PrintConfig.cpp`
- GUI: `src/slic3r/GUI/`
- Core algorithms: `src/libslic3r/` (GCode/, Fill/, Support/, Geometry/, Format/, Arachne/)
- Printer profiles: `resources/profiles/[manufacturer].json`
- **Image Paint core**: `src/libslic3r/ImagePaint/`
- **Image Paint GUI**: `src/slic3r/GUI/ImagePaint/`, `src/slic3r/GUI/Gizmos/GLGizmoImagePainter.*`

## Critical Constraints

- **Backward compatibility required** for .3mf project files and printer profiles
- **Cross-platform** — all changes must work on Windows, macOS, and Linux
- Profile/format changes need version migration handling
- Dependencies built separately in `deps/build/`, then linked to main app

## Code Review Focus Areas

- Changes must not cause regressions in existing functionality, defaults, profiles, or project compatibility.
- Features gated by options must not affect existing behavior when those options are disabled.
- Changes should follow the existing code style and architecture. Architectural changes should be justified in code comments and the PR description.
