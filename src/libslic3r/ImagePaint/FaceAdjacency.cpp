#include "FaceAdjacency.hpp"

#include <algorithm>
#include <unordered_map>
#include <cassert>

namespace Slic3r::ImagePaint {

namespace {

// Canonical directed edge key: sorted vertex pair packed into a uint64_t.
inline std::uint64_t edge_key(int32_t a, int32_t b) noexcept
{
    const uint32_t lo = static_cast<uint32_t>(std::min(a, b));
    const uint32_t hi = static_cast<uint32_t>(std::max(a, b));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

} // namespace

FaceAdjacency build_face_adjacency(Span<const Vec3i32> indices)
{
    const std::size_t n_faces = indices.size();

    // Map each undirected edge -> list of face indices that share it.
    std::unordered_map<std::uint64_t, std::vector<FaceIndex>> edge_to_faces;
    edge_to_faces.reserve(n_faces * 3);

    for (std::size_t fi = 0; fi < n_faces; ++fi) {
        const Vec3i32& tri = indices[fi];
        for (int e = 0; e < 3; ++e) {
            const int32_t va = tri[e];
            const int32_t vb = tri[(e + 1) % 3];
            edge_to_faces[edge_key(va, vb)].push_back(static_cast<FaceIndex>(fi));
        }
    }

    // Build adjacency lists: for each face, collect all neighbors via shared edges.
    std::vector<std::vector<FaceIndex>> adj(n_faces);

    for (const auto& [key, faces] : edge_to_faces) {
        if (faces.size() < 2) continue;  // boundary edge
        for (std::size_t i = 0; i < faces.size(); ++i)
            for (std::size_t j = 0; j < faces.size(); ++j)
                if (i != j)
                    adj[faces[i]].push_back(faces[j]);
    }

    // Deduplicate and sort each adjacency list (deterministic order).
    for (auto& nbrs : adj) {
        std::sort(nbrs.begin(), nbrs.end());
        nbrs.erase(std::unique(nbrs.begin(), nbrs.end()), nbrs.end());
    }

    // Convert to CSR format.
    FaceAdjacency result;
    result.offsets.resize(n_faces + 1, 0);
    for (std::size_t fi = 0; fi < n_faces; ++fi)
        result.offsets[fi + 1] = result.offsets[fi] +
                                   static_cast<std::uint32_t>(adj[fi].size());

    result.neighbors.resize(result.offsets.back());
    for (std::size_t fi = 0; fi < n_faces; ++fi)
        std::copy(adj[fi].begin(), adj[fi].end(),
                  result.neighbors.begin() + result.offsets[fi]);

    return result;
}

} // namespace Slic3r::ImagePaint
