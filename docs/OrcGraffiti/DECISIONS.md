# OrcGraffiti — Architecture Decision Records

## ADR-0001 — Fork as OrcGraffiti, not OrcaSlicer

**Status:** Accepted
**Date:** 2026-08-05

### Context

The project requires a named GitHub fork of OrcaSlicer to develop Image Paint as a native feature.

### Decision

Fork OrcaSlicer/OrcaSlicer to hardcoreerik/OrcGraffiti. Use this name throughout the project.

### Consequences

- Fork URL: https://github.com/hardcoreerik/OrcGraffiti
- Upstream remote: https://github.com/OrcaSlicer/OrcaSlicer
- The fork begins at commit b97ca3c0ace8cb04eb520d86417fbe13b7ddbdde which matches the architecture research baseline exactly.

### Evidence

Fork created via `gh repo fork OrcaSlicer/OrcaSlicer --fork-name OrcGraffiti`.

---

## ADR-0002 — Use Existing MMU Facet Format

**Status:** Accepted
**Date:** 2026-08-05

### Context

Multiple color-painting representations exist: per-face state arrays, Bambu's topology-changing textured mesh import, and Orca's existing TriangleSelector-based MMU painting.

### Decision

The MVP writes face states using exactly the same representation as Orca's existing Color Painting gizmo via `ModelVolume::mmu_segmentation_facets`. No custom format is introduced.

### Consequences

- 3MF persistence is free (existing writer handles it)
- Undo/redo can use the same snapshot mechanism as existing painting tools
- Future Bambu-style topology-changing paths use a distinct `TopologyChangingPaintPlan` type

### Evidence

Documented in Project_Truth.md section 5.1. No new format risk.

---

## ADR-0003 — GUI-Independent Core Pipeline

**Status:** Accepted
**Date:** 2026-08-05

### Context

OrcaSlicer mixes GUI and algorithm code in many gizmo files. This makes testing difficult and creates platform dependencies.

### Decision

All image paint algorithms live in `src/libslic3r/ImagePaint/` with zero GUI dependencies. The GUI layer in `src/slic3r/GUI/ImagePaint/` and `src/slic3r/GUI/Gizmos/GLGizmoImagePainter.*` depends on the core, not the reverse.

### Consequences

- Core can be tested without launching OrcaSlicer
- Headless unit tests are the primary quality gate before any GUI work
- GUI work is blocked until Phase 3 headless pipeline is verified

### Evidence

Architecture doc sections 2 and 3. Roadmap Phase 4 prerequisite for Phase 5.
