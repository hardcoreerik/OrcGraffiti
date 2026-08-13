#include "MeshRemesh.hpp"

#include "FaceSampler.hpp"
#include "ColorSpace.hpp"
#include "ColorDifference.hpp"
#include "FilamentMatcher.hpp"

#include "libslic3r/Triangulation.hpp"
#include "libslic3r/MarchingSquares.hpp"
#include "libslic3r/IntersectionPoints.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>

namespace Slic3r::ImagePaint {

constexpr std::uint8_t kSentinelOutsideTriangle = 255;
constexpr std::uint8_t kSentinelUnpainted       = 254;

// A local NxN grid of classification IDs (cluster id 0..252, or one of the
// sentinels above), in the barycentric (u,v) parametrization of one face:
// grid(row=i, col=j) corresponds to u=i/(n-1) (toward vb), v=j/(n-1) (toward vc).
struct ClassificationGrid {
    int n = 0;
    std::vector<std::uint8_t> id; // n*n, row-major

    std::uint8_t at(int row, int col) const { return id[static_cast<std::size_t>(row) * n + col]; }
};

// Binary view of a ClassificationGrid for one target id — what marching
// squares actually needs (a scalar field to threshold), rebuilt per cluster
// without re-running classification.
struct MaskedGrid {
    const ClassificationGrid* grid;
    std::uint8_t target;
};

} // namespace Slic3r::ImagePaint

// marchsq raster-trait specialization must live in namespace marchsq.
namespace marchsq {
template<> struct _RasterTraits<Slic3r::ImagePaint::MaskedGrid> {
    using ValueType = std::uint8_t;
    static std::uint8_t get(const Slic3r::ImagePaint::MaskedGrid& g, size_t row, size_t col)
    {
        return g.grid->at(static_cast<int>(row), static_cast<int>(col)) == g.target ? 255 : 0;
    }
    static size_t rows(const Slic3r::ImagePaint::MaskedGrid& g) { return static_cast<size_t>(g.grid->n); }
    static size_t cols(const Slic3r::ImagePaint::MaskedGrid& g) { return static_cast<size_t>(g.grid->n); }
};
} // namespace marchsq

namespace Slic3r::ImagePaint {

namespace {

// Same nearest-cluster assignment used by ImagePaintPipeline.cpp — duplicated
// here (both are small, self-contained) rather than exported, to keep the
// pipeline's internal helper private.
std::uint32_t assign_to_cluster(const ColorLab& lab, const std::vector<SourceCluster>& clusters)
{
    std::uint32_t best = 0;
    double best_d2 = std::numeric_limits<double>::max();
    for (const auto& c : clusters) {
        const ColorLab cl = rgb8_to_lab(c.representative);
        const double dl = lab.l - cl.l, da = lab.a - cl.a, db = lab.b - cl.b;
        const double d2 = dl*dl + da*da + db*db;
        if (d2 < best_d2) { best_d2 = d2; best = c.id; }
    }
    return best;
}

// Point deduplication: Triangulation::triangulate() requires exactly-unique
// input points, but independently-traced contour rings can produce points
// that coincide (or nearly so) at grid-cell boundaries. Snap to a fine grid
// and collapse.
struct PointDeduper {
    static constexpr double kSnap = 1.0 / 4096.0; // ~0.02% of the [0,1] domain

    std::map<std::pair<long, long>, uint32_t> index_of;
    std::vector<Vec2d> uv;

