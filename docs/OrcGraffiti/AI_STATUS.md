# OrcGraffiti AI Status

## Current Phase

Phase 6+ mapping quality green. **Agent Surface AS-1 (CLI skeleton) implemented.**

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
- `orcgraffiti.exe` (new): `version`, `help`, `info` — builds and links libslic3r only
- Sample image: `C:\Users\hardc\OneDrive\Pictures\garth.jpg`

## Active Task

AS-2: extend `orcgraffiti` with `paint --dry-run`, wiring `run_image_paint` to a
loaded 3MF/STL volume and producing the diagnostics report from §6.8.

## Next Three Tasks

1. AS-2 paint dry-run command (no 3MF mutation, report only)
2. AS-3 paint write path (3MF apply + GUI reopen proof)
3. Keep PR #1 updated; GUI retest auto-fit paint with garth.jpg when convenient

## Open PRs

https://github.com/hardcoreerik/OrcGraffiti/pull/1

## Updated

2026-08-11 — AS-1 orcgraffiti CLI skeleton landed (version/help/info), verified
against tests/orcgraffiti_cli/fixtures/unit_cube.stl. v0.1.0-alpha tagged
and released on GitHub earlier this session.
