# OrcGraffiti AI Status

## Current Phase

Phase 6+ mapping quality green. **Agent Surface design documented** (implementation deferred).

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
- Sample image: `C:\Users\hardc\OneDrive\Pictures\garth.jpg`

## Active Task

User retest mapping in GUI when ready. Agent Surface **implementation not started** — design freeze AS-0.

## Next Three Tasks

1. GUI retest auto-fit paint with garth.jpg
2. When scheduled: AS-1 CLI skeleton (`orcgraffiti info`)
3. Keep PR #1 updated

## Open PRs

https://github.com/hardcoreerik/OrcGraffiti/pull/1

## Updated

2026-08-06 — Agent Surface documentation set written
