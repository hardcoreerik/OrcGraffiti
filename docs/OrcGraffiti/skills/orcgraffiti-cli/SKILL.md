---
name: orcgraffiti-cli
description: >
  Headless OrcGraffiti automation for AI agents: inspect a 3D model and
  dry-run image-paint multicolor MMU faces onto it — no GUI. Use when the
  user asks an agent to plan a paint job or inspect a model without opening
  OrcaSlicer. Writing a painted 3MF (--out) is currently disabled — see
  "Implementation status".
status: AS-1/AS-2 implemented and tested; AS-3 write path DISABLED (confirmed to crash real slicer software)
spec: docs/OrcGraffiti/Agent_Surface.md
---

# orcgraffiti-cli

> **Implementation status:** `info` and `paint --dry-run` are implemented,
> built, and covered by `ctest` (label `CLI`). **`paint --out` (writing a
> painted 3MF) is disabled** — two separate write-path fix attempts were
> each verified structurally correct by this CLI's own tooling, but both
> were confirmed by direct human testing to break real slicer software
> (crashed OrcaSlicer 2.4.2, hung FlashForge Studio). `verify` and most of
> `Agent_Surface.md`'s full flag set are not implemented either. This file
> documents the CLI as it actually behaves today. Cross-check against
> `orcgraffiti help` if in doubt; that output is authoritative.

## When to use

- Inspect volumes / triangle counts / existing paint before acting
- Dry-run a paint plan and read JSON diagnostics (painted faces, cluster
  matches, warnings) without writing anything

## When not to use

- **Writing a painted 3MF file** — `--out` is disabled. It will refuse
  with a clear error (`InvalidTarget`, "--out is disabled") rather than
  silently doing nothing or producing a broken file.
- Interactive placement polish (use the GUI Image Paint gizmo)
- Clicking the Orca GUI / screenshots / RPA
- Topology-changing remesh paint (not in MVP)

## Why `--out` is disabled (read before attempting to re-enable it)

Two independent write-path implementations landed and were each verified
by this CLI's own tooling (`info` reporting `has_mmu_paint: true`,
fingerprint/geometry round-tripping exactly) — and both were then
confirmed broken by an actual human opening the file:

1. First attempt (BBS-native writer, missing `<assemble_item>` fixed):
   **crashed OrcaSlicer 2.4.2 and FlashForge Studio outright.**
2. Second attempt (added a placeholder thumbnail to fix a dangling 3MF
   package relationship, structurally verified): **still broken — hung
   FlashForge Studio indefinitely on load** (screenshot evidence: stuck on
   a "Loading..." progress dialog).

**The lesson, not just the bug:** this CLI's own self-consistency checks
(`has_mmu_paint` round-tripping, fingerprint matching, zip structural
inspection) are necessary but not sufficient evidence that a written 3MF
is safe to hand to real software. Do not re-enable `--out` on the strength
of those checks alone again. See `AI_STATUS.md`'s full AS-3 bug history
for the detailed chronology before attempting a third fix.

## Hard rules

1. Always pass **`--report path.json`** so results are machine-readable
   (otherwise the report prints to stdout, mixed with any stderr progress).
2. Treat report JSON as source of truth; stderr is for humans.
3. `paint --dry-run` does **not** mutate anything — no printer/process/
   filament profile files (INV-008), no 3MF write of any kind.
4. Selection: run `info` first if the project has multiple volumes; pass
   `--object`/`--volume` to `paint` to disambiguate.

## Binary

| Tool | Role | Status |
|---|---|---|
| `orcgraffiti` | info / paint (dry-run only) | **Built** |
| `orca-slicer` | slice / export / existing CLI | Exists in this fork, undocumented here |

Windows dev-tree path: `F:\Ai\OrcGraffiti\build\src\Release\orcgraffiti.exe`

## Cookbook

### 1. Inspect

