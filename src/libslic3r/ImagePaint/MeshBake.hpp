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

} // namespace Slic3r::ImagePaint