    uint32_t get_or_add(double u, double v)
    {
        u = std::clamp(u, 0.0, 1.0);
        v = std::clamp(v, 0.0, 1.0);
        const auto key = std::make_pair(std::lround(u / kSnap), std::lround(v / kSnap));
        if (auto it = index_of.find(key); it != index_of.end())
            return it->second;
        const auto idx = static_cast<uint32_t>(uv.size());
        uv.push_back({u, v});
        index_of.emplace(key, idx);
        return idx;
    }
};

// Adds (a,b) as a constraint unless degenerate or already present in either
// direction — Triangulation::triangulate() forbids both duplicate and
// bidirectional constrained edges (e.g. two clusters sharing a border trace
// the same physical edge from opposite sides).
void add_constraint_unique(std::set<std::pair<uint32_t, uint32_t>>& seen,
                           Triangulation::HalfEdges& out, uint32_t a, uint32_t b)
{
    if (a == b) return;
    const auto canon = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
    if (!seen.insert(canon).second) return; // already have this edge (either direction)
    out.push_back({a, b});
}

ClassificationGrid
build_classification_grid(const Vec3f& va, const Vec3f& vb, const Vec3f& vc,
                          const DecodedImage& image, const PlanarProjectionSettings& projection,
                          const std::vector<SourceCluster>& clusters, int n)
{
    ClassificationGrid grid;
    grid.n = n;
    grid.id.assign(static_cast<std::size_t>(n) * n, kSentinelOutsideTriangle);

    const Vec3d dva = va.cast<double>(), dvb = vb.cast<double>(), dvc = vc.cast<double>();
    for (int i = 0; i < n; ++i) {
        const double u = static_cast<double>(i) / (n - 1);
        for (int j = 0; j < n; ++j) {
            const double v = static_cast<double>(j) / (n - 1);
            if (u + v > 1.0)
                continue; // outside the triangle — leave as kSentinelOutsideTriangle

            const Vec3d p = dva + u * (dvb - dva) + v * (dvc - dva);
            const auto pp = project_planar(p, projection);
            if (!pp.inside) {
                grid.id[static_cast<std::size_t>(i) * n + j] = kSentinelUnpainted;
                continue;
            }
            const ColorRgba8 px = sample_bilinear(image, pp.u, pp.v);
            if (px.a == 0) {
                grid.id[static_cast<std::size_t>(i) * n + j] = kSentinelUnpainted;
                continue;
            }
            const ColorRgbf lin{srgb_to_linear(px.r), srgb_to_linear(px.g), srgb_to_linear(px.b)};
            const auto cid = assign_to_cluster(linear_to_lab(lin), clusters);
            grid.id[static_cast<std::size_t>(i) * n + j] = static_cast<std::uint8_t>(std::min<std::uint32_t>(cid, 253));
        }
    }
    return grid;
}

// Remesh a single candidate face: classify a local grid, trace each present
// cluster's boundary within it, constrained-Delaunay-triangulate the result,
// and lift every new triangle back to 3D via barycentric interpolation of
// (va, vb, vc). Appends to result/result_states; never fails outright — on
// any local degeneracy it falls back to leaving the face as its own single
// (unsplit) triangle with its centroid's classified state, so one awkward
// face can't abort the whole remesh.
void remesh_face(const Vec3f& va, const Vec3f& vb, const Vec3f& vc,
                 const DecodedImage& image, const PlanarProjectionSettings& projection,
                 const std::vector<SourceCluster>& clusters,
                 const std::vector<SelectorState>& cluster_state,
                 int grid_resolution,
                 std::vector<Vec3f>& out_vertices, std::vector<Vec3i32>& out_indices,
                 std::vector<SelectorState>& out_states)
{
    const int n = std::max(3, grid_resolution);
    const ClassificationGrid grid = build_classification_grid(va, vb, vc, image, projection, clusters, n);

    // Distinct real (non-sentinel) cluster ids actually present in this face.
    std::vector<std::uint8_t> present;
    for (auto id : grid.id)
        if (id != kSentinelOutsideTriangle && id != kSentinelUnpainted &&
            std::find(present.begin(), present.end(), id) == present.end())
            present.push_back(id);

    auto emit_flat_fallback = [&]() {
        // Classify by centroid and keep the face as one triangle — the
        // pre-existing (Option 1) behavior, used whenever CDT isn't
        // warranted or fails locally.
        const Vec3d centroid = (va.cast<double>() + vb.cast<double>() + vc.cast<double>()) / 3.0;
        const auto pp = project_planar(centroid, projection);
        SelectorState state = kStateNone;
        if (pp.inside) {
            const ColorRgba8 px = sample_bilinear(image, pp.u, pp.v);
            if (px.a > 0) {
                const ColorRgbf lin{srgb_to_linear(px.r), srgb_to_linear(px.g), srgb_to_linear(px.b)};
                const auto cid = assign_to_cluster(linear_to_lab(lin), clusters);
                if (cid < cluster_state.size())
                    state = cluster_state[cid];
            }
        }
        const auto base = static_cast<int>(out_vertices.size());
        out_vertices.push_back(va);
        out_vertices.push_back(vb);
        out_vertices.push_back(vc);
        out_indices.push_back({base, base + 1, base + 2});
        out_states.push_back(state);
    };

    if (present.empty()) {
        emit_flat_fallback();
        return;
    }

    PointDeduper dedup;
    Triangulation::HalfEdges half_edges;
    std::set<std::pair<uint32_t, uint32_t>> seen_edges;

    // Corners as POINTS only — not as explicit boundary-edge constraints.
    // Any point in this local grid satisfies u>=0, v>=0, u+v<=1, so these
    // 3 corners are always extreme points of the set's convex hull; the
    // Delaunay triangulation of a point set always includes its convex-hull
    // edges as real triangulation edges, so the triangle's true boundary
    // comes out exactly right without an explicit constraint. Adding one
    // explicitly was the source of a real (initially non-deterministic,
    // SIGSEGV-crashing) bug: wherever a cluster's own contour touches this
    // same boundary — the common case, not a rare one — its traced edge
    // overlaps-but-doesn't-coincide with the explicit one, which violates
    // CGAL's Exact_predicates_tag no-crossing-constraints precondition.
    const uint32_t c0 = dedup.get_or_add(0.0, 0.0);
    const uint32_t c1 = dedup.get_or_add(1.0, 0.0);
    const uint32_t c2 = dedup.get_or_add(0.0, 1.0);
    (void)c0; (void)c1; (void)c2;

    for (const auto target : present) {
        MaskedGrid masked{&grid, target};
        const std::vector<marchsq::Ring> rings = marchsq::execute(masked, 128, {1, 1});
        for (const marchsq::Ring& ring : rings) {
            if (ring.size() < 3) continue;
            std::vector<uint32_t> ring_indices;
            ring_indices.reserve(ring.size());
            for (const marchsq::Coord& coord : ring) {
                const double u = static_cast<double>(coord.r) / (n - 1);
                const double v = static_cast<double>(coord.c) / (n - 1);
                ring_indices.push_back(dedup.get_or_add(u, v));
            }
            for (std::size_t k = 0; k < ring_indices.size(); ++k) {
                const uint32_t a = ring_indices[k];
                const uint32_t b = ring_indices[(k + 1) % ring_indices.size()];
                add_constraint_unique(seen_edges, half_edges, a, b);
            }
        }
    }

    if (half_edges.size() < 3 || dedup.uv.size() < 3) {
        emit_flat_fallback();
        return;
    }

    std::sort(half_edges.begin(), half_edges.end());
    half_edges.erase(std::unique(half_edges.begin(), half_edges.end()), half_edges.end());

    Points points;
    points.reserve(dedup.uv.size());
    for (const auto& p : dedup.uv)
        points.push_back(scaled(p));

    // Triangulation::triangulate()'s CDT uses CGAL::Exact_predicates_tag,
    // which requires constrained edges to never properly cross one another
    // (only touch at shared endpoints) — violating that is undefined
    // behavior, not a catchable exception (confirmed by testing: an earlier
    // version of this function crashed non-deterministically, roughly 1 in
    // 3 runs, exactly matching this precondition being silently violated).
    // Independently-traced contours from different clusters can cross near
    // ambiguous marching-squares cells, so this check is load-bearing, not
    // defensive boilerplate — check it explicitly instead of relying on the
    // assert() Triangulation.cpp itself only runs in debug builds.
    Lines constraint_lines;
    constraint_lines.reserve(half_edges.size());
    for (const auto& he : half_edges)
        constraint_lines.emplace_back(points[he.first], points[he.second]);
    if (!get_intersections(constraint_lines).empty()) {
        emit_flat_fallback();
        return;
    }

    Triangulation::Indices tri_indices;
    try {
        tri_indices = Triangulation::triangulate(points, half_edges);
    } catch (...) {
        emit_flat_fallback();
        return;
    }

    if (tri_indices.empty()) {
        emit_flat_fallback();
        return;
    }

    const Vec3d dva = va.cast<double>(), dvb = vb.cast<double>(), dvc = vc.cast<double>();
    auto lift = [&](uint32_t idx) -> Vec3f {
        const Vec2d& uv = dedup.uv[idx];
        return (dva + uv.x() * (dvb - dva) + uv.y() * (dvc - dva)).cast<float>();
    };
    auto classify = [&](uint32_t i0, uint32_t i1, uint32_t i2) -> SelectorState {
        const Vec2d centroid_uv = (dedup.uv[i0] + dedup.uv[i1] + dedup.uv[i2]) / 3.0;
        const Vec3d p = dva + centroid_uv.x() * (dvb - dva) + centroid_uv.y() * (dvc - dva);
        const auto pp = project_planar(p, projection);
        if (!pp.inside) return kStateNone;
        const ColorRgba8 px = sample_bilinear(image, pp.u, pp.v);
        if (px.a == 0) return kStateNone;
        const ColorRgbf lin{srgb_to_linear(px.r), srgb_to_linear(px.g), srgb_to_linear(px.b)};
        const auto cid = assign_to_cluster(linear_to_lab(lin), clusters);
        return cid < cluster_state.size() ? cluster_state[cid] : kStateNone;
    };

    const auto base = static_cast<int>(out_vertices.size());
    for (std::size_t k = 0; k < dedup.uv.size(); ++k)
        out_vertices.push_back(lift(static_cast<uint32_t>(k)));

    for (const auto& tri : tri_indices) {
        out_indices.push_back({base + tri(0), base + tri(1), base + tri(2)});
        out_states.push_back(classify(static_cast<uint32_t>(tri(0)), static_cast<uint32_t>(tri(1)), static_cast<uint32_t>(tri(2))));
    }
}

} // namespace

Expected<BakedMesh, ImagePaintError>
remesh_by_color_boundary(const std::vector<Vec3f>&       vertices,
                         const std::vector<Vec3i32>&     indices,
                         const DecodedImage&              image,
                         const PlanarProjectionSettings&  projection,
                         const std::vector<FilamentColor>& filaments,
                         const QuantizationSettings&      quantization,
                         int                                grid_resolution,
                         const std::function<bool()>&      cancel)
{
    if (vertices.empty() || indices.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::NoEligibleFaces, "Mesh has no faces to remesh."});
    if (filaments.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::NoAvailableFilaments, "No filaments provided."});

    const std::size_t n_faces = indices.size();
    const ProjectionSettings proj_variant = projection;

    const auto samples = sample_faces(vertices, indices, image, proj_variant,
                                      SamplingQuality::Gaussian7, 0, n_faces, cancel);
    if (cancel && cancel())
        return make_unexpected(ImagePaintError{ImagePaintErrorCode::Canceled, "Cancelled."});

    std::vector<double> face_area(n_faces, 0.0);
    for (std::size_t i = 0; i < n_faces; ++i) {
        const auto& t = indices[i];
        const Vec3d ab = (vertices[t[1]] - vertices[t[0]]).cast<double>();
        const Vec3d ac = (vertices[t[2]] - vertices[t[0]]).cast<double>();
        face_area[i] = 0.5 * ab.cross(ac).norm();
    }

    std::vector<ColorSample> color_samples;
    color_samples.reserve(samples.size());
    for (const auto& s : samples) {
        if (!s.inside) continue;
        const double weight = std::max(face_area[s.face_index] * static_cast<double>(s.alpha), 1e-6);
        color_samples.push_back({linear_to_lab(s.linear_rgb), weight});
    }
    if (color_samples.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::NoEligibleFaces, "Image does not touch any face."});

    const auto clusters = quantize_colors(color_samples, quantization, cancel);
    if (clusters.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::QuantizationFailed, "Colour quantization produced no clusters."});
    if (cancel && cancel())
        return make_unexpected(ImagePaintError{ImagePaintErrorCode::Canceled, "Cancelled."});

    auto match_result = match_clusters_to_filaments(clusters, filaments, quantization.one_to_one_filament_match);
    if (!match_result)
        return make_unexpected(match_result.error());

    std::vector<SelectorState> cluster_state(clusters.size(), kStateNone);
    for (const auto& m : *match_result) {
        if (m.cluster_id >= cluster_state.size()) continue;
        auto sr = filament_index_to_selector_state(m.filament_index);
        if (sr) cluster_state[m.cluster_id] = *sr;
    }

    BakedMesh result;
    for (std::size_t i = 0; i < n_faces; ++i) {
        if (cancel && cancel())
            return make_unexpected(ImagePaintError{ImagePaintErrorCode::Canceled, "Cancelled."});

        bool is_candidate = false;
        if (samples[i].inside) {
            const auto cid = assign_to_cluster(linear_to_lab(samples[i].linear_rgb), clusters);
            is_candidate = cid < cluster_state.size() && cluster_state[cid] != kStateNone;
        }

        const auto& t = indices[i];
        if (!is_candidate) {
            const auto base = static_cast<int>(result.mesh.vertices.size());
            result.mesh.vertices.push_back(vertices[t[0]]);
            result.mesh.vertices.push_back(vertices[t[1]]);
            result.mesh.vertices.push_back(vertices[t[2]]);
            result.mesh.indices.push_back({base, base + 1, base + 2});
            result.triangle_states.push_back(kStateNone);
            continue;
        }

        remesh_face(vertices[t[0]], vertices[t[1]], vertices[t[2]], image, projection,
                   clusters, cluster_state, grid_resolution,
                   result.mesh.vertices, result.mesh.indices, result.triangle_states);
    }

    if (result.mesh.indices.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::NoEligibleFaces, "Remesh produced no geometry."});

    return result;
}

} // namespace Slic3r::ImagePaint
