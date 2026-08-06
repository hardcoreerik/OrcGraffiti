#pragma once

#include <cstdint>
#include <cstddef>
#include "ImagePaintCompat.hpp"
#include <vector>

namespace Slic3r {
    class TriangleMesh;
}

namespace Slic3r::ImagePaint {

// Deterministic mesh identity used to detect stale FacePaintPlan at Apply time.
// Does not use std::hash (not stable across runs/platforms).
// Uses FNV-1a over raw bytes — cheap, deterministic, in-project.
//
// connectivity_hash: depends only on triangle index data (which triangles share edges).
// geometry_hash: depends only on vertex position data.
// Both together identify a mesh up to floating-point bit-exact representation.
struct TopologyFingerprint {
    std::uint64_t connectivity_hash = 0;
    std::uint64_t geometry_hash     = 0;
    std::uint32_t vertex_count      = 0;
    std::uint32_t triangle_count    = 0;

    bool operator==(const TopologyFingerprint&) const = default;
    bool operator!=(const TopologyFingerprint&) const = default;
};

// Compute a fingerprint directly from raw arrays (worker-thread safe).
// vertices: flat array of (x, y, z) floats, length = vertex_count * 3
// indices:  flat array of (i0, i1, i2) int32, length = triangle_count * 3
TopologyFingerprint fingerprint_from_arrays(
    Span<const float>   vertices,
    Span<const int32_t> indices);

// Convenience overload for TriangleMesh (reads shared snapshot — thread-safe read).
TopologyFingerprint fingerprint(const TriangleMesh& mesh);

// FNV-1a 64-bit over arbitrary bytes — used internally and exposed for tests.
std::uint64_t fnv1a_64(const void* data, std::size_t size) noexcept;

} // namespace Slic3r::ImagePaint
