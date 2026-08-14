#pragma once

#include "ImagePaintTypes.hpp"
#include "ImagePaintErrors.hpp"
#include "ImagePaintCompat.hpp"

#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <utility>
#include <vector>

namespace Slic3r::ImagePaint {

// Bake stage, Option 1 (see docs/OrcGraffiti/MeshGraffiti_Bake_Plan.md
// section 4) — materializes the fine-detail leaves already computed by
// run_image_paint() (FacePaintPlan::detail_leaf_states) as REAL mesh
// geometry, instead of TriangleSelector's virtual split tree.
//
// This does not sample the source image or run any new subdivision math —
// it replays the exact same deterministic TriangleSelector split
// (subdivide_facet_uniform + collect_leaves) the pipeline and Apply path
// already use, then exports it via TriangleSelector::get_facets_strict(),
// which already produces a T-junction-free triangulation across face
// boundaries (the same machinery every other paint gizmo relies on to
// export split geometry). Every new vertex is therefore a point on the
// original mesh's surface — no barycentric math needs to be hand-rolled
// here, subdivide_facet_uniform's edge bisection already guarantees it.
struct BakedMesh {
    indexed_triangle_set mesh;
    // Parallel to mesh.indices — the paint state each new triangle should get.
    std::vector<SelectorState> triangle_states;
};

// vertices/indices: the mesh snapshot the plan was computed against.
// flat_states: FacePaintPlan::states — one value per original face; used
//   as-is for every face NOT present in detail_leaf_states.
// detail_leaf_states: FacePaintPlan::detail_leaf_states.
// detail_edge_length_mm: the same value the plan was computed with
//   (ImagePaintRequest::detail_edge_length_mm) — must match exactly, since
//   subdivide_facet_uniform's split tree shape depends on it.
Expected<BakedMesh, ImagePaintError>
bake_candidate_mesh(const std::vector<Vec3f>&   vertices,
                    const std::vector<Vec3i32>& indices,
                    const std::vector<SelectorState>& flat_states,
                    const std::vector<std::pair<FaceIndex, std::vector<SelectorState>>>& detail_leaf_states,
                    double detail_edge_length_mm);

// Bake stage, Option 1: validates and lightly cleans up a candidate mesh
// before any commit is attempted (see docs/OrcGraffiti/MeshGraffiti_Bake_Plan.md
// section 8, steps 1-3). Removes zero-area triangles in lockstep with
// triangle_states (so geometry and color never desync), then checks
// manifoldness (its_num_open_edges == 0) and self-intersection
// (MeshBoolean::cgal::does_self_intersect). Returns BakeInvalidGeometry if
// either check still fails after cleanup.
//
// Deliberately NOT attempted here: automatic CGAL repair
// (MeshBoolean::cgal::repair(), plan section 8 step 4). That function's
// boolean-self-union pipeline does not preserve a stable per-triangle
// correspondence with its input, so there is no safe way yet to carry
// triangle_states through it without risking triangles ending up with the
// WRONG color — a silently-wrong result is worse than a loud failure here.
// bake_candidate_mesh() extracts geometry through
// TriangleSelector::get_facets_strict(), already relied on elsewhere in
// the app to produce manifold, T-junction-free output from a manifold
// input mesh, so this path is expected to be rare; it's a safety net, not
// a repair strategy. Revisit (with a color-preserving remap design) only
// if real testing shows it's actually hit.
Expected<BakedMesh, ImagePaintError> validate_baked_mesh(BakedMesh baked);

} // namespace Slic3r::ImagePaint
