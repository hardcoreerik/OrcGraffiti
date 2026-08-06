---
name: orcgraffiti-cli
description: >
  Headless OrcGraffiti / OrcaSlicer automation for AI agents: inspect 3MF models,
  image-paint multicolor MMU faces, verify paint, and chain to existing orca-slicer
  slice CLI. Use when the user asks an agent to paint an image onto a mesh, batch
  process projects, or drive OrcGraffiti without the GUI.
status: template — binary not implemented yet; flags are design-normative
spec: docs/OrcGraffiti/Agent_Surface.md
---

# orcgraffiti-cli

> **Implementation status:** The `orcgraffiti` binary is **not shipped yet**.
> This skill is the **agent-facing contract** from `Agent_Surface.md`.
> Do not invent flags. When the binary lands, this file must match `--help`.

## When to use

- Paint a PNG/JPG onto a model as Orca MMU per-face colors
- Inspect volumes / triangle counts / existing paint before acting
- Dry-run paint plans and read JSON diagnostics
- After paint, slice with **existing** `orca-slicer` CLI (not a new slicer)

## When not to use

- Interactive placement polish (use the GUI Image Paint gizmo)
- Changing printer/filament profiles as a side effect of paint
- Clicking the Orca GUI / screenshots / RPA
- Topology-changing remesh paint (not in MVP)

## Hard rules

1. Prefer **`--out` a new path** — never silently overwrite the only project file.
2. Prefer **`--dry-run`** before the first real write in a session.
3. Always request **`--report path.json`** so results are machine-readable.
4. Treat report JSON as source of truth; stderr is for humans.
5. Do **not** mutate machine/process/filament profile files during paint (INV-008).
6. Selection: run `info` first if the project has multiple volumes.
7. Coordinate space: default volume-local look/up; use `--view` presets when unsure.

## Binaries

| Tool | Role | Status |
|---|---|---|
| `orcgraffiti` | info / paint / verify | **Planned** |
| `orca-slicer` | slice / export / existing CLI | **Exists** in this fork |

Windows example paths (dev tree):

```text
F:\Ai\OrcGraffiti\build\src\Release\orca-slicer.exe
# orcgraffiti.exe — TBD after AS-1
```

## Cookbook

### 1. Inspect

```bash
orcgraffiti info project.3mf --report info.json
```

Read `objects[].volumes[]` for `triangle_count`, `bbox_mm`, `has_mmu_paint`, `fingerprint`.

### 2. Dry-run paint

```bash
orcgraffiti paint project.3mf \
  --image garth.jpg \
  --view front \
  --auto-fit \
  --colors 4 \
  --dry-run \
  --report paint_dry.json
```

Check `ok`, `diagnostics.painted_faces`, `warnings`.

### 3. Write painted 3MF

```bash
orcgraffiti paint project.3mf \
  --image garth.jpg \
  --view front \
  --auto-fit \
  --colors 4 \
  --out project_painted.3mf \
  --force \
  --report paint.json
```

### 4. Verify

```bash
orcgraffiti verify project_painted.3mf --report verify.json
```

### 5. Slice (existing Orca CLI)

```bash
orca-slicer --slice 0 --export-3mf project_sliced.gcode.3mf project_painted.3mf
```

Confirm flags against `orca-slicer --help` for the installed build. Setting priority (from Orca help text): CLI > `--load_settings`/`--load_filaments` > 3MF.

## Important flags (paint)

| Flag | Meaning |
|---|---|
| `--image` | PNG/JPG/BMP path |
| `--out` | Output 3MF (required unless dry-run) |
| `--dry-run` | Report only |
| `--auto-fit` | Fit projector to mesh (default on) |
| `--view` | `front` `back` `left` `right` `top` `bottom` |
| `--look` / `--up` | Explicit direction vectors |
| `--colors` | Quantizer target 1–16 |
| `--object` / `--volume` | Selection indices |
| `--merge overwrite\|preserve` | Existing paint policy |
| `--report` | JSON report path |

Full contract: `docs/OrcGraffiti/Agent_Surface.md` §6.

## Failure remediation

| Symptom | Action |
|---|---|
| Image open/decode failed | Check path; JPG needs libjpeg path; try PNG |
| No filaments | Pass `--filaments palette.json` or use 3MF with colors |
| painted_faces == 0 | Wrong `--view`/`--look`; try `top`/`front`; ensure model faces camera direction |
| Fingerprint mismatch | Mesh changed between dry-run and write; re-run from current file |
| Ambiguous volume | `info` then `--object` / `--volume` |

## Sample assets (dev)

- Image: `C:\Users\hardc\OneDrive\Pictures\garth.jpg` (user machine)
- Copy used in tests: `build/garth.jpg` when present

## See also

- `docs/OrcGraffiti/Agent_Surface.md` — full design
- `docs/OrcGraffiti/Project_Truth.md` — product invariants
- `docs/OrcGraffiti/DECISIONS.md` — ADR-0010
