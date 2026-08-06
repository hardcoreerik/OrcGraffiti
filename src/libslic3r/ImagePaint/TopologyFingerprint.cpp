#include "TopologyFingerprint.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cstring>
#include <cassert>

namespace Slic3r::ImagePaint {

// FNV-1a 64-bit — deterministic, no external dependency.
// Reference: http://www.isthe.com/chongo/tech/comp/fnv/
std::uint64_t fnv1a_64(const void* data, std::size_t size) noexcept
{
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime  = 1099511628211ULL;

    std::uint64_t h = kOffset;
    const auto*   p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        h ^= std::uint64_t(p[i]);
        h *= kPrime;
    }
    return h;
}

TopologyFingerprint fingerprint_from_arrays(
    std::span<const float>   vertices,
    std::span<const int32_t> indices)
{
    // vertex_count and triangle_count derived from array sizes.
    assert(vertices.size() % 3 == 0);
    assert(indices.size()  % 3 == 0);

    TopologyFingerprint fp;
    fp.vertex_count   = static_cast<std::uint32_t>(vertices.size() / 3);
    fp.triangle_count = static_cast<std::uint32_t>(indices.size()  / 3);

    fp.connectivity_hash = fnv1a_64(
        indices.data(),
        indices.size_bytes());

    fp.geometry_hash = fnv1a_64(
        vertices.data(),
        vertices.size_bytes());

    return fp;
}

TopologyFingerprint fingerprint(const TriangleMesh& mesh)
{
    const indexed_triangle_set& its = mesh.its;

    // Vertices: flat float data
    const float*   verts  = its.vertices.empty()
                             ? nullptr
                             : its.vertices[0].data();
    const std::size_t vsize = its.vertices.size() * 3;

    // Indices: flat int32_t data
    const int32_t* idxs  = its.indices.empty()
                            ? nullptr
                            : its.indices[0].data();
    const std::size_t isize = its.indices.size() * 3;

    return fingerprint_from_arrays(
        std::span<const float>  (verts, vsize),
        std::span<const int32_t>(idxs,  isize));
}

} // namespace Slic3r::ImagePaint
