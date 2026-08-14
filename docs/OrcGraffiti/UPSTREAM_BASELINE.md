# OrcGraffiti — Upstream Baseline

## Repository Information

| Field | Value |
|---|---|
| Fork | https://github.com/hardcoreerik/OrcGraffiti |
| Upstream | https://github.com/OrcaSlicer/OrcaSlicer |
| Current branch | feature/orcgraffiti-bootstrap |
| Current commit | b97ca3c0ace8cb04eb520d86417fbe13b7ddbdde |
| Research baseline commit | b97ca3c0ace8cb04eb520d86417fbe13b7ddbdde |
| Baseline date | 2026-08-05 |
| Clone date | 2026-08-05 |

## Baseline Match

The current fork HEAD exactly matches the research baseline commit. No API drift has been detected yet.

## Key Integration Points to Verify

These paths were documented in research and must be verified against current source:

| Path | Purpose | Verified |
|---|---|---|
| src/slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.hpp | MMU gizmo — state write model | Pending |
| src/slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.cpp | MMU gizmo implementation | Pending |
| src/slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp | Base painter gizmo | Pending |
| src/libslic3r/TriangleSelector.hpp | Face state data structure | Pending |
| src/libslic3r/TriangleSelector.cpp | Face state implementation | Pending |
| src/libslic3r/Model.hpp | ModelVolume definition | Pending |
| src/libslic3r/Model.cpp | ModelVolume implementation | Pending |
| src/libslic3r/Format/3mf.cpp | 3MF serialization | Pending |
| src/libslic3r/MultiMaterialSegmentation.cpp | MMU segmentation | Pending |

## Bambu Studio Reference

| Field | Value |
|---|---|
| Reference commit | f43cbe4296b16cb6089a47af8fe82d2b79d2442f |
| Repository | https://github.com/bambulab/BambuStudio |

## Rebase Notes

- No rebase has been performed yet
- Upstream will be tracked for rebasing during development
- Run `git fetch upstream && git rebase upstream/main` on the feature branch periodically

## Detected Differences

None detected yet. Research baseline and current HEAD are identical.
