#include "RegionCleaner.hpp"

#include <queue>
#include <unordered_map>
#include <cmath>
#include <cassert>
#include <algorithm>

namespace Slic3r::ImagePaint {

namespace {

// BFS flood fill to find connected components of same selector state.
// Returns component_id per face (-1 = not yet assigned).
std::vector<int>
find_components(const std::vector<SelectorState>& states,
                const FaceAdjacency&              adj,
                std::vector<std::vector<FaceIndex>>& out_components)
{
    const std::size_t n = states.size();
    std::vector<int> comp_id(n, -1);
    out_components.clear();
    int next_id = 0;

    for (FaceIndex seed = 0; seed < n; ++seed) {
        if (comp_id[seed] >= 0) continue;
        const SelectorState target = states[seed];

        std::vector<FaceIndex> component;
        std::queue<FaceIndex> q;
        q.push(seed);
        comp_id[seed] = next_id;

        while (!q.empty()) {
            const FaceIndex fi = q.front(); q.pop();
            component.push_back(fi);

            for (FaceIndex nb : adj.neighbors_of(fi)) {
                if (comp_id[nb] < 0 && states[nb] == target) {
                    comp_id[nb] = next_id;
                    q.push(nb);
                }
            }
        }

        out_components.push_back(std::move(component));
        ++next_id;
    }

    return comp_id;
}

bool is_sharp_edge(FaceIndex fi, FaceIndex nb,
                   Span<const Vec3d> face_normals,
                   double cos_threshold)
{
    if (fi >= face_normals.size() || nb >= face_normals.size())
        return false;
    const double dot = face_normals[fi].dot(face_normals[nb]);
    return dot < cos_threshold;
}

} // namespace

std::uint32_t
clean_tiny_regions(
    std::vector<SelectorState>&         states,
    const FaceAdjacency&                adjacency,
    Span<const Vec3d>              face_normals,
    Span<const double>             face_areas,
    const RegionCleanupSettings&        settings,
    const std::function<bool()>&        cancel)
{
    if (!settings.enabled || states.empty())
        return 0;

    const double cos_threshold = std::cos(
        settings.max_cross_edge_angle_degrees * 3.14159265358979323846 / 180.0);

    std::uint32_t total_merged = 0;

    for (std::uint32_t iter = 0; iter < settings.max_iterations; ++iter) {
        if (cancel && cancel()) break;

        std::vector<std::vector<FaceIndex>> components;
        const auto comp_id = find_components(states, adjacency, components);

        bool any_merged = false;

        for (const auto& comp : components) {
            if (cancel && cancel()) break;

            // Compute component size metrics.
            const std::uint32_t n_faces = static_cast<std::uint32_t>(comp.size());
            double area = 0.0;
            for (FaceIndex fi : comp)
                if (fi < face_areas.size()) area += face_areas[fi];

            const bool too_small =
                n_faces < settings.min_faces ||
                (settings.min_surface_area_mm2 > 0.0 && area < settings.min_surface_area_mm2);

            if (!too_small) continue;

            // Find candidate neighbor states (across non-sharp edges).
            // Count shared-boundary length (number of shared edges per state).
            std::unordered_map<SelectorState, std::uint32_t> boundary_count;
            const SelectorState my_state = states[comp[0]];

            for (FaceIndex fi : comp) {
                for (FaceIndex nb : adjacency.neighbors_of(fi)) {
                    if (states[nb] == my_state) continue;
                    if (is_sharp_edge(fi, nb, face_normals, cos_threshold)) continue;
                    boundary_count[states[nb]]++;
                }
            }

            if (boundary_count.empty()) continue;

            // Select candidate: longest shared boundary, tie-break by lowest state.
            SelectorState best_state = my_state;
            std::uint32_t best_count = 0;
            for (const auto& [s, count] : boundary_count) {
                if (count > best_count ||
                    (count == best_count && s < best_state)) {
                    best_count = count;
                    best_state = s;
                }
            }

            if (best_state == my_state) continue;

            // Merge: reassign all faces in this component to best_state.
            for (FaceIndex fi : comp)
                states[fi] = best_state;

            ++total_merged;
            any_merged = true;
        }

        if (!any_merged) break;
    }

    return total_merged;
}

} // namespace Slic3r::ImagePaint
