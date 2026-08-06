#pragma once

#include "ImagePaintTypes.hpp"
#include "ImagePaintErrors.hpp"

#include "ImagePaintCompat.hpp"
#include <vector>

namespace Slic3r::ImagePaint {

struct QuantizationSettings;

// Match source color clusters to available project filaments using CIEDE2000.
//
// When one_to_one is true (default), each filament is used at most once
// (Hungarian-style greedy assignment by best ΔE, in cluster luminance order).
// When false, each cluster independently picks the closest filament.
//
// All filament indices returned are 0-based (internal representation).
// The caller is responsible for the 0->1-based conversion at the UI layer.
Expected<std::vector<ClusterMatch>, ImagePaintError>
match_clusters_to_filaments(
    const std::vector<SourceCluster>&  clusters,
    const std::vector<FilamentColor>&  filaments,
    bool                               one_to_one = true);

// Central utility: validate and convert a 0-based FilamentIndex to a
// SelectorState (which maps to EnforcerBlockerType).
// Returns an error if the index is out of range.
Expected<SelectorState, ImagePaintError>
filament_index_to_selector_state(FilamentIndex index);

// Inverse: SelectorState -> 0-based FilamentIndex.
// State 0 (NONE/unpainted) returns an error — it has no filament.
Expected<FilamentIndex, ImagePaintError>
selector_state_to_filament_index(SelectorState state);

} // namespace Slic3r::ImagePaint
