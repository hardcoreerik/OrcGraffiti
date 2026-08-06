#pragma once

#include "ImagePaintTypes.hpp"
#include "libslic3r/Point.hpp"

#include <vector>
#include "ImagePaintCompat.hpp"

namespace Slic3r::ImagePaint {

// Compact CSR (compressed sparse row) adjacency graph over mesh triangles.
// Two faces are adjacent if they share exactly one edge (a sorted vertex pair).
// Non-manifold edges (shared by >2 faces) are included for all pairs.
// Boundary edges (shared by 1 face) have no adjacency entry.
struct FaceAdjacency {
    // offsets[i]..offsets[i+1] is the range of neighbors for face i.
    std::vector<std::uint32_t> offsets;
    // Flat list of neighbor face indices.
    std::vector<FaceIndex>     neighbors;

    std::size_t face_count() const noexcept
    { return offsets.empty() ? 0 : offsets.size() - 1; }

    // Range of neighbor indices for face i.
    Span<const FaceIndex> neighbors_of(FaceIndex i) const noexcept
    {
        if (i + 1 >= offsets.size()) return Span<const FaceIndex>();
        return Span<const FaceIndex>(
            neighbors.data() + offsets[i],
            offsets[i + 1] - offsets[i]);
    }
};

// Build adjacency from a triangle index list.
// indices: flat (i0, i1, i2) triples; length = triangle_count * 3.
FaceAdjacency build_face_adjacency(Span<const Vec3i32> indices);

} // namespace Slic3r::ImagePaint
