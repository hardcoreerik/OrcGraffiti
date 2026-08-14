---
title: OrcGraffiti Agent Surface
status: Design — not yet implemented
last_researched: 2026-08-06
related:
  - Project_Truth.md (authoritative product truth)
  - Architechture.md (core pipeline layers)
  - Roadmap.md (gated phases)
  - DECISIONS.md (ADR-0010)
---

# OrcGraffiti Agent Surface

## Document purpose

This document specifies how **humans and AI agents** (Claude, Grok, Codex, Cursor, and similar tools) should gain machine-usable access to:

1. **OrcGraffiti Image Paint** (project-owned, headless-first);
2. **Existing OrcaSlicer CLI capabilities** already in this codebase (slice, transform, export);
3. Optional later delivery layers: **agent skills**, **MCP tools**, and **Python bindings**.

**Status:** design and contract only. Implementation is deferred. When implementation begins, this file is the exit-gate specification for the agent surface.

**Authority:** `Project_Truth.md` still wins on product and safety invariants. This file wins on CLI/MCP/skill contracts unless a maintainer updates it.

---

## 1. Executive summary

### 1.1 Goal

Give AI agents the ability to:

```text
read a model / project
  → paint an image onto mesh faces as MMU filament states
  → optionally slice / export
  → return machine-readable JSON + files
```

without opening the GUI, clicking gizmos, or holding live `ModelVolume*` pointers across threads.

### 1.2 Strategy in one sentence

**Ship a headless, file-based CLI that reuses the existing ImagePaint core; document it as a skill; optionally wrap it as MCP.** Do not automate the wxWidgets GUI.

### 1.3 Delivery stack (priority order)

| Priority | Layer | Role | Depends on |
|---:|---|---|---|
| **P0** | **CLI binary** (`orcgraffiti` and/or extended `orca-slicer` actions) | Source of truth for machine I/O | `libslic3r` ImagePaint + 3MF I/O |
| **P1** | **Agent skill** (`SKILL.md`) | Teaches models when/how to call the CLI | P0 |
| **P2** | **MCP server** | Tool schema for MCP hosts (Claude Desktop, Cursor, …) | P0 (thin wrapper) |
| **P3** | **Python package / host bridge** | Notebooks and Python agent runtimes | P0 |
| **✗** | GUI RPA / screenshot clicking | Explicit non-goal | — |

```text
┌──────────────────────────────────────────────────────────────┐
│  Agent (Claude / Grok / Codex / Cursor / custom orchestrator)│
└────────────────────────────┬─────────────────────────────────┘
                             │  shell or MCP
┌────────────────────────────▼─────────────────────────────────┐
│  Skill docs  │  MCP tools  │  scripts / CI                   │
└────────────────────────────┬─────────────────────────────────┘
                             │  subprocess / in-process
┌────────────────────────────▼─────────────────────────────────┐
│  orcgraffiti CLI  (+ existing orca-slicer CLI for slice)     │
│  exit codes · JSON reports · file artifacts                  │
└────────────────────────────┬─────────────────────────────────┘
                             │
┌────────────────────────────▼─────────────────────────────────┐
│  libslic3r::ImagePaint  ·  TriangleSelector  ·  3MF I/O      │
│  (GUI-independent core — already largely implemented)        │
└──────────────────────────────────────────────────────────────┘
```

---

## 2. Problem statement

### 2.1 Why the GUI is not enough for AI

Interactive Image Paint (gizmo) is correct for humans:

- Browse image, face the model, Apply, Undo.

AI agents fail at that path because they:

- cannot reliably see/control complex desktop UI across platforms;
- cannot reason about transient camera matrices without structure;
- need **deterministic, repeatable, CI-testable** operations;
- need **structured errors** (not a modal dialog);
- often run headless (SSH, CI, cloud VMs).

### 2.2 Why “full Orca for AI” is too large as a first cut

OrcaSlicer is a full FDM suite: profiles, AMS mapping, plates, supports, calibration, cloud hosts, etc. Exposing “everything” as agent tools creates:

- huge attack surface (profile corruption, print-host actions);
- enormous prompt/tool-schema noise;
- safety review burden.

**Agent surface v1 is deliberately narrow:** inspect + paint (+ reuse existing slice CLI as a second skill chapter).

### 2.3 What already exists in-repo (implementation inventory)

| Capability | Location | Agent-ready today? |
|---|---|---|
| Planar image→face pipeline | `src/libslic3r/ImagePaint/*` | **Yes** via unit tests; not yet as product CLI |
| Topology fingerprint | `TopologyFingerprint` | Yes (core) |
| JPEG decode without OpenCV JPEG | `ImageDecoder` + libjpeg fallback | Yes |
| Auto-fit projection | `fit_planar_projection` | Yes (core + gizmo) |
| Interactive apply | `GLGizmoImagePainter` / `ImagePaintJob` | Human only |
| Existing slice/export CLI | `CLI` in `OrcaSlicer.cpp`, defs in `PrintConfig.cpp` | **Partial** — slice exists, paint does not |
| Unit tests | `tests/libslic3r/test_image_paint_*` | CI gate for core |

