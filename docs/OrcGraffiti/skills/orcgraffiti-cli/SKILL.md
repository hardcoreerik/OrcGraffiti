---
name: orcgraffiti-cli
description: >
  Headless OrcGraffiti automation for AI agents: inspect a 3D model, image-paint
  multicolor MMU faces onto it, and write a paintable 3MF — no GUI. Use when the
  user asks an agent to paint an image onto a mesh, batch process models, or drive
  OrcGraffiti's Image Paint without opening OrcaSlicer.
status: AS-1/AS-2/AS-3 (v1 scope) implemented and tested — see "Implementation status" below
spec: docs/OrcGraffiti/Agent_Surface.md
---

# orcgraffiti-cli

> **Implementation status:** `info`, `paint --dry-run`, and `paint --out` are
> implemented, built, and covered by `ctest` (label `CLI`). `verify` and the
> full flag set in `Agent_Surface.md` (`--auto-fit` toggle, explicit
> `--look`/`--up`, `--filaments-from-project`) are **not** implemented — this
> file documents the CLI as it actually behaves today, not the full future
> design. Cross-check against `orcgraffiti help` if in doubt; that output is
> authoritative.

## When to use

- Paint a PNG/JPG/BMP onto a model as Orca MMU per-face colors, headlessly
- Inspect volumes / triangle counts / existing paint before acting
- Dry-run a paint plan and read JSON diagnostics before committing to a write

## When not to use

- Interactive placement polish (use the GUI Image Paint gizmo)
- Preserving a BBS/Orca project's printer/filament/plate profile through
  paint — `--out` writes a **plain** 3MF (see "Known limitations" below);
  profile data is not preserved
- Confirming paint is visible without a human — this CLI's own self-check
  cannot currently verify it (see "Known limitations")
- Clicking the Orca GUI / screenshots / RPA
- Topology-changing remesh paint (not in MVP)

## Hard rules

1. Prefer **`--out` a new path** — `--out` equal to the input path is
   refused unless `--allow-in-place` is passed.
2. Prefer **`--dry-run`** before the first real write in a session.
   `--dry-run` and `--out` are mutually exclusive; exactly one is required.
3. Always pass **`--report path.json`** so results are machine-readable
   (otherwise the report prints to stdout, mixed with any stderr progress).
4. Treat report JSON as source of truth; stderr is for humans.
5. `paint` does **not** mutate printer/process/filament profile files
   (INV-008) — and with the current `store_3mf`-based `--out` writer, it
   also doesn't *preserve* a BBS/Orca project's profile settings. If the
   input was a real Orca project 3MF, the output loses that context.
6. Selection: run `info` first if the project has multiple volumes; pass
   `--object`/`--volume` to `paint` to disambiguate.

## Binary

| Tool | Role | Status |
|---|---|---|
| `orcgraffiti` | info / paint | **Built** |
| `orca-slicer` | slice / export / existing CLI | Exists in this fork, undocumented here |

Windows dev-tree path: `F:\Ai\OrcGraffiti\build\src\Release\orcgraffiti.exe`

## Cookbook

### 1. Inspect

```bash
orcgraffiti info project.3mf --report info.json
```

Read `objects[].volumes[]` for `triangle_count`, `bbox_mm`, `has_mmu_paint`,
`fingerprint`. Works for `.stl` and `.3mf` inputs.

### 2. Build a filament palette (if the input has no usable project colors)

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

### 3. Dry-run paint

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

### 4. Write a painted 3MF

```bash
orcgraffiti paint model.stl \
  --image photo.jpg \
  --filaments palette.json \
  --view front \
  --colors 4 \
  --out model_painted.3mf \
  --report paint.json
```

Add `--force` to overwrite an existing `--out` path.

**There is currently no automated way to confirm the write worked beyond
`ok: true` in the report.** `orcgraffiti info model_painted.3mf` will
report `has_mmu_paint: false` even on a successfully painted file — this is
a known reader-format gap (see below), not evidence the paint is missing.
If you need certainty, ask the user to open the file in Orca's GUI and
check the Image Paint gizmo / MMU face colors.

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
| `--dry-run` | Report only, no write — mutually exclusive with `--out` |
| `--out <path.3mf>` | Write mode — mutually exclusive with `--dry-run` |
| `--force` | Allow overwriting an existing `--out` path |
| `--allow-in-place` | Allow `--out` == input path (default: refused) |
| `--report <path.json>` | Write the JSON report to a file instead of stdout |

## Known limitations (read before assuming something is broken)

1. **`--out` writes a plain (non-BBS) 3MF**, via `Slic3r::store_3mf`, not
   this fork's native `store_bbs_3mf`. A BBS/Orca project's
   printer/filament/plate settings are **not** carried into the output.
   This was a deliberate scope decision after the BBS writer proved to have
   an unresolved 3MF-round-trip bug — see `AI_STATUS.md`'s AS-3 notes if
   revisiting this.
2. **This CLI cannot verify its own `--out` writes show paint.** `info` and
   `paint --dry-run` always read `.3mf` through the BBS-format reader
   (`load_bbs_3mf`), which only recognizes the BBS `paint_color` triangle
   attribute — not the `slic3rpe:mmu_segmentation` attribute `store_3mf`
   writes. `has_mmu_paint` will read back `false` on a file this same CLI
   just painted. Mesh geometry and the topology fingerprint DO round-trip
   correctly (verified) — it's specifically the paint-attribute check that
   can't see across the format mismatch.
3. Mesh geometry/topology round-trips exactly through `--out` — verified by
   comparing `info`'s reported `fingerprint` before and after a
   `paint --out` cycle on the same volume.

## Failure remediation

| Symptom | Action |
|---|---|
| `ModelLoadFailed` on a real `.3mf` | Should not happen after the `LoadStrategy` fix (2026-08-11) — if it recurs, check the input isn't corrupt |
| `ImageOpenFailed` / `ImageDecodeFailed` | Check the image path and format (PNG/JPG/BMP) |
| `NoAvailableFilaments` | `--filaments` path missing/malformed — see the JSON shape above |
| `InvalidProjection: unknown --view preset` | Use one of `front back left right top bottom` |
| `painted_faces == 0` | Wrong `--view` for this mesh's orientation; try `top` or a different side |
| `exactly one of --dry-run or --out is required` | Pass exactly one, not both, not neither |
| `--out already exists` | Pass `--force`, or pick a new `--out` path |
| `--out equals input path` | Pass `--allow-in-place`, or pick a different `--out` path |

## Sample assets (dev)

- Image: `C:\Users\hardc\OneDrive\Pictures\garth.jpg` (user machine, not in repo)
- Committed test fixtures: `tests/orcgraffiti_cli/fixtures/` —
  `unit_cube.stl`, `filaments.json`, `sample_project.3mf`

## See also

- `docs/OrcGraffiti/Agent_Surface.md` — full future-state design (this
  skill documents the current subset actually implemented)
- `docs/OrcGraffiti/Project_Truth.md` — product invariants
- `docs/OrcGraffiti/AI_STATUS.md` — current implementation status and
  the AS-3 investigation notes referenced above
