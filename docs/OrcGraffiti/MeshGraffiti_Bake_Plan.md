# Mesh Graffiti — Baked-Mesh Architecture Plan

Status: **PLANNING ONLY — no implementation started.** Written in response to a
direct challenge to the earlier "no remeshing" MVP assumption, backed by three
research passes over the existing OrcaSlicer codebase (citations below).

## 0. Reassessment — was the earlier caution correct?

Partially, but overstated. The earlier conclusion ("remeshing is too risky for
this project") assumed we'd be building topology-mutation infrastructure from
scratch. That's wrong: **OrcaSlicer already ships three features that replace
a `ModelVolume`'s real mesh at runtime — Simplify (`GLGizmoSimplify`), Mesh
Boolean (`GLGizmoMeshBoolean`), and Cut (`CutUtils.cpp`/`GLGizmoCut`) — and
all three share one established, tested pattern** for doing it without
breaking paint, undo, or 3MF. We don't need to invent that pattern; we need to
reuse it. That materially changes the risk calculus. The remaining genuine
risk is narrower than "should we ever touch mesh geometry" — it's "can our
*specific* adaptive-subdivision algorithm reliably produce valid geometry,"
which is a scoped, testable engineering problem, not an architectural one.

Revised position: **yes, build a real bake stage**, using the existing
mesh-replacement pattern verbatim, with our own subdivision algorithm as the
only genuinely new piece.

---

## 1. Recommended architecture

Two-stage, exactly as proposed:

**Stage A — Preview (interactive, already built).** The current
`GLGizmoImagePainter` + `TriangleSelector::subdivide_facet_uniform`/
`collect_leaves`/`set_leaf_state` path (landed this session) stays as-is.
Position/rotate/size/detail/color sliders all operate against the *virtual*
split tree — zero mesh mutation, fully interactive, cheap to recompute on
every slider tick. This is not thrown away; it becomes the live-preview
layer and the source of truth for "what should the final result look like."

**Stage B — Bake (one-shot, transactional, new).** On a separate, explicit
user action ("Bake" or folded into "Apply" with a checkbox — UX TBD, not
blocking this plan), convert the *approved* projection into real geometry:

```
original mesh
  → generate candidate mesh (adaptive subdivision, image-boundary-aware)
  → cleanup (degenerate faces, near-duplicate vertices, normals)
  → validate (manifold check)
  → repair if needed (existing CGAL repair pipeline)
  → validate again
  → commit (via the existing set_mesh + save/restore_painting + snapshot pattern)
  → on any failure at any stage: rollback, keep original mesh, fall back to
    Stage A's virtual-subdivision result (which is always valid, since it
    never touched real geometry)
```

The rollback path is cheap and safe *by construction*: because Stage A never
touches the real mesh, "bake failed" always has a working fallback — the
virtual-subdivision paint result — not just "undo to blank." This is a
meaningful advantage over a single-stage bake-only design.

---

## 2. Exact files/classes involved (from research)

| Concern | File / Class / Function |
|---|---|
| Virtual subdivision (Stage A, existing) | `src/libslic3r/TriangleSelector.{hpp,cpp}` — `subdivide_facet_uniform`, `collect_leaves`, `set_leaf_state` (added this session) |
| Mesh-replacing precedent #1 | `src/slic3r/GUI/Gizmos/GLGizmoSimplify.cpp` — `apply_simplify()` |
| Mesh-replacing precedent #2 | `src/slic3r/GUI/Gizmos/GLGizmoMeshBoolean.cpp` — `generate_new_volume()` |
| Mesh-replacing precedent #3 | `src/libslic3r/CutUtils.cpp` — `Cut::perform_with_plane()` / `Cut::finalize()` |
| Mesh replace primitive | `src/libslic3r/Model.hpp:857-862` — `ModelVolume::set_mesh(TriangleMesh&&/const&, indexed_triangle_set&&/const&)` (bare setter, no side effects — caller must do the rest) |
| Paint save/restore | `src/libslic3r/Model.cpp:1982,1997` — `ModelVolume::save_painting()` / `restore_painting()` |
| Paint spatial remap | `src/libslic3r/TriangleSelector.cpp:2482` — `TriangleSelector::remap_painting()` (AABB-tree + normal-similarity + overlap test; wired in, not dead code) |
| Saved-painting bundle | `src/libslic3r/TriangleSelector.hpp:412` — `struct SavedPainting { TriangleMesh mesh; TriangleSplittingData supported, seam, mmu, fuzzy; }` — **captures all four paint layers together** |
| Undo/redo stack | `src/slic3r/Utils/UndoRedo.cpp` — `StackImpl::take_snapshot`, `save_mutable_object<Model>`, `save_immutable_object<TriangleMesh>` |
| Undo entry point | `src/slic3r/GUI/Plater.cpp:12626` — `Plater::take_snapshot()` |
| Reslice invalidation trigger | `src/slic3r/GUI/Plater.cpp:18238` — `Plater::changed_mesh(obj_idx)` → `schedule_background_process()` |
| Reslice diff logic | `src/libslic3r/PrintApply.cpp:1550-1577` — `model_volume_list_changed` (keys on `ModelVolume::id()`), `model_mmu_segmentation_data_changed` (keys on `FacetsAnnotation` timestamp) |
| Manifold check | `src/libslic3r/TriangleMesh.cpp:1525` — `its_num_open_edges()` |
| Degenerate-face removal | `src/libslic3r/TriangleMesh.hpp:212` — `its_remove_degenerate_faces()` |
| Self-intersection check | `src/libslic3r/MeshBoolean.cpp:459` — `MeshBoolean::cgal::does_self_intersect()` |
| Full manifold repair | `src/libslic3r/MeshBoolean.cpp` — `MeshBoolean::cgal::repair(TriangleMesh&, RepairedMeshErrors*, std::string*)` — **clean, GUI-free, already linked CGAL** |
| Import-time light repair (reference) | `src/libslic3r/TriangleMesh.cpp:79-179` — `trianglemesh_repair_on_import()` (admesh: `stl_check_facets_exact/nearby`, `stl_fix_normal_directions/values`) |
| 3MF write | `src/libslic3r/Format/bbs_3mf.cpp:7245` — `_BBS_3MF_Exporter::_add_mesh_to_object_stream` |
| 3MF read | `src/libslic3r/Format/bbs_3mf.cpp:5156-5182` |
| CGAL dependency | `src/libslic3r/CMakeLists.txt:532` — `find_package(CGAL REQUIRED)` — already required, not optional |

---

## 3. Data flow, image projection → baked mesh

```
ImagePaintRequest (existing)
  → run_image_paint() (existing, unchanged) → FacePaintPlan
      .states[]              (coarse, unchanged — fallback / non-detail callers)
      .detail_leaf_states[]  (existing, this session — virtual leaves + colors)
  → [NEW] bake_candidate_mesh(original_its, detail_leaf_states, adaptive params)
      → per touched face: run CDT-lite subdivision seeded by the SAME leaf
        color boundaries already computed in Stage A (no new image sampling —
        reuse the classification already done), snapping new vertices onto
        the original triangle's plane via barycentric coordinates
      → produces: new indexed_triangle_set + parallel array of per-NEW-triangle
        SelectorState (the color each new triangle should get)
  → [NEW] cleanup_mesh(candidate_its)         — its_remove_degenerate_faces, weld near-dup verts
  → [NEW] validate_mesh(candidate_its)        — its_num_open_edges == 0, no self-intersections
  → [NEW] repair_if_needed(candidate_its)     — MeshBoolean::cgal::repair() only if validate failed
  → [NEW] validate_mesh(candidate_its) again  — must pass or abort to rollback
  → [NEW] commit_bake(volume, candidate_its, per_triangle_states)
      = plater->take_snapshot("Image Paint (Bake)")
      + vol->save_painting()                  // captures supported/seam/mmu/fuzzy together
      + vol->set_mesh(std::move(candidate_its))
      + write per_triangle_states directly into a fresh TriangleSelector
        (set_facet per new triangle — cheap, deterministic, NOT remap_painting,
        since we KNOW the exact new-triangle→color mapping already; no need
        to spatially guess it)
      + vol->restore_painting(saved_painting, /*keep_existing_paint=*/true)
        // this DOES use remap_painting, but only for the OTHER paint layers
        // (seam/support/fuzzy) that we didn't compute ourselves — see §9
      + vol->set_new_unique_id()
      + vol->calculate_convex_hull(); vol->invalidate_convex_hull_2d();
      + vol->get_object()->invalidate_bounding_box(); ensure_on_bed();
      + plater->changed_mesh(object_idx)      // triggers reslice invalidation
```

Key point: for **our own new MMU paint**, we don't need `remap_painting`'s
spatial heuristic at all — we generated the new triangles ourselves and know
exactly which color each one gets. `remap_painting` is only needed for
*pre-existing* paint layers (seam/support/fuzzy-skin, or MMU paint outside
the image footprint) that we didn't touch — same as Simplify/Boolean/Cut
already do it.

---

## 4. Adaptive subdivision algorithm options

Three options, ordered by implementation cost:

**Option 1 — Reuse Stage A's leaf grid directly (cheapest, recommended for v1).**
`TriangleSelector::collect_leaves()` already gives us, per original face, a
set of small triangles each assigned one color. Bake v1 can simply emit
those leaf triangles as REAL geometry (their positions are already
barycentric-valid subpoints of the original face — see §6). This is *not*
adaptive in the CDT sense (uniform grid, same as the virtual preview), but
it requires zero new subdivision math — the "candidate mesh generation"
step in §3 becomes "materialize the leaves TriangleSelector already
computed," reusing 100% of this session's work. Ship this first; it already
beats the flat per-face ceiling.

**Option 2 — Adaptive uniform-grid pruning (moderate cost).** Post-process
Option 1's output: merge adjacent leaves that ended up the same color back
into larger triangles (a simple greedy quad-merge on the regular grid,
since `subdivide_facet_uniform`'s split pattern is a predictable binary
tree). Reduces triangle count where the image doesn't need detail, without
needing real CDT. This directly addresses "prefer adaptive, not uniform."

**Option 3 — True constrained Delaunay triangulation on image boundaries
(highest cost, best fidelity, matches MakerWorld most closely).** Extract
the image's color-region contours (already vectorizable — this is the
"vectorize the image" idea the user raised early in this project) within
each face's UV footprint, project them onto the face's plane in barycentric
coordinates, and run 2D CDT (`CGAL::Constrained_Delaunay_triangulation_2`,
available since CGAL is already linked) per face using the contour as
constraint edges, then lift the result back onto the 3D triangle. This is
the only option that produces triangle edges that actually *follow* color
boundaries rather than approximate them with a grid. Recommended as a v2
target once Option 1/2 are proven safe in production.

