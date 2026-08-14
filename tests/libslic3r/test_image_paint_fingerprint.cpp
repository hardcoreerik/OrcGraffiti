#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/ImagePaint/TopologyFingerprint.hpp"
#include "libslic3r/ImagePaint/ImagePaintCompat.hpp"

using namespace Slic3r::ImagePaint;

// ---------------------------------------------------------------------------
// Helpers: build minimal indexed_triangle_set data for testing.
// We test fingerprint_from_arrays() directly to avoid TriangleMesh overhead.
// ---------------------------------------------------------------------------

static std::vector<float> triangle_vertices()
{
    // One triangle: (0,0,0) (1,0,0) (0,1,0)
    return { 0.f, 0.f, 0.f,
             1.f, 0.f, 0.f,
             0.f, 1.f, 0.f };
}

static std::vector<int32_t> triangle_indices()
{
    return { 0, 1, 2 };
}

static std::vector<float> cube_vertices()
{
    return {
        0.f,0.f,0.f,  1.f,0.f,0.f,  1.f,1.f,0.f,  0.f,1.f,0.f,
        0.f,0.f,1.f,  1.f,0.f,1.f,  1.f,1.f,1.f,  0.f,1.f,1.f
    };
}

static std::vector<int32_t> cube_indices()
{
    return {
        0,1,2, 0,2,3,   // -Z
        4,6,5, 4,7,6,   // +Z
        0,4,5, 0,5,1,   // -Y
        3,2,6, 3,6,7,   // +Y
        0,3,7, 0,7,4,   // -X
        1,5,6, 1,6,2    // +X
    };
}

// ---------------------------------------------------------------------------

TEST_CASE("Identical mesh produces equal fingerprints", "[ImagePaint][Fingerprint]")
{
    auto verts = triangle_vertices();
    auto idxs  = triangle_indices();

    auto fp1 = fingerprint_from_arrays(verts, idxs);
    auto fp2 = fingerprint_from_arrays(verts, idxs);

    CHECK(fp1 == fp2);
    CHECK(fp1.vertex_count   == 3);  // triangle_vertices() returns 3 vertices
    CHECK(fp1.triangle_count == 1);
}

TEST_CASE("Vertex position change alters geometry hash", "[ImagePaint][Fingerprint]")
{
    auto verts_a = triangle_vertices();
    auto verts_b = triangle_vertices();
    verts_b[3] = 2.f;  // move vertex 1 x from 1.0 to 2.0

    auto idxs = triangle_indices();

    auto fp_a = fingerprint_from_arrays(verts_a, idxs);
    auto fp_b = fingerprint_from_arrays(verts_b, idxs);

    CHECK(fp_a.connectivity_hash == fp_b.connectivity_hash);
    CHECK(fp_a.geometry_hash     != fp_b.geometry_hash);
    CHECK(fp_a != fp_b);
}

TEST_CASE("Triangle index change alters connectivity hash", "[ImagePaint][Fingerprint]")
{
    auto verts  = cube_vertices();
    auto idxs_a = cube_indices();
    auto idxs_b = cube_indices();

    // Swap the first and second triangle indices within the first face
    // (different connectivity — vertex 1 now connects differently)
    std::swap(idxs_b[0], idxs_b[1]);

    auto fp_a = fingerprint_from_arrays(verts, idxs_a);
    auto fp_b = fingerprint_from_arrays(verts, idxs_b);

    CHECK(fp_a.geometry_hash     == fp_b.geometry_hash);
    CHECK(fp_a.connectivity_hash != fp_b.connectivity_hash);
}

TEST_CASE("Triangle reorder changes connectivity hash", "[ImagePaint][Fingerprint]")
{
    auto verts  = cube_vertices();
    auto idxs_a = cube_indices();
    auto idxs_b = cube_indices();

    // Move first triangle to the end — different byte order for connectivity
    int32_t t0 = idxs_b[0], t1 = idxs_b[1], t2 = idxs_b[2];
    for (int i = 0; i < (int)idxs_b.size() - 3; ++i)
        idxs_b[i] = idxs_b[i + 3];
    idxs_b[idxs_b.size()-3] = t0;
    idxs_b[idxs_b.size()-2] = t1;
    idxs_b[idxs_b.size()-1] = t2;

    auto fp_a = fingerprint_from_arrays(verts, idxs_a);
    auto fp_b = fingerprint_from_arrays(verts, idxs_b);

    CHECK(fp_a.connectivity_hash != fp_b.connectivity_hash);
}

TEST_CASE("Empty mesh produces zero fingerprint with zero counts", "[ImagePaint][Fingerprint]")
{
    auto fp = fingerprint_from_arrays(
        Span<const float>{},
        Span<const int32_t>{});

    CHECK(fp.vertex_count   == 0);
    CHECK(fp.triangle_count == 0);
    // Hashes of empty spans should be the FNV offset basis (deterministic)
    CHECK(fp.connectivity_hash == fp.geometry_hash);
}

TEST_CASE("Vertex count and triangle count are correct", "[ImagePaint][Fingerprint]")
{
    auto verts = cube_vertices();
    auto idxs  = cube_indices();

    auto fp = fingerprint_from_arrays(verts, idxs);

    CHECK(fp.vertex_count   == 8u);
    CHECK(fp.triangle_count == 12u);
}

TEST_CASE("Different mesh topology produces different fingerprint", "[ImagePaint][Fingerprint]")
{
    auto verts_a = triangle_vertices();
    auto idxs_a  = triangle_indices();

    auto verts_b = cube_vertices();
    auto idxs_b  = cube_indices();

    auto fp_a = fingerprint_from_arrays(verts_a, idxs_a);
    auto fp_b = fingerprint_from_arrays(verts_b, idxs_b);

    CHECK(fp_a != fp_b);
    CHECK(fp_a.connectivity_hash != fp_b.connectivity_hash);
    CHECK(fp_a.geometry_hash     != fp_b.geometry_hash);
    CHECK(fp_a.vertex_count      != fp_b.vertex_count);
    CHECK(fp_a.triangle_count    != fp_b.triangle_count);
}

TEST_CASE("fnv1a_64 is deterministic across calls", "[ImagePaint][Fingerprint]")
{
    const char data[] = "OrcGraffiti deterministic hash test";
    auto h1 = fnv1a_64(data, sizeof(data));
    auto h2 = fnv1a_64(data, sizeof(data));
    CHECK(h1 == h2);
    CHECK(h1 != 0u);
}

TEST_CASE("fnv1a_64 produces different hashes for different inputs", "[ImagePaint][Fingerprint]")
{
    const char a[] = "abc";
    const char b[] = "abd";
    CHECK(fnv1a_64(a, sizeof(a)) != fnv1a_64(b, sizeof(b)));
}
