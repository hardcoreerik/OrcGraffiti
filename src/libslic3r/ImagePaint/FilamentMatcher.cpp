#include "FilamentMatcher.hpp"
#include "ColorSpace.hpp"
#include "ColorDifference.hpp"

#include <algorithm>
#include <limits>

namespace Slic3r::ImagePaint {

// Selector state encoding mirrors EnforcerBlockerType:
//   0 = NONE (unpainted)
//   1 = Extruder1 (filament index 0)
//   ...
//   16 = Extruder16 (filament index 15)
//
// This is the ONLY place in the codebase that performs this conversion.
// All other code uses SelectorState (uint8_t) or FilamentIndex (uint16_t).

std::expected<SelectorState, ImagePaintError>
filament_index_to_selector_state(FilamentIndex index)
{
    // FilamentIndex is 0-based; SelectorState 1..16 correspond to Extruder1..16.
    const SelectorState state = static_cast<SelectorState>(index + 1u);
    if (state < kStateExtruderMin || state > kStateExtruderMax)
        return std::unexpected(ImagePaintError{
            ImagePaintErrorCode::FilamentOutOfRange,
            "Filament index out of valid range.",
            "index=" + std::to_string(index)});
    return state;
}

std::expected<FilamentIndex, ImagePaintError>
selector_state_to_filament_index(SelectorState state)
{
    if (state < kStateExtruderMin || state > kStateExtruderMax)
        return std::unexpected(ImagePaintError{
            ImagePaintErrorCode::FilamentOutOfRange,
            "Selector state does not correspond to a filament.",
            "state=" + std::to_string(state)});
    return static_cast<FilamentIndex>(state - 1u);
}

std::expected<std::vector<ClusterMatch>, ImagePaintError>
match_clusters_to_filaments(
    const std::vector<SourceCluster>&  clusters,
    const std::vector<FilamentColor>&  filaments,
    bool                               one_to_one)
{
    if (filaments.empty())
        return std::unexpected(ImagePaintError{
            ImagePaintErrorCode::NoAvailableFilaments,
            "No filaments configured in the project."});

    if (clusters.empty())
        return std::vector<ClusterMatch>{};

    // Pre-compute Lab for each filament.
    std::vector<ColorLab> filament_lab;
    filament_lab.reserve(filaments.size());
    for (const auto& f : filaments)
        filament_lab.push_back(rgb8_to_lab(f.display_rgb));

    std::vector<bool> used(filaments.size(), false);
    std::vector<ClusterMatch> result;
    result.reserve(clusters.size());

    // Greedy assignment: clusters are already sorted by luminance (darkest first).
    // Each cluster picks the closest unassigned filament.
    for (const auto& cluster : clusters) {
        const ColorLab cluster_lab = rgb8_to_lab(cluster.representative);

        double best_de = std::numeric_limits<double>::max();
        std::size_t best_fi = 0;

        for (std::size_t fi = 0; fi < filaments.size(); ++fi) {
            if (one_to_one && used[fi]) continue;
            const double de = delta_e_2000(cluster_lab, filament_lab[fi]);
            if (de < best_de) {
                best_de = de;
                best_fi = fi;
            }
        }

        // Validate the chosen index.
        auto state_result = filament_index_to_selector_state(
            filaments[best_fi].project_index);
        if (!state_result)
            return std::unexpected(state_result.error());

        if (one_to_one) used[best_fi] = true;

        ClusterMatch cm;
        cm.cluster_id     = cluster.id;
        cm.filament_index = filaments[best_fi].project_index;
        cm.delta_e        = best_de;
        cm.user_overridden = false;
        result.push_back(cm);
    }

    return result;
}

} // namespace Slic3r::ImagePaint