**Recommendation**: ship Option 1 first (near-zero new subdivision code,
reuses everything already built and tested), evaluate whether visual
quality is "good enough," and only invest in Option 3 if there's a
concrete complaint about jagged/pixelated edges at typical detail settings.

---

## 5. Attribute/color transfer to new triangles

Trivial for Option 1/2: `TriangleSelector::collect_leaves()` +
`FacePaintPlan::detail_leaf_states` already produce a 1:1 leaf→color
mapping. Materializing leaves as real triangles means the color assignment
is already known per new triangle — write it directly via `set_facet()` on
a fresh `TriangleSelector` built over the new mesh, no remapping needed
(see §3). For Option 3 (true CDT), each new triangle inherits the color of
whichever original image-region contour it falls inside — computed at
triangulation time, same idea, still a direct assignment, not a spatial
guess.

---

## 6. Geometry preservation (staying on the original surface)

Every new vertex introduced by subdivision must be expressible as
`P = a*V0 + b*V1 + c*V2` (barycentric, `a+b+c=1`) against the *original*
face's three vertices. This is automatically true for `subdivide_facet_uniform`
(it bisects edges — an edge midpoint is trivially barycentric) and remains
true for CDT-based Option 3 as long as new points are generated in 2D UV/
barycentric space and lifted back via the same formula, never computed in
world-space independently. This guarantees the baked mesh's *shape* is
unchanged — only its triangle density/pattern changes, directly satisfying
the "topology/resolution, not physical shape" requirement. Worth an explicit
unit test: assert every new vertex lies exactly on the original triangle's
plane (cross-product/plane-distance check) for both Option 1 and Option 3.

