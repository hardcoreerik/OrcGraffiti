#pragma once

#include "ImagePaintTypes.hpp"
#include "FaceAdjacency.hpp"

#include <vector>
#include "ImagePaintCompat.hpp"
#include <functional>

namespace Slic3r::ImagePaint {

struct RegionCleanupSettings {
    bool          enabled                    = true;
    std::uint32_t min_faces                  = 2;
    double        min_surface_area_mm2       = 0.0;
    double        min_projected_area_mm2     = 0.0;
    double        max_cross_edge_angle_degrees = 45.0;
    std::uint32_t max_iterations             = 3;
};

// Clean up tiny single-color connected components in proposed_states.
//
// Algorithm (per the architecture doc, section 15):
//   1. Identify same-state connected components via BFS over FaceAdjacency.
//   2. For each component below the size threshold, find all neighboring states.
//   3. Skip candidates across a sharp edge (dihedral > max_cross_edge_angle).
//   4. Merge into the neighbor with the longest shared boundary.
//   5. Tie-break by lowest filament index.
//   6. Repeat up to max_iterations times.
//
// proposed_states is modified in place.
// face_normals: one unit normal per face (used for sharp-edge test).
// face_areas: surface area in mm² per face.
// Returns the number of tiny components merged.
std::uint32_t
clean_tiny_regions(
    std::vector<SelectorState>&         proposed_states,
    const FaceAdjacency&                adjacency,
    Span<const Vec3d>              face_normals,
    Span<const double>             face_areas,
    const RegionCleanupSettings&        settings,
    const std::function<bool()>&        cancel = {});

} // namespace Slic3r::ImagePaint