```bash
orcgraffiti info project.3mf --report info.json
```

Read `objects[].volumes[]` for `triangle_count`, `bbox_mm`, `has_mmu_paint`,
`fingerprint`. Works for `.stl` and `.3mf` inputs.

### 2. Build a filament palette

`info` does not yet report a project's filament colors, so `paint` always
needs an explicit palette file — 0-based indices, `#RRGGBB` hex:

```json
{
  "filaments": [
    { "index": 0, "name": "PLA Red",   "color_hex": "#E31C23" },
    { "index": 1, "name": "PLA Black", "color_hex": "#1A1A1A" },
    { "index": 2, "name": "PLA White", "color_hex": "#F5F5F5" }
  ]
}
```

### 3. Dry-run paint (the only working `paint` mode right now)

```bash
orcgraffiti paint model.stl \
  --image photo.jpg \
  --filaments palette.json \
  --view front \
  --colors 4 \
  --dry-run \
  --report paint_dry.json
```

Projection is always auto-fit to the mesh (no manual `--look`/`--up` flags
yet) — `--view` picks the camera direction: `front`, `back`, `left`,
`right`, `top`, `bottom`. `front`/`top` are locked to golden tests in
`tests/libslic3r/test_image_paint_pipeline.cpp`; `back`/`left`/`bottom` are
the geometric mirror, not independently golden-tested yet.

Check `ok`, `diagnostics.painted_faces`, `diagnostics.warnings`. If
`painted_faces` is 0, the view is probably wrong for this mesh — try a
different `--view`.

There is no supported way to turn a dry-run plan into an actual painted
3MF right now. Use the GUI Image Paint gizmo for that until `--out` is
re-enabled.

## Important flags (paint)

| Flag | Meaning |
|---|---|
| `--image <path>` | PNG/JPG/BMP source, **required** |
| `--filaments <path>` | Palette JSON per above, **required** |
| `--object <i>` / `--volume <i>` | Selection indices (default 0/0) |
| `--view <preset>` | `front` `back` `left` `right` `top` `bottom` (default `front`) |
| `--colors <n>` | Quantizer target, 1-16 (default 4) |
| `--quality <q>` | `fast` `threepoint` `gaussian7` (default `gaussian7`) |
| `--merge <policy>` | `overwrite` or `preserve` existing paint (default `overwrite`) |
| `--dry-run` | Report only, no write — **currently required**, `--out` is disabled |
| `--report <path.json>` | Write the JSON report to a file instead of stdout |

## Failure remediation

| Symptom | Action |
|---|---|
| `--out is disabled` | Expected — see "Why `--out` is disabled" above. Not a bug to work around. |
| `ModelLoadFailed` on a real `.3mf` | Should not happen after the `LoadStrategy` fix (2026-08-11) — if it recurs, check the input isn't corrupt |
| `ImageOpenFailed` / `ImageDecodeFailed` | Check the image path and format (PNG/JPG/BMP) |
| `NoAvailableFilaments` | `--filaments` path missing/malformed — see the JSON shape above |
| `InvalidProjection: unknown --view preset` | Use one of `front back left right top bottom` |
| `painted_faces == 0` | Wrong `--view` for this mesh's orientation; try `top` or a different side |

## Sample assets (dev)

- Image: `C:\Users\hardc\OneDrive\Pictures\garth.jpg` (user machine, not in repo)
- Committed test fixtures: `tests/orcgraffiti_cli/fixtures/` —
  `unit_cube.stl`, `filaments.json`, `sample_project.3mf`, `pattern.png`

## See also

- `docs/OrcGraffiti/Agent_Surface.md` — full future-state design (this
  skill documents the current subset actually implemented)
- `docs/OrcGraffiti/Project_Truth.md` — product invariants
- `docs/OrcGraffiti/AI_STATUS.md` — current implementation status and the
  full AS-3 bug chronology (read before attempting to re-enable `--out`)