---

## 3. Research survey (prior art to borrow)

This section is the research basis for the design. Nothing here is copied as code; patterns are adopted.

### 3.1 Slicer family CLIs (same product lineage)

#### PrusaSlicer / Slic3r CLI

- **Source:** [PrusaSlicer wiki — Command Line Interface](https://github.com/prusa3d/PrusaSlicer/wiki/Command-Line-Interface); historical Slic3r manual CLI chapter.
- **Pattern:** single process, flags + input paths, print-option overrides, export actions.
- **Lesson for OrcGraffiti:** agents already expect `slicer [options] files…` shape; do not invent an alien UX for slice. **Do** invent a clear subcommand or dedicated binary for paint.

#### OrcaSlicer CLI (this fork’s upstream behavior)

- **Source:** `src/libslic3r/PrintConfig.cpp` (`CLIActionsConfigDef`, `CLITransformConfigDef`, `CLIMiscConfigDef`); runtime help in `CLI::print_help` (`Usage: orca-slicer [ OPTIONS ] [ file.3mf/file.stl ... ]`).
- **Community usage:** [OrcaSlicer discussion #8593](https://github.com/OrcaSlicer/OrcaSlicer/discussions/8593) (CLI slice + export-3mf); flag catalogs such as [Printago Slicer CLI Reference](https://printago.io/slicer-cli).
- **Lesson:** Orca already has a headless slice path. Agent “slice” skills should call **existing** flags first, not reimplement Print.

**Inventory of relevant existing actions (from this tree’s `PrintConfig.cpp`):**

| Option key | Role (from tooltip/label) |
|---|---|
| `slice` | Slice plates: `0` = all, `i` = plate i |
| `export_3mf` | Export project as 3MF |
| `export_stl` / `export_stls` | Mesh export |
| `export_slicedata` / `load_slicedata` | Cached slice data |
| `info` | Output model information |
| `export_settings` | Export settings JSON |
| `pipe` | Progress pipe name |
| `no_check` | Skip some validity checks |
| `arrange` / `orient` / `rotate*` / `scale` | Transforms |
| `load_settings` / `load_filaments` | Profile JSON injection |
| `datadir` | Alternate profile data dir |

**Print setting priority (documented in `print_help`):**

1. CLI setting values (highest);
2. `--load_settings` / `--load_filaments`;
3. values from 3MF (lowest).

#### Bambu Studio CLI

- **Source:** [BambuStudio wiki — Command Line Usage](https://github.com/bambulab/BambuStudio/wiki/Command-Line-Usage) (examples for `--slice`, `--export-3mf`, multi-filament JSON loads).
- **Lesson:** multi-file settings as semicolon-separated JSON paths; explicit plate index; progress pipe. Good model for **filament list injection** when painting without a full project GUI.

### 3.2 Engine-without-GUI pattern

#### CuraEngine + wrappers

- **CuraEngine:** headless slice engine; GUI is separate.
- **[makermate/curaengine-slicer-api](https://github.com/makermate/curaengine-slicer-api):** Docker + FastAPI around CuraEngine (async jobs, upload STL → gcode).
- **Lesson:** ImagePaint core already matches CuraEngine’s role. Optional HTTP/MCP layers should wrap a stable CLI/library, not the reverse.

### 3.3 AI-agent control of 3D software

#### Blender MCP ecosystem

- **[ahujasid/blender-mcp](https://github.com/ahujasid/blender-mcp):** MCP → Blender addon socket → `bpy` tools for Claude et al.
- **Follow-ons:** multi-agent Blender controllers (e.g. 3D-Agent discussions) that add plan → act → **verify** (viewport screenshot).
- **Lesson for OrcGraffiti:**
  - Prefer **small, named tools** (`paint`, `info`, `slice`) over arbitrary code execution.
  - Always return **structured results**.
  - Add a **verify** step after mutation (diagnostics JSON, optional fingerprint check).
  - Community feedback on Blender-MCP: vague tools and huge state make agents flaky — keep the tool surface small.

#### FreeCAD headless / MCP

- **FreeCADCmd / `FreeCAD -c`:** headless Python ([Headless FreeCAD wiki](https://wiki.freecad.org/Headless_FreeCAD)).
- **mcp-freecad** and skill libraries for FreeCAD scripting.
- **Lesson:** CAD automation for agents is mature via **scripts + headless interpreter**, not GUI RPA.

#### OpenSCAD CLI

- Fully file-driven parametric pipeline (`openscad -o out.stl in.scad -D param=…`).
- **Lesson:** pure **file in → file out** is the most agent-reliable contract.

### 3.4 What research does *not* show

No mature public project was found that exposes:

> headless **image → per-face MMU filament painting** with JSON reports for agents

as a product CLI. Adjacent pieces exist (slice CLIs, GUI paint tools, our unit-tested core). **OrcGraffiti can own this niche.**

### 3.5 Pattern synthesis (adopted design rules)

| Rule | From |
|---|---|
| GUI-independent core | CuraEngine split; OrcGraffiti ADR-0004 |
| File → file + structured log | OpenSCAD, Bambu CLI examples |
| Reuse existing slice CLI | Prusa/Orca/Bambu |
| Thin MCP over CLI | Blender-MCP architecture (adapter, not engine) |
| Small tool surface + verify | Blender agent post-mortems |
| Never overwrite without explicit out path | Production automation hygiene |
| Immutable snapshots + fingerprint | Project_Truth INV-004 / architecture |

---

## 4. Goals and non-goals

### 4.1 Goals (v1 agent surface)

1. **Headless paint:** apply Image Paint to a volume in a 3MF (or mesh + sidecar) and write a new 3MF whose `mmu_segmentation_facets` reopen in the GUI.
2. **Inspect:** list objects/volumes, triangle counts, bbox, existing paint presence, filament colors when available.
3. **Machine reports:** JSON diagnostics compatible with agent reasoning (painted faces, areas, warnings, fingerprint).
4. **Composable with slice:** document how to call existing `orca-slicer --slice …` after paint.
5. **Safe defaults:** no printer/profile mutation; no live model pointers; fingerprint check before write; bounded image decode.
6. **Agent docs:** skill file sufficient for Claude/Grok/Codex without reading C++.

### 4.2 Non-goals (v1)

- GUI automation / OCR of the Orca window.
- Remote print-host control (send to printer, AMS cloud, etc.).
- Arbitrary profile authoring or printer-vendor lock-in.
- Topology-changing remesh as part of paint (Project_Truth: separate plan type later).
- Full parity with every gizmo feature (handles, live preview render).
- Replacing the interactive gizmo (human UX remains).

### 4.3 Invariants carried from Project_Truth

| ID | Invariant | Agent surface implication |
|---|---|---|
| INV-001 | One face state per triangle | Report length must equal face count |
| INV-002 | Zero = unpainted | JSON uses same state encoding as Orca |
| INV-003 | States in Extruder1..Max | Reject out-of-range states |
| INV-004 | Validate IDs + topology fingerprint | Refuse apply if mesh changed |
| INV-005 | Unmasked faces preserve prior paint | Merge policy configurable |
| INV-006 | Cancel leaves model unchanged | Cancel mid-job → no write |
| INV-007 | One undo snapshot per Apply | CLI has no undo stack; use file versions instead |
| INV-008 | No printer/profile mutation | Paint CLI must not rewrite machine/filament profiles |
| INV-009 | Named coordinate spaces | CLI flags document space (mesh-local vs world) |
| INV-010 | Explicit thread ownership | CLI is single-process; workers internal only |
| INV-011 | Preview disposable | Optional `--dry-run` produces report without write |

---

## 5. Architecture

### 5.1 Logical components

```text
┌─────────────────────┐
│ Agent / Skill / MCP │
└──────────┬──────────┘
           │ argv + files
┌──────────▼──────────┐
│  CLI front-end      │  parse · validate · I/O · exit codes
└──────────┬──────────┘
           │ ImagePaintRequest (immutable)
┌──────────▼──────────┐
│  ImagePaint core    │  decode · fit · sample · quantize · match · clean
└──────────┬──────────┘
           │ FacePaintPlan
┌──────────▼──────────┐
│  Apply adapter      │  load 3MF · TriangleSelector · set facets · save 3MF
└──────────┬──────────┘
           │
     painted.3mf + report.json
```

### 5.2 Binary packaging options (decide at implementation)

| Option | Description | Pros | Cons |
|---|---|---|---|
| **A. Sibling binary `orcgraffiti`** | Links `libslic3r` (+ minimal I/O), no wx | Small, headless-clean | Second artifact to ship |
| **B. New CLI actions on `orca-slicer`** | e.g. `--image-paint` next to `--slice` | One binary users already have | Heavier process; GUI baggage on some platforms |
| **C. Hybrid** | `orcgraffiti` for paint/info; document `orca-slicer` for slice | Clear separation | Two commands for agents |

**Recommendation (design default):** **C — hybrid.**  
Paint/info is new product surface under OrcGraffiti control; slice remains upstream-style `orca-slicer` CLI. Skills teach both.

### 5.3 Coordinate spaces (must appear in CLI help)

From Project_Truth §14:

```text
mesh-local  →  volume transform  →  object  →  instance  →  world  →  projector  →  UV
```

**CLI paint v1 contract:**

- Mesh snapshot is **volume-local** (matches `ModelVolume::mesh()` and TriangleSelector).
- `--look` / `--up` are interpreted in **volume-local** unless `--space=world` is set and transforms are applied by the CLI.
- `--auto-fit` runs `fit_planar_projection` in the same space as vertices.
- Implementation must document which path was used in the JSON report (`projection.space`).

Gizmo today transforms world camera → local via `inverse(world_matrix).linear()`. CLI has no camera; agents supply look/up or use axis presets (`front`, `top`, `right`).

### 5.4 Threading model

| Context | Thread | Notes |
|---|---|---|
| CLI process | Main | Single invocation = one paint job |
| Internal sampling | TBB worker pool | Same as `FaceSampler` |
| GUI gizmo (unchanged) | Worker job + UI finalize | Not used by agent surface |

Agents never see worker handles. Cancellation is process kill or cooperative cancel if multi-stage CLI is added later.

---

## 6. CLI contract (normative design)

### 6.1 Command overview (proposed)

```text
orcgraffiti <command> [options]

Commands:
  version                 Print version / git hash / build type
  help                    Help
  info      <input>       Inspect model/project (JSON or text)
  fingerprint <input>     Topology fingerprints per volume
  paint     <input>       Image paint → output 3MF (+ report)
  verify    <input>       Sanity-check painted facets / report consistency

# Slice remains on existing binary (documented, not reimplemented):
orca-slicer [OPTIONS] file.3mf   # --slice, --export-3mf, ...
```

### 6.2 Exit codes

| Code | Meaning | Agent action |
|---:|---|---|
| 0 | Success | Read report + outputs |
| 1 | Usage / validation error (bad flags, missing file) | Fix args; do not retry same command |
| 2 | Domain error (decode fail, no filaments, fingerprint mismatch, empty mesh) | Read `error` in report; adjust inputs |
| 3 | I/O error (cannot write output, permission) | Check paths/permissions |
| 4 | Internal / unexpected | Log stderr; file bug |
| 130 | Interrupted (SIGINT) | No partial output written (or partial marked dirty) |

**Rule:** on non-zero exit, if `--report` was requested, still attempt to write a report with `"ok": false` when possible.

### 6.3 Global options

| Flag | Type | Description |
|---|---|---|
| `--report <path.json>` | path | Write machine-readable report (always preferred for agents) |
| `--json-stdout` | bool | Emit report JSON on stdout (mutually exclusive with human logs on stdout) |
| `--quiet` | bool | Suppress human progress; errors still on stderr |
| `--verbose` | bool | Extra diagnostics on stderr |
| `--datadir <path>` | path | Align with Orca profile data dir when loading project context |

### 6.4 `info` command

**Purpose:** let agents discover volumes before painting.

```bash
orcgraffiti info project.3mf --report info.json
orcgraffiti info model.stl --report info.json
```

**Report sketch:**

```json
{
  "ok": true,
  "command": "info",
  "input": "project.3mf",
  "objects": [
    {
      "index": 0,
      "name": "Benchy",
      "volumes": [
        {
          "index": 0,
          "name": "Benchy",
          "volume_id": "…",
          "triangle_count": 225154,
          "vertex_count": 112800,
          "bbox_mm": { "min": [0,0,0], "max": [60,31,48] },
          "has_mmu_paint": false,
          "fingerprint": {
            "connectivity_hash": "…",
            "geometry_hash": "…",
            "vertex_count": 112800,
            "triangle_count": 225154
          }
        }
      ]
    }
  ],
  "filaments": [
    { "index": 0, "color_hex": "#FF0000", "name": "…" }
  ]
}
```

### 6.5 `paint` command (core)

#### Synopsis

```bash
orcgraffiti paint INPUT \
  --image PATH \
  --out OUTPUT.3mf \
  --report report.json \
  [selection] [projection] [color] [merge] [decode]
```

#### Selection

| Flag | Description |
|---|---|
| `--object <i>` | Object index (default 0) |
| `--volume <i>` | Volume index within object (default 0) |
| `--volume-id <id>` | Prefer stable ObjectID when available |
| `--require-single-volume` | Fail if project has multiple volumes and selection ambiguous |

#### Image and decode

| Flag | Description |
|---|---|
| `--image <path>` | PNG / JPG / JPEG / BMP (decoder limits apply) |
| `--max-pixels <n>` | Override `ImageDecodeLimits::max_pixels` |
| `--max-width` / `--max-height` | Dimension caps |

JPEG support must use the **libjpeg path** when OpenCV is built without JPEG (see ImageDecoder design already in tree).

#### Projection

| Flag | Description |
|---|---|
| `--auto-fit` | Default **true**. Call `fit_planar_projection` |
| `--look <x,y,z>` | Look direction (volume-local by default) |
| `--up <x,y,z>` | Up hint |
| `--view <preset>` | `front` \| `back` \| `left` \| `right` \| `top` \| `bottom` (sets look/up) |
| `--width-mm` / `--height-mm` | Manual plane size; implies disable auto-fit unless both auto-fit and override policy defined |
| `--aspect-from-image` | Default true when auto-fit |
| `--margin <f>` | Fit margin (default 1.02) |
| `--rotation-deg <a>` | In-plane rotation |
| `--mirror-u` / `--mirror-v` | Mirrors |
| `--front-face-cosine <t>` | Default ~0.05 |
| `--min-coverage <t>` | Default ~0.25 |
| `--paint-through` | Allow back faces |
| `--space <mesh-local\|world>` | Default `mesh-local` |

**Presets (volume-local, right-handed, Z-up bed convention):**

| Preset | look | up |
|---|---|---|
| `front` | (0, −1, 0) or (0, +1, 0) — **must match mesh convention; document after golden tests** | (0, 0, 1) |
| `top` | (0, 0, −1) | (0, 1, 0) |
| `right` | (−1, 0, 0) | (0, 0, 1) |

> Implementation note: golden tests on the unit cube in `test_image_paint_pipeline.cpp` already establish front-view look `(0,1,0)` with up `(0,0,1)` for the test cube’s front at y=0. CLI presets must align with those conventions and be locked by tests.

#### Color / matching

| Flag | Description |
|---|---|
| `--colors <n>` | Target quantizer colors (1–16) |
| `--filaments-from-project` | Default true when input is 3MF with filament colors |
| `--filaments <json>` | Explicit palette file (see §6.7) |
| `--quality <fast\|threepoint\|gaussian7>` | Sampling quality |
| `--merge <overwrite\|preserve>` | Merge policy |

#### Output / safety

| Flag | Description |
|---|---|
| `--out <path.3mf>` | **Required** for write mode |
| `--dry-run` | Run pipeline; write report only; no 3MF mutation |
| `--force` | Allow overwriting `--out` if exists |
| `--keep-other-volumes` | Default true: pass through untouched volumes |
| `--fail-on-warning` | Non-zero exit if diagnostics contain warnings |

#### Example agent session

```bash
# 1. Inspect
orcgraffiti info benchy.3mf --report /tmp/info.json

# 2. Paint (dry-run first)
orcgraffiti paint benchy.3mf \
  --image garth.jpg \
  --view front \
  --auto-fit \
  --colors 4 \
  --dry-run \
  --report /tmp/paint_dry.json

# 3. Commit write
orcgraffiti paint benchy.3mf \
  --image garth.jpg \
  --view front \
  --auto-fit \
  --colors 4 \
  --out benchy_painted.3mf \
  --force \
  --report /tmp/paint.json

# 4. Slice with existing Orca CLI (illustrative)
orca-slicer --slice 0 --export-3mf benchy_sliced.gcode.3mf benchy_painted.3mf
```

### 6.6 `verify` command

```bash
orcgraffiti verify painted.3mf --report verify.json
orcgraffiti verify painted.3mf --expect-report /tmp/paint.json
```

Checks:

- volume still exists;
- face state vector length == triangle count;
- states ∈ valid range;
- optional match against prior report fingerprint.

### 6.7 Filament palette JSON

```json
{
  "filaments": [
    { "index": 0, "name": "PLA Red", "color_hex": "#E31C23" },
    { "index": 1, "name": "PLA Black", "color_hex": "#1A1A1A" },
    { "index": 2, "name": "PLA White", "color_hex": "#F5F5F5" },
    { "index": 3, "name": "PLA Skin", "color_hex": "#C8A078" }
  ]
}
```

Indices are **0-based project filament indices**. Conversion to Orca selector state (1-based) remains centralized in `FilamentMatcher` (Project_Truth).

### 6.8 Paint report JSON schema (normative sketch)

```json
{
  "ok": true,
  "command": "paint",
  "version": "orcgraffiti-agent-surface-0.1",
  "input": "benchy.3mf",
  "output": "benchy_painted.3mf",
  "dry_run": false,
  "selection": {
    "object_index": 0,
    "volume_index": 0,
    "volume_id": "…"
  },
  "image": {
    "path": "garth.jpg",
    "width_px": 692,
    "height_px": 994,
    "aspect_w_over_h": 0.696
  },
  "projection": {
    "space": "mesh-local",
    "auto_fit": true,
    "look": [0.0, 1.0, 0.0],
    "up": [0.0, 0.0, 1.0],
    "width_mm": 62.4,
    "height_mm": 89.6,
    "front_face_cosine_threshold": 0.05,
    "minimum_coverage": 0.25
  },
  "fingerprint": {
    "connectivity_hash": "…",
    "geometry_hash": "…",
    "vertex_count": 112800,
    "triangle_count": 225154
  },
  "diagnostics": {
    "total_faces": 225154,
    "candidate_faces": 12000,
    "painted_faces": 8432,
    "transparent_faces": 0,
    "back_facing_faces": 110000,
    "painted_surface_area_mm2": 4512.3,
    "coarse_mesh_warning": false,
    "warnings": []
  },
  "matches": [
    { "cluster_id": 0, "filament_index": 2, "delta_e": 3.1 }
  ],
  "timings_ms": {
    "decode": 12,
    "sample": 840,
    "quantize": 40,
    "total": 920
  },
  "error": null
}
```

Failure example:

```json
{
  "ok": false,
  "command": "paint",
  "error": {
    "code": "ImageOpenFailed",
    "message": "Could not open or decode image.",
    "detail": "C:\\\\path\\\\garth.jpg"
  }
}
```

Error codes should map 1:1 from `ImagePaintErrorCode` where applicable.

---

## 7. Relationship to existing `orca-slicer` CLI

### 7.1 What agents can already do (once binary is on PATH)

Without any new code, a skill can document:

```bash
# Model info (existing)
orca-slicer --info model.3mf

# Slice all plates and export (existing pattern; exact flags per build)
orca-slicer --slice 0 --export-3mf out.gcode.3mf project.3mf

# Load external settings (existing)
orca-slicer --load-settings "machine.json;process.json" \
  --load-filaments "fila0.json;fila1.json" \
  --slice 0 --export-3mf out.3mf model.stl
```

Implementation phase must **snapshot actual `--help` output** from the built `orca-slicer.exe` into `docs/OrcGraffiti/CLI_ORCA_BASELINE.md` (version-pinned). This document intentionally lists options from **source** (`PrintConfig.cpp`) because Windows help attachment can be flaky in automated capture.

### 7.2 What must be new

| Gap | Why agents need it |
|---|---|
| Image paint action | Core exists; no CLI action today |
| Stable JSON paint report | Agents need structured diagnostics |
| Volume selection by index/id for paint | Multi-volume projects |
| Dry-run paint | Safe planning |
| Explicit no-profile-mutation guarantee | INV-008 |

### 7.3 Naming collision policy

- Prefer `orcgraffiti paint` over overloading `orca-slicer --slice` semantics.
- If paint is added to `orca-slicer`, use a dedicated action name (`image_paint` / `--image-paint`) never aliased to slice.

---

## 8. Agent skill specification (P1)

### 8.1 Purpose

A **skill** is documentation an agent loads before acting. It does not execute code. It must be accurate enough that models do not invent flags.

### 8.2 Proposed skill locations

| Consumer | Path (proposed) |
|---|---|
| Repo (source of truth) | `docs/OrcGraffiti/skills/orcgraffiti-cli/SKILL.md` |
| Grok Build | copy/symlink into user skills dir when packaging |
| Claude Code | `.claude/skills/orcgraffiti-cli/SKILL.md` or project instruction pointer |
| Codex | `AGENTS.md` section + skill file |

### 8.3 Skill content outline (required sections)

1. **Name / description / when to use**
2. **Prerequisites** (binary path, resources dir, sample assets)
3. **Hard rules** (no profile mutation, always `--out`, prefer `--dry-run` first)
4. **Command cookbook** (info → dry-run paint → paint → verify → slice)
5. **JSON field meanings**
6. **Failure table** (error code → remediation)
7. **Coordinate / view presets**
8. **Safety** (paths, overwrites, large images)
9. **Non-goals** (do not attempt GUI)

### 8.4 Draft skill body (documentation-only template)

The following is **specification text** for a future `SKILL.md`, not an installed skill yet:

```markdown
# orcgraffiti-cli

Use when the user wants to paint an image onto a 3D model as multicolor
MMU face paint, inspect a 3MF, or chain paint → slice without the GUI.

## Binary
- Paint/info: `orcgraffiti` (build path TBD)
- Slice: `orca-slicer` (existing)

## Workflow
1. `orcgraffiti info INPUT --report info.json`
2. Choose --volume / --view
3. `orcgraffiti paint INPUT --image IMG --view front --auto-fit --dry-run --report dry.json`
4. If dry ok: same with `--out OUT.3mf --force`
5. Optional: `orca-slicer --slice 0 --export-3mf OUT_SLICED.3mf OUT.3mf`

## Never
- Overwrite INPUT without a new --out path
- Invent CLI flags not in this skill
- Mutate printer profiles
- Click the GUI on behalf of automation
```

---

## 9. MCP server specification (P2)

### 9.1 Design principle

MCP is a **transport**, not a second business logic layer.

```text
MCP tool call  →  spawn orcgraffiti …  →  parse report JSON  →  tool result
```

Optional later: in-process link for speed.

### 9.2 Tools (v1)

| Tool name | Maps to | Inputs (JSON) | Output |
|---|---|---|---|
| `orcgraffiti_info` | `info` | `input_path` | report object |
| `orcgraffiti_paint` | `paint` | `input_path`, `image_path`, `out_path`, projection, colors, `dry_run` | report object |
| `orcgraffiti_verify` | `verify` | `input_path` | report object |
| `orca_slice` | `orca-slicer --slice …` | `input_path`, `out_path`, `plate` | paths + stderr tail |

### 9.3 Tool schema principles

- All paths absolute or workspace-relative with documented base.
- Default `dry_run=true` for paint tool in cautious profiles; hosts may override.
- Cap image size server-side (reuse decode limits).
- Never expose print-host credentials tools in v1.

### 9.4 Host install sketch (non-normative)

```json
{
  "mcpServers": {
    "orcgraffiti": {
      "command": "orcgraffiti-mcp",
      "args": ["--cli", "C:/path/to/orcgraffiti.exe"]
    }
  }
}
```

---

## 10. Python API (P3, deferred detail)

Future binding surface (aligns with Architecture’s optional `orca.host.paint`):

```python
from orcgraffiti import paint, info

report = paint(
    input="benchy.3mf",
    image="garth.jpg",
    out="benchy_painted.3mf",
    view="front",
    auto_fit=True,
    colors=4,
    dry_run=False,
)
assert report["ok"]
```

Implementation choices later: pybind11 vs CLI subprocess. **Subprocess first** keeps ABI simple.

---

## 11. Security, safety, and multi-agent use

### 11.1 Threat model (agents)

| Threat | Mitigation |
|---|---|
| Overwrite user’s only 3MF | Require `--out`; refuse same path as input unless `--allow-in-place` (default off) |
| Path traversal / arbitrary file write | Normalize paths; optional allowlist root |
| Decode bombs | Existing `ImageDecodeLimits` |
| Profile / printer corruption | Paint path does not write machine/filament JSON (INV-008) |
| Stale mesh apply | Fingerprint check before write |
| Huge jobs DoS | Face count warnings; optional time budget flag |
| Prompt injection via filenames | Treat paths as data; no shell interpolation in MCP server |

### 11.2 Human vs agent undo

| Context | Undo model |
|---|---|
| GUI gizmo | One `take_snapshot` (INV-007) |
| CLI | **Files are the undo stack** — keep input; write new output |

Agents must not assume CLI can Ctrl-Z inside a project file.

### 11.3 Logging

- **stderr:** human progress and warnings  
- **report JSON:** machine truth  
- Never put secrets in reports  

---

## 12. Testing strategy (for when implementation starts)

### 12.1 Layers

| Layer | What | Gate |
|---|---|---|
| Core unit tests | Existing `ctest -L ImagePaint` | Already green (69+) |
| CLI contract tests | Golden JSON for `info`/`paint --dry-run` on fixture cube | New |
| Round-trip | paint → load 3MF → face states match plan | New |
| Integration | optional `garth.jpg` if present (pattern already in pipeline tests) | New |
| Skill lint | flags mentioned in SKILL.md exist in `--help` | New |
| MCP smoke | tool list + one dry-run paint | New |

### 12.2 Fixtures

- `tests/orcgraffiti_cli/fixtures/unit_cube.3mf` (or generate)
- solid color PNG in repo (no OneDrive dependency)
- optional external JPG for soak tests

### 12.3 Exit-gate for “Agent Surface Phase complete”

1. `orcgraffiti paint --dry-run` on fixture returns `ok: true` with stable diagnostics.  
2. `orcgraffiti paint --out` produces 3MF that GUI opens with visible MMU paint.  
3. Fingerprint mismatch refuses write (tested).  
4. JPEG and PNG both documented and tested.  
5. Skill file checked in; human can follow cookbook without C++ knowledge.  
6. Slice path documented against **version-pinned** `orca-slicer --help` dump.

---

## 13. Phased delivery plan (gated)

This is a **parallel track** to Roadmap phases 0–6 (MVP GUI). It may start after ImagePaint core is green (already true).

| Phase | Name | Deliverable | Exit gate |
|---|---|---|---|
| **AS-0** | Spec freeze | This document + ADR accepted | Maintainer ack |
| **AS-1** | CLI skeleton | `orcgraffiti version/help/info` | info JSON on fixture |
| **AS-2** | Headless paint dry-run | `paint --dry-run` | matches unit pipeline diagnostics within tolerance |
| **AS-3** | 3MF apply/write | `paint --out` | GUI reopen proof |
| **AS-4** | Skill | `SKILL.md` + cookbook | Agent dry-run success recorded |
| **AS-5** | MCP thin wrap | paint/info tools | one host integration note |
| **AS-6** | Slice chapter | skill documents `orca-slicer` slice | end-to-end paint→slice artifact |
| **AS-7** | Hardening | timeouts, path policy, CI | AS tests in CI |

**Do not** implement AS-5 before AS-3.

---

## 14. Mapping from current code to CLI (implementation cheat-sheet)

| CLI concern | Existing symbol |
|---|---|
| Decode image | `ImagePaint::decode_image` |
| Fit projection | `ImagePaint::fit_planar_projection` |
| Run pipeline | `ImagePaint::run_image_paint` |
| Fingerprint | `ImagePaint::fingerprint` / plan.fingerprint |
| Face states | `FacePaintPlan::states` |
| Apply facets | `TriangleSelector` + `ModelVolume::mmu_segmentation_facets` (see gizmo job finalize) |
| Filament index conversion | `FilamentMatcher::filament_index_to_selector_state` |
| Merge policies | `PaintStateMerge` / `MergePolicy` |
| Cancel | `std::function<bool()>` already on pipeline |
| Existing Orca CLI | `CLI` class, `cli_*_config_def` |

**Missing glue (to implement later):**

- Load 3MF → select volume → snapshot vertices/indices/existing_states → build `ImagePaintRequest`.
- After plan: write facets → save 3MF without destroying other project data.
- Filament color extraction from project (GUI uses `get_extruder_colors_from_plater_config`; CLI needs a libslic3r-side equivalent).

---

## 15. Human workflow vs agent workflow

| Step | Human (gizmo) | Agent (CLI) |
|---|---|---|
| Choose model | Selection | `--object/--volume` after `info` |
| Choose image | Browse dialog | `--image path` |
| Aim projector | Orbit camera + auto-fit | `--view` / `--look` + `--auto-fit` |
| Colors | Spinner | `--colors` + filament source |
| Preview | (future) | `--dry-run` + report |
| Apply | Apply button + undo snapshot | `--out` new file |
| Slice | Slice button | `orca-slicer --slice …` |

---

## 16. Documentation set (this feature)

| Doc | Role |
|---|---|
| **This file** (`Agent_Surface.md`) | Master design + contracts |
| `DECISIONS.md` ADR-0010 | Accept hybrid CLI strategy |
| `Roadmap.md` Agent Surface phases | Gating |
| Future `CLI_ORCA_BASELINE.md` | Pinned `orca-slicer --help` dump |
| Future `skills/orcgraffiti-cli/SKILL.md` | Agent-facing cookbook |
| Future OpenAPI/JSON Schema files | Optional formal schemas under `docs/OrcGraffiti/schemas/` |

---

## 17. Open questions (resolve at implementation kickoff)

1. **Binary name and install layout** on Windows/macOS/Linux.  
2. Exact **front/back look vectors** for real Orca mesh orientation vs unit-cube tests.  
3. Whether paint CLI needs **full project plate context** or volume-only 3MF subset.  
4. Filament color source when 3MF lacks display colors.  
5. MCP default `dry_run` policy for consumer hosts.  
6. Whether to vendor a minimal sample 3MF+PNG in-repo for CI (license of assets).  

---

## 18. References

### In-tree

- `docs/OrcGraffiti/Project_Truth.md`
- `docs/OrcGraffiti/Architechture.md`
- `docs/OrcGraffiti/Roadmap.md`
- `src/libslic3r/ImagePaint/*`
- `src/libslic3r/PrintConfig.cpp` (`CLIActionsConfigDef` et al.)
- `src/OrcaSlicer.cpp` (`CLI::print_help`, `CLI::run`)
- `src/slic3r/GUI/Gizmos/GLGizmoImagePainter.cpp`
- `tests/libslic3r/test_image_paint_*`

### External (research)

- PrusaSlicer CLI wiki: https://github.com/prusa3d/PrusaSlicer/wiki/Command-Line-Interface  
- OrcaSlicer CLI discussion: https://github.com/OrcaSlicer/OrcaSlicer/discussions/8593  
- Bambu Studio CLI wiki: https://github.com/bambulab/BambuStudio/wiki/Command-Line-Usage  
- Printago Slicer CLI reference: https://printago.io/slicer-cli  
- Blender MCP: https://github.com/ahujasid/blender-mcp  
- CuraEngine API wrapper example: https://github.com/makermate/curaengine-slicer-api  
- FreeCAD headless: https://wiki.freecad.org/Headless_FreeCAD  
- OpenSCAD CLI: https://en.wikibooks.org/wiki/OpenSCAD_User_Manual/Using_OpenSCAD_in_a_command_line_environment  
- Model Context Protocol: https://modelcontextprotocol.io/

---

## 19. Changelog

| Date | Change |
|---|---|
| 2026-08-06 | Initial design: research survey, hybrid CLI, skill/MCP phases, JSON contracts, gates |