---

## 7. Undo/redo strategy

**Confirmed by research: plain snapshot-before/restore-after is sufficient,
no custom reversal logic needed.** `Plater::take_snapshot()` captures the
entire `Model` via cereal; `ModelVolume` is a "mutable object" with
`timestamp()==0` (no `ObjectWithTimestamp` base), so it is **always fully
re-serialized on every snapshot** — mesh pointer (as a new immutable-object
blob, since `set_mesh` always allocates a new `shared_ptr<const TriangleMesh>`)
plus all four `FacetsAnnotation` fields (`supported_facets`, `seam_facets`,
`mmu_segmentation_facets`, `fuzzy_skin_facets`), embedded directly in the
same blob. A single `take_snapshot("Image Paint (Bake)")` before the bake
commit is complete and correct; Ctrl+Z fully restores original geometry +
original paint with zero bake-specific undo code. This exactly matches
Simplify/Boolean/Cut's existing pattern — no new undo infrastructure at all.

---

## 8. Validation and repair pipeline

Layered, cheapest-first, matching what already exists:

1. **Cheap structural cleanup** (new, small, no CGAL): `its_remove_degenerate_faces()`
   (already exists) for zero-area triangles; a simple spatial-hash near-duplicate-vertex
   weld (pattern-matching admesh's `stl_check_facets_nearby`, but we may be able to
   call that directly on our candidate mesh via the existing `TriangleMesh::from_stl`-style
   path — needs a spike to confirm it's callable standalone, not just at import time).
2. **Manifold check**: `its_num_open_edges(candidate_its) == 0`. If true, skip repair
   entirely (expected common case for Option 1/2 — bisection subdivision of a manifold
   mesh with the same T-junction-avoidance discipline `TriangleSelector` already uses
   internally for its virtual tree should stay manifold by construction).
3. **Self-intersection check**: `MeshBoolean::cgal::does_self_intersect()` — should
   never trigger for barycentric-constrained subdivision (new geometry can't leave
   the original face's plane), but cheap to check and a good early-warning canary
   for algorithm bugs during development.
4. **Repair, only if step 2 or 3 fails**: `MeshBoolean::cgal::repair()` — already a
   clean `TriangleMesh → TriangleMesh` function, no GUI coupling, no new dependency.
5. **Re-validate** (repeat 2/3). Fail → rollback (§1).

**T-junctions are the one place with no reusable general-purpose fixer**
(`TriangleSelector::get_facets_split_by_tjoints` is internal to its own split
tree, not applicable to an arbitrary `indexed_triangle_set` — confirmed by
research). Mitigation: **design the subdivision algorithm to never create
T-junctions in the first place** (this is what `TriangleSelector`'s own
split machinery already guarantees for the virtual tree via
`child_neighbors`/neighbor propagation — Option 1 can literally copy that
discipline since it's reusing the same split pattern). This turns a
"genuinely difficult, needs new general-purpose code" problem into an
"avoided by algorithm design" problem. Option 3 (CDT) would need the same
discipline applied per-face at triangulation time (constrain shared edges
between adjacent faces to bisect identically) — flagged as a real risk for
v2, see §13.

---

## 9. Existing-paint handling — no v1 restriction needed

Research finding changes this from "acceptable v1 limitation" to
"already solved": `ModelVolume::SavedPainting` bundles **all four** paint
layers (`supported`, `seam`, `mmu`, `fuzzy`) together, and
`save_painting()`/`restore_painting()` already handle carrying all of them
across a topology change — this is exactly what Simplify/Boolean/Cut do
today when a user has, say, support paint AND is running Simplify. Bake
should call the same two functions. **No requirement to run Mesh Graffiti
before seam/support painting, no warn-and-cancel dialog needed for v1.**

The one caveat worth keeping as a safety net (not a hard blocker):
`remap_painting`'s spatial-overlap heuristic (AABB overlap + ~5° normal
tolerance) can silently under-map paint when topology changes
significantly in the touched region. Recommended mitigation: after
`restore_painting()`, compare total painted area (already-existing
`PaintDiagnostics`-style metric) before vs. after for the *other* paint
layers specifically, and surface a status-bar warning ("N% of existing
support paint could not be carried over") rather than silently dropping
it. This is a small addition, not a new subsystem.

---

## 10. Performance limits and failure safeguards

- Reuse the existing `kMaxDetailLeaves` budget from Stage A (250,000) as the
  upper bound on how many real triangles a single bake can produce — if
  Stage A's preview already respected this cap, materializing it 1:1
  (Option 1) inherits the same bound for free.
- Add a wall-clock timeout around the validate/repair loop (repair via CGAL
  boolean-union-based `repair()` is the potentially-slow step) — if repair
  exceeds e.g. 10s, treat as failure and roll back rather than risk the
  UI-thread hang class of bug this project already hit once (AS-3).
- Bake should run on the same worker-thread/Job infrastructure `ImagePaintJob`
  already uses, with only the final `set_mesh`/`restore_painting`/snapshot
  commit on the UI thread — consistent with the existing threading contract
  and with how Simplify computes its result off-thread before a synchronous
  commit.
- Do NOT reject the architecture over worst-case triangle counts (per the
  user's explicit instruction) — instead, make the cap configurable and
  make "budget exceeded" a graceful, visible fallback to Stage A's virtual
  result (which is always available) rather than a hard error.

---

## 11. Test strategy

- **Unit**: candidate-mesh generation — barycentric-plane assertion (§6),
  manifold check before/after for a range of detail settings, degenerate-face
  count is zero after cleanup.
- **Unit**: color transfer — every new triangle's assigned state matches the
  leaf it was derived from (extends this session's existing
  `test_triangle_selector.cpp` / `test_image_paint_pipeline.cpp`).
- **Integration**: full bake transaction on the existing unit-cube fixture
  and at least one denser real-world mesh (reuse `D:\3D models\m5tab5\...`
  reference files already used for MakerWorld comparison) — assert
  triangle count increases as expected, `its_num_open_edges == 0`, volume
  before/after within tolerance (confirms shape preserved).
- **Integration**: existing-paint preservation — pre-paint a face with
  seam/support data, bake Image Paint elsewhere on the same volume, assert
  the seam/support paint survives (extends the existing
  `save_painting`/`restore_painting` pattern's own test coverage, if any —
  worth checking whether Simplify/Boolean have existing tests to model this
  on).
- **Integration**: undo — snapshot, bake, Ctrl+Z equivalent (`Stack::undo`),
  assert original mesh triangle count and original paint state are restored
  exactly.
- **Save/reload**: bake, save to `.3mf`, reload, assert geometry and paint
  are byte-identical to the pre-save in-memory state (extends this fork's
  existing 3MF round-trip test patterns).
- **Real slicing (the test this project has historically under-invested in
  before AS-3 — do not repeat that mistake)**: bake a real model, run it
  through `Print::apply()`/actual slicing (not just the CLI dry-run), and —
  critically — **have a human open the resulting sliced file/3MF in real
  OrcaSlicer and FlashForge Studio**, matching the verification bar AS-3's
  post-mortem established. Self-checks (manifold check, volume check) are
  necessary but the AS-3 lesson stands: they are not sufficient proof of
  real-world safety for anything that touches saved geometry.

---

## 12. Incremental implementation stages

1. **Candidate mesh generation, Option 1 only** (materialize Stage A's
   existing leaves as real geometry) + barycentric-plane unit tests. No
   commit path yet — just prove the geometry step is correct in isolation.
2. **Validate/cleanup/repair pipeline**, wired to (1)'s output, with the
   layered approach from §8. Prove manifold-in → manifold-out on a battery
   of test meshes (cube, sphere, the m5tab5 reference, a deliberately
   near-degenerate mesh).
3. **Commit transaction** (§3's `commit_bake`), reusing `set_mesh`/
   `save_painting`/`restore_painting`/`take_snapshot`/`changed_mesh`
   verbatim from the Simplify pattern. Test undo/redo and reslice
   invalidation.
4. **3MF round-trip** verification (should require zero format changes per
   §3 findings — this stage is mostly test-writing, not new code).
5. **GUI wiring**: a "Bake" action in `GLGizmoImagePainter`, gated behind
   an explicit user choice (not automatic on every Apply) so Stage A's fast
   preview stays the default, cheap workflow.
6. **Option 2** (adaptive merge of same-color leaves) — quality/performance
   improvement, no architecture change.
7. **Option 3** (true CDT) — only after 1-6 are shipped and validated in
   real use; separate design pass, since it introduces the one genuinely
   new hard problem (T-junction discipline across adjacent faces, §8/§13).

Each stage should land as its own reviewable change with its own tests,
not as one large PR — consistent with how this session's virtual-subdivision
work was structured.

---

## 13. Major risks that remain after these mitigations

Ranked by how much genuinely new engineering they require:

- **T-junction-free triangulation across adjacent face boundaries for
  Option 3 (CDT).** Genuinely difficult — no existing general-purpose
  utility (§8). Option 1/2 sidestep this by copying `TriangleSelector`'s
  own already-correct split discipline; Option 3 does not get this for
  free and would need its own neighbor-consistency algorithm. This is the
  single hardest remaining problem and the reason Option 3 is sequenced
  last.
- **`remap_painting`'s spatial-overlap heuristic accuracy** for
  *pre-existing* paint layers under significant topology change (§9) — a
  real, if narrow, quality risk. Mitigated by a diagnostic warning, not
  eliminated.
- **CGAL repair performance/behavior on pathological candidate meshes** —
  the boolean-self-union step in `MeshBoolean::cgal::repair()` is
  heavyweight; needs real profiling against the largest meshes/detail
  settings we intend to support, not just small test fixtures.
- **Verification gap**: this project's own AS-3 history is direct evidence
  that self-checks (manifold, volume, structural) can pass while the file
  still fails in real slicer software. Baked-mesh 3MF output needs the
  same human-verification bar AS-3 eventually required, before this ships
  as a default-on feature.
- **UX/scope creep risk**: "Bake" as a second explicit step adds workflow
  complexity (when does a user bake vs. stay in preview?) — a design
  question, not an engineering one, but worth resolving before Stage 5.

None of these are "remeshing is fundamentally unsafe for this project" —
they're scoped, testable engineering problems with clear owners in the
staging plan above.

---

## 14. Recommendation: is TriangleSelector-only the final architecture?

**No — it should become the preview layer of a real baked-mesh
implementation, not stay the final architecture.** The virtual-subdivision
work from this session is not wasted: it's Stage A, it's already correct
and tested, and Option 1 of the bake stage literally reuses its output
verbatim. The two-stage design lets us ship the low-risk, already-built
half immediately (done) while building the bake stage incrementally behind
its own explicit user action, with a safe rollback to the virtual result at
every step. This matches the user's framing exactly: TriangleSelector
subdivision was never the wrong tool, it was an incomplete architecture —
the missing piece was the bake transaction, and that piece turns out to be
much more buildable than earlier assumed, because OrcaSlicer already has
the load-bearing infrastructure for it.
