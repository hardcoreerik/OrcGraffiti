#include "MeshBake.hpp"

#include "libslic3r/TriangleSelector.hpp"

#include <algorithm>

namespace Slic3r::ImagePaint {

Expected<BakedMesh, ImagePaintError>
bake_candidate_mesh(const std::vector<Vec3f>&   vertices,
                    const std::vector<Vec3i32>& indices,
                    const std::vector<SelectorState>& flat_states,
                    const std::vector<std::pair<FaceIndex, std::vector<SelectorState>>>& detail_leaf_states,
                    double detail_edge_length_mm)
{
    if (vertices.empty() || indices.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::NoEligibleFaces, "Mesh has no faces to bake."});

    TriangleMesh snapshot_mesh(vertices, indices);
    TriangleSelector selector(snapshot_mesh);

    // Untouched faces keep their flat (possibly kStateNone) state directly —
    // this is what makes them show up under state=NONE's get_facets_strict()
    // call below, unsplit, exactly as in the original mesh.
    for (std::size_t i = 0; i < flat_states.size() && i < indices.size(); ++i)
        if (flat_states[i] != kStateNone)
            selector.set_facet(static_cast<int>(i), static_cast<EnforcerBlockerType>(flat_states[i]));

    // Touched faces get replaced by their fine-detail leaves — replaying the
    // exact same deterministic split ImagePaintJob::finalize() already uses
    // to write mmu_segmentation_facets (see Apply-time replay comment there).
    const auto edge_limit = static_cast<float>(detail_edge_length_mm);
    for (const auto& [face_idx, leaf_states] : detail_leaf_states) {
        if (face_idx >= indices.size())
            continue;
        selector.set_facet(static_cast<int>(face_idx), EnforcerBlockerType::NONE);
        selector.subdivide_facet_uniform(static_cast<int>(face_idx), edge_limit);
        const auto leaves = selector.collect_leaves(static_cast<int>(face_idx));
        const std::size_t n = std::min(leaves.size(), leaf_states.size());
        for (std::size_t k = 0; k < n; ++k)
            selector.set_leaf_state(leaves[k].leaf_index, static_cast<EnforcerBlockerType>(leaf_states[k]));
    }

    // get_facets_strict() rebuilds its vertex list from TriangleSelector's
    // own m_vertices/ref-count bookkeeping, which we don't mutate between
    // calls below — so every call returns byte-identical vertices in the
    // same order. Take them once from the first non-empty state and only
    // append indices from the rest, instead of welding duplicate vertex
    // copies across states.
    BakedMesh result;
    bool vertices_taken = false;
    for (int s = 0; s <= static_cast<int>(EnforcerBlockerType::ExtruderMax); ++s) {
        const auto state = static_cast<EnforcerBlockerType>(s);
        if (!selector.has_facets(state))
            continue;

        auto part = selector.get_facets_strict(state);
        if (!vertices_taken) {
            result.mesh.vertices = std::move(part.vertices);
            vertices_taken = true;
        }

        result.mesh.indices.reserve(result.mesh.indices.size() + part.indices.size());
        for (const auto& tri : part.indices) {
            result.mesh.indices.push_back(tri);
            result.triangle_states.push_back(static_cast<SelectorState>(s));
        }
    }

    if (result.mesh.indices.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::NoEligibleFaces, "Bake produced no geometry."});

    return result;
}

} // namespace Slic3r::ImagePaint
