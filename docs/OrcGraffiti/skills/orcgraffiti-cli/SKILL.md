---
name: orcgraffiti-cli
description: >
  Headless OrcGraffiti automation for AI agents: inspect a 3D model, image-paint
  multicolor MMU faces onto it, and write a paintable 3MF — no GUI. Use when the
  user asks an agent to paint an image onto a mesh, batch process models, or drive
  OrcGraffiti's Image Paint without opening OrcaSlicer.
status: AS-1/AS-2/AS-3 implemented and tested (native BBS 3MF writer) — see "Implementation status" below
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
- Confirming paint is visible without a human — even though this CLI's own
  self-check now passes (`has_mmu_paint: true` after a `paint --out` +
  `info` round trip), an actual GUI reopen has not been exhaustively
  verified across every input shape — see "Known limitations"
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
   (INV-008) — the CLI passes a real 3MF project's loaded config/plate/
   preset data straight through to the writer unchanged, rather than
   dropping or editing it.
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

`orcgraffiti info model_painted.3mf` now correctly reports
`has_mmu_paint: true` after a `paint --out` write (fixed 2026-08-11 — see
"Known limitations"). That confirms the paint attribute round-trips
through this CLI's own reader/writer pair; it is **not** the same as a GUI
reopen check. Ask the user to open the file in Orca and check the Image
Paint gizmo / MMU face colors for real confirmation, especially for
anything beyond a simple single-object model.

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

1. **`--out` writes via `Slic3r::store_bbs_3mf`**, this fork's native
   format — not the plain/generic 3MF writer. Two earlier attempts at this
   failed round-trip (see `AI_STATUS.md`'s AS-3 notes for the full history):
   first an empty `<plate>` element for non-3MF inputs, then a missing
   `<assemble_item>` entry (the exporter only writes one for instances with
   an initialized assemble transform — fixed by initializing it from the
   instance's own transformation before writing). Both are now handled.
2. This CLI's own `info`/`paint --dry-run` correctly report
   `has_mmu_paint: true` on a file `--out` just wrote (fixed alongside the
   writer bug above, since reader and writer now agree on the `paint_color`
   attribute). This is real evidence the write worked structurally, but is
   still not the same as a human confirming the GUI shows the paint —
   **only tested against a single-object, single-instance, single-volume
   model so far.** Multi-object or multi-plate inputs are unverified.
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
