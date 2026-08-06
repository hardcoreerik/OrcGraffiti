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
- Upstream remote: https://github.com/OrcaSlicer/OrcaSlicer (fetch only — push disabled)
- The fork begins at commit b97ca3c0ace8cb04eb520d86417fbe13b7ddbdde which matches the architecture research baseline exactly.
- No upstream contribution is planned. This is a private product fork.

### Evidence

Fork created via `gh repo fork OrcaSlicer/OrcaSlicer --fork-name OrcGraffiti`.

---

## ADR-0002 — No upstream contribution

**Status:** Accepted
**Date:** 2026-08-05

### Context

OrcaSlicer is AGPL-3.0. A fork can legally contribute back, but the project owner has no current intention to upstream this work.

### Decision

The upstream remote is configured fetch-only (`git remote set-url --push upstream no-push`). All work stays in hardcoreerik/OrcGraffiti. Per-phase PRs are opened within that fork only.

### Consequences

- No accidental pushes to OrcaSlicer/OrcaSlicer are possible.
- AGPL compliance still required for any distribution of the fork.
- Can re-enable upstream contribution later if desired.

---

## ADR-0003 — Use Existing MMU Facet Format

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

## ADR-0004 — GUI-Independent Core Pipeline

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

---

## ADR-0010 — Agent Surface: Hybrid CLI First (Skill, then MCP)

**Status:** Accepted (design); implementation deferred
**Date:** 2026-08-06

### Context

Users and AI agents (Claude, Grok, Codex, Cursor) need machine-usable access to OrcGraffiti Image Paint and existing Orca capabilities. Options considered:

1. Headless CLI over ImagePaint core
2. Agent skill documentation only
3. MCP server
4. Python bindings
5. GUI automation / RPA

Prior art: PrusaSlicer/Orca/Bambu CLI for slicing; CuraEngine headless engine pattern; Blender-MCP for AI tool access; FreeCADCmd/OpenSCAD file pipelines. No mature public CLI for image→MMU face paint was found.

### Decision

1. **Primary surface:** headless CLI for paint/info (orcgraffiti), reusing `libslic3r/ImagePaint`.
2. **Slice/export:** document and call **existing** `orca-slicer` CLI; do not reimplement Print.
3. **Skills (P1):** ship `SKILL.md` after CLI exists.
4. **MCP (P2):** thin wrapper over CLI only.
5. **Python (P3):** deferred; subprocess-first if needed.
6. **GUI RPA:** rejected.

Normative design: `docs/OrcGraffiti/Agent_Surface.md`.

### Consequences

- Aligns with ADR-0004 (GUI-independent core).
- Agents get file→file + JSON reports; undo is file versioning, not GUI snapshots.
- Implementation gated as Agent Surface phases AS-0…AS-7 in Roadmap.
- Must not mutate printer profiles (Project_Truth INV-008).

### Evidence

Research survey and contracts in Agent_Surface.md §§3–13.
