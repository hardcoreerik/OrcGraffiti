# 2026-08-11 — Image Paint gizmo rework + MakerWorld technique finding

## What changed

Reworked `GLGizmoImagePainter` to match MakerWorld's Mesh Graffiti
interaction model, per the user's explicit request and a screen-capture
video of the real workflow: the image stays fixed at screen-center; the
user orbits/pans/zooms the *model* into position behind it, tunes Size%
and Rotation, then Applies.

- Added `ViewPreset` enum + `view_preset_vectors()` to
  `libslic3r/ImagePaint/Projection.hpp/.cpp` as the single shared table
  for both the `orcgraffiti --view` CLI flag and the new gizmo buttons.
- Gizmo panel: Front/Back/Left/Right/Top/Bottom buttons, Size% slider
  (scales the centered auto-fit plane), Rotate slider (wires the
  previously-dead `PlanarProjectionSettings::rotation_radians`), Apply.
  Legacy camera-facing auto-fit path kept as a collapsed "Advanced"
  section.
- Deleted the `MeshRaycaster`-based click-to-stamp mechanic entirely —
  built earlier this session from a misreading of MakerWorld's
  screenshots, corrected once the user's video showed the actual
  (non-raycast) mechanic.
- Verified: GUI DLL + CLI compile clean, 30/30 ctest, and the user's own
  screen recording of testing the rebuilt gizmo confirms the UI behaves
  correctly (preset highlighting, slider response, Apply produces a
  visible color change).

## The real finding: why the paint result doesn't look like the image

On the user's test (12-triangle cube + `garth.jpg`), Apply produced one
flat color block, not recognizable image content. Rather than guess, I
inspected MakerWorld's actual Mesh Graffiti web client — with the user's
own already-authenticated browser session; I did not handle or enter any
credentials — by reading the network request log and fetching the
same-origin JS chunks it loads.

Findings, directly from the fetched bundle:
- A module fetched as `cdt_wasm_bg.wasm`, with a console log tag
  `[CDT WASM] Remote load failed:` in the surrounding code — CDT =
  Constrained Delaunay Triangulation.
- The bundle containing that reference has 46 occurrences of
  `triangulat` and 36 of `vectoriz`.

Reading: MakerWorld vectorizes the uploaded image into contours, then
runs constrained Delaunay triangulation to insert new mesh edges along
those contours — actively retriangulating (remeshing) the mesh surface in
the painted region so each color region gets triangles shaped to match
the image, independent of the source mesh's original triangle density.

That is remeshing, full stop, and OrcGraffiti's Project_Truth.md /
AGENTS.md invariant is explicit: "Do not remesh in MVP — no subdivision,
repair, or edge collapse." So the flat-block result on a 12-triangle cube
is the *correct* output of a no-remesh per-face painter, not a pipeline
bug. Chasing image vectorization alone (the user's original hypothesis)
would not fix this on its own — the missing piece is the retriangulation
step that gives the vectorized contours somewhere to land.

## Decision

Proceeding under existing MVP scope: documented this as a known,
by-design limitation (quality bounded by triangle density in the painted
region) rather than attempting a remesh workaround that would violate a
hard project invariant. A CDT-based "remesh for paint" mode is a
plausible **post-MVP phase** — real scope (topology/undo/3MF
implications) — not something to bolt on mid-loop without the user
explicitly greenlighting a new phase.

## Test/build status

- ImagePaint unit tests: 93/93 (unchanged this entry — no pipeline logic
  touched, only GUI + shared view-preset table)
- ctest: 30/30
- GUI DLL: rebuilt, launched, and live-tested by the user via screen
  recording
