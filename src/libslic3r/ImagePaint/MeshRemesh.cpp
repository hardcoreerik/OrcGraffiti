#include "MeshRemesh.hpp"

#include "FaceSampler.hpp"
#include "ColorSpace.hpp"
#include "FilamentMatcher.hpp"

#include "libslic3r/Triangulation.hpp"
#include "libslic3r/MarchingSquares.hpp"
#include "libslic3r/IntersectionPoints.hpp"

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Triangulation_vertex_base_with_info_2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>

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

std::uint8_t classify_point(const Vec3d& p,
                            const DecodedImage& image,
                            const PlanarProjectionSettings& projection,
                            const std::vector<SourceCluster>& clusters)
{
    const auto pp = project_planar(p, projection);
    if (!pp.inside)
        return kSentinelUnpainted;
    const ColorRgba8 px = sample_bilinear(image, pp.u, pp.v);
    if (px.a == 0)
        return kSentinelUnpainted;
    const ColorRgbf lin{srgb_to_linear(px.r), srgb_to_linear(px.g), srgb_to_linear(px.b)};
    const auto cid = assign_to_cluster(linear_to_lab(lin), clusters);
    return static_cast<std::uint8_t>(std::min<std::uint32_t>(cid, 253));
}

SelectorState classify_to_state(const Vec3d& p,
                                const DecodedImage& image,
                                const PlanarProjectionSettings& projection,
                                const std::vector<SourceCluster>& clusters,
                                const std::vector<SelectorState>& cluster_state)
{
    const auto id = classify_point(p, image, projection, clusters);
    if (id == kSentinelUnpainted || id == kSentinelOutsideTriangle)
        return kStateNone;
    return id < cluster_state.size() ? cluster_state[id] : kStateNone;
}

// Point deduplication: Triangulation::triangulate() requires exactly-unique
// input points, but independently-traced contour rings can produce points
// that coincide (or nearly so) at grid-cell boundaries. Snap to a fine grid
// and collapse.
struct PointDeduper {
    static constexpr double kSnap = 1.0 / 4096.0; // ~0.02% of the [0,1] domain

    std::map<std::pair<long, long>, uint32_t> index_of;
    std::vector<Vec2d> uv;
    std::vector<int>   global; // -1 until assigned (interior points)

    uint32_t get_or_add(double u, double v, int global_idx = -1)
    {
        u = std::clamp(u, 0.0, 1.0);
        v = std::clamp(v, 0.0, 1.0);
        const auto key = std::make_pair(std::lround(u / kSnap), std::lround(v / kSnap));
        if (auto it = index_of.find(key); it != index_of.end()) {
            if (global_idx >= 0 && global[it->second] < 0)
                global[it->second] = global_idx;
            return it->second;
        }
        const auto idx = static_cast<uint32_t>(uv.size());
        uv.push_back({u, v});
        global.push_back(global_idx);
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
            grid.id[static_cast<std::size_t>(i) * n + j] =
                classify_point(p, image, projection, clusters);
        }
    }
    return grid;
}

// Steiner vertex that must appear on one local edge of a face, already
// assigned a shared global vertex index so the adjacent face uses the
// exact same vertex.
struct EdgeSteiner {
    Vec2d uv    = Vec2d::Zero();
    int   global = -1;
};

using FaceSteiners = std::array<std::vector<EdgeSteiner>, 3>; // AB, AC, BC

void build_boundary_loop(int g0, int g1, int g2,
                         const FaceSteiners& steiners,
                         std::vector<int>& boundary,
                         std::vector<Vec2d>& boundary_uv)
{
    boundary.clear();
    boundary_uv.clear();
    boundary.reserve(3 + steiners[0].size() + steiners[1].size() + steiners[2].size());
    boundary_uv.reserve(boundary.capacity());

    auto push = [&](int g, const Vec2d& uv) {
        if (!boundary.empty() && boundary.back() == g)
            return;
        boundary.push_back(g);
        boundary_uv.push_back(uv);
    };

    push(g0, {0.0, 0.0});
    for (const auto& s : steiners[0])
        push(s.global, s.uv);
    push(g1, {1.0, 0.0});
    for (const auto& s : steiners[2])
        push(s.global, s.uv);
    push(g2, {0.0, 1.0});
    for (auto it = steiners[1].rbegin(); it != steiners[1].rend(); ++it)
        push(it->global, it->uv);

    if (boundary.size() >= 2 && boundary.front() == boundary.back()) {
        boundary.pop_back();
        boundary_uv.pop_back();
    }
}

// Triangulate the original triangle plus Steiner points on its edges.
// Fan-from-vertex-0 is NOT valid here: it emits chords that skip Steiners
// on edges incident to the apex, which reopens T-junctions. Instead:
//   0 refined edges → the original triangle
//   1 refined edge  → fan from the opposite vertex
//   2+ refined      → new centroid vertex, fan to every boundary edge
void emit_boundary_fan(int g0, int g1, int g2,
                       const FaceSteiners& steiners,
                       const Vec3f& va, const Vec3f& vb, const Vec3f& vc,
                       const DecodedImage& image,
                       const PlanarProjectionSettings& projection,
                       const std::vector<SourceCluster>& clusters,
                       const std::vector<SelectorState>& cluster_state,
                       bool classify_each,
                       SelectorState flat_state,
                       std::vector<Vec3f>& out_vertices,
                       std::vector<Vec3i32>& out_indices,
                       std::vector<SelectorState>& out_states)
{
    const Vec3d dva = va.cast<double>(), dvb = vb.cast<double>(), dvc = vc.cast<double>();
    auto emit_tri = [&](int a, int b, int c, const Vec2d& ua, const Vec2d& ub, const Vec2d& uc) {
        if (a == b || a == c || b == c)
            return;
        out_indices.push_back({a, b, c});
        if (!classify_each) {
            out_states.push_back(flat_state);
            return;
        }
        const Vec2d cuv = (ua + ub + uc) / 3.0;
        const Vec3d p = dva + cuv.x() * (dvb - dva) + cuv.y() * (dvc - dva);
        out_states.push_back(classify_to_state(p, image, projection, clusters, cluster_state));
    };

    int n_refined = 0;
    int refined = -1;
    for (int e = 0; e < 3; ++e) {
        if (!steiners[static_cast<std::size_t>(e)].empty()) {
            ++n_refined;
            refined = e;
        }
    }

    if (n_refined == 0) {
        emit_tri(g0, g1, g2, {0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0});
        return;
    }

    if (n_refined == 1) {
        int opp = g0;
        Vec2d opp_uv{0.0, 0.0};
        std::vector<int> chain;
        std::vector<Vec2d> chain_uv;
        if (refined == 0) { // AB, opposite C
            opp = g2; opp_uv = {0.0, 1.0};
            chain.push_back(g0); chain_uv.push_back({0.0, 0.0});
            for (const auto& s : steiners[0]) { chain.push_back(s.global); chain_uv.push_back(s.uv); }
            chain.push_back(g1); chain_uv.push_back({1.0, 0.0});
        } else if (refined == 1) { // AC, opposite B
            opp = g1; opp_uv = {1.0, 0.0};
            chain.push_back(g0); chain_uv.push_back({0.0, 0.0});
            for (const auto& s : steiners[1]) { chain.push_back(s.global); chain_uv.push_back(s.uv); }
            chain.push_back(g2); chain_uv.push_back({0.0, 1.0});
        } else { // BC, opposite A
            opp = g0; opp_uv = {0.0, 0.0};
            chain.push_back(g1); chain_uv.push_back({1.0, 0.0});
            for (const auto& s : steiners[2]) { chain.push_back(s.global); chain_uv.push_back(s.uv); }
            chain.push_back(g2); chain_uv.push_back({0.0, 1.0});
        }
        for (std::size_t i = 0; i + 1 < chain.size(); ++i)
            emit_tri(opp, chain[i], chain[i + 1], opp_uv, chain_uv[i], chain_uv[i + 1]);
        return;
    }

    std::vector<int> boundary;
    std::vector<Vec2d> boundary_uv;
    build_boundary_loop(g0, g1, g2, steiners, boundary, boundary_uv);
    if (boundary.size() < 3) {
        emit_tri(g0, g1, g2, {0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0});
        return;
    }

    const int gc = static_cast<int>(out_vertices.size());
    out_vertices.push_back(((dva + dvb + dvc) / 3.0).cast<float>());
    const Vec2d cent{1.0 / 3.0, 1.0 / 3.0};
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        const std::size_t j = (i + 1) % boundary.size();
        emit_tri(gc, boundary[i], boundary[j], cent, boundary_uv[i], boundary_uv[j]);
    }
}

// CDT of a 2D point set that returns EVERY finite face (the convex hull
// fill). Triangulation::triangulate() is the wrong tool here: it flood-fills
// only the interior of oriented constraint rings, and insert_constraint of
// overlapping-but-not-crossing segments is Exact_predicates_tag UB (the
// v1 SIGSEGV). We uniquify scaled points, insert optional interior
// constraints only after an intersection test, and keep all finite faces.
Triangulation::Indices
triangulate_face_points(const Points& points, const Triangulation::HalfEdges& constraints)
{
    if (points.size() < 3)
        return {};

    using K   = CGAL::Exact_predicates_inexact_constructions_kernel;
    using Vb  = CGAL::Triangulation_vertex_base_with_info_2<uint32_t, K>;
    using Fb  = CGAL::Constrained_triangulation_face_base_2<K>;
    using Tds = CGAL::Triangulation_data_structure_2<Vb, Fb>;
    using CDT = CGAL::Constrained_Delaunay_triangulation_2<K, Tds, CGAL::Exact_predicates_tag>;

    CDT cdt;
    std::vector<CDT::Vertex_handle> handles(points.size());
    try {
        for (std::size_t i = 0; i < points.size(); ++i) {
            auto h = cdt.insert(CDT::Point(points[i].x(), points[i].y()));
            h->info() = static_cast<uint32_t>(i);
            handles[i] = h;
        }
        for (const auto& e : constraints) {
            if (e.first == e.second)
                continue;
            if (e.first >= points.size() || e.second >= points.size())
                continue;
            if (handles[e.first] == handles[e.second])
                continue;
            cdt.insert_constraint(handles[e.first], handles[e.second]);
        }
    } catch (...) {
        return {};
    }

    Triangulation::Indices out;
    for (auto fh : cdt.finite_face_handles()) {
        const uint32_t i0 = fh->vertex(0)->info();
        const uint32_t i1 = fh->vertex(1)->info();
        const uint32_t i2 = fh->vertex(2)->info();
        if (i0 == i1 || i0 == i2 || i1 == i2)
            continue;
        out.push_back({static_cast<int>(i0), static_cast<int>(i1), static_cast<int>(i2)});
    }
    return out;
}

enum class OnTriangleEdge : int { None = -1, AB = 0, AC = 1, BC = 2 };

OnTriangleEdge on_triangle_edge(double u, double v, double eps)
{
    if (v <= eps && u >= -eps && u <= 1.0 + eps)
        return OnTriangleEdge::AB;
    if (u <= eps && v >= -eps && v <= 1.0 + eps)
        return OnTriangleEdge::AC;
    if (std::abs(u + v - 1.0) <= eps && u >= -eps && v >= -eps)
        return OnTriangleEdge::BC;
    return OnTriangleEdge::None;
}

void clamp_to_triangle(double& u, double& v)
{
    u = std::max(0.0, u);
    v = std::max(0.0, v);
    if (u + v > 1.0) {
        const double s = u + v;
        u /= s;
        v /= s;
    }
}

// Remesh a single candidate face: classify a local grid, trace each present
// cluster's boundary within it, constrained-Delaunay-triangulate the result,
// and lift every new triangle back to 3D via barycentric interpolation of
// (va, vb, vc). Shared-edge Steiner points are injected as CDT input (and
// marching-squares hits on a triangle edge snap to them) so adjacent faces
// agree on where a color boundary crosses the shared edge.
//
// Never fails outright — on any local degeneracy it falls back to a
// boundary fan that still includes the Steiner points, so one awkward face
// can't abort the whole remesh or reopen a T-junction.
void remesh_face(const Vec3f& va, const Vec3f& vb, const Vec3f& vc,
                 int g0, int g1, int g2,
                 const FaceSteiners& steiners,
                 bool color_remesh,
                 const DecodedImage& image, const PlanarProjectionSettings& projection,
                 const std::vector<SourceCluster>& clusters,
                 const std::vector<SelectorState>& cluster_state,
                 int grid_resolution,
                 std::vector<Vec3f>& out_vertices,
                 std::vector<Vec3i32>& out_indices,
                 std::vector<SelectorState>& out_states)
{
    auto emit_flat = [&](SelectorState state) {
        emit_boundary_fan(g0, g1, g2, steiners, va, vb, vc, image, projection,
                          clusters, cluster_state, /*classify_each=*/false, state,
                          out_vertices, out_indices, out_states);
    };

    if (!color_remesh) {
        emit_flat(kStateNone);
        return;
    }

    const int n = std::max(3, grid_resolution);
    const ClassificationGrid grid = build_classification_grid(va, vb, vc, image, projection, clusters, n);

    std::vector<std::uint8_t> present;
    for (auto id : grid.id)
        if (id != kSentinelOutsideTriangle && id != kSentinelUnpainted &&
            std::find(present.begin(), present.end(), id) == present.end())
            present.push_back(id);

    const Vec3d centroid = (va.cast<double>() + vb.cast<double>() + vc.cast<double>()) / 3.0;
    const SelectorState centroid_state =
        classify_to_state(centroid, image, projection, clusters, cluster_state);

    if (present.empty()) {
        emit_boundary_fan(g0, g1, g2, steiners, va, vb, vc, image, projection,
                          clusters, cluster_state, /*classify_each=*/true, centroid_state,
                          out_vertices, out_indices, out_states);
        return;
    }

    PointDeduper dedup;
    std::array<std::vector<uint32_t>, 3> edge_local{};

    const uint32_t c0 = dedup.get_or_add(0.0, 0.0, g0);
    const uint32_t c1 = dedup.get_or_add(1.0, 0.0, g1);
    const uint32_t c2 = dedup.get_or_add(0.0, 1.0, g2);
    edge_local[0].push_back(c0);
    edge_local[0].push_back(c1);
    edge_local[1].push_back(c0);
    edge_local[1].push_back(c2);
    edge_local[2].push_back(c1);
    edge_local[2].push_back(c2);

    for (int e = 0; e < 3; ++e) {
        for (const auto& s : steiners[e]) {
            const uint32_t id = dedup.get_or_add(s.uv.x(), s.uv.y(), s.global);
            edge_local[e].push_back(id);
        }
    }

    auto snap_to_edge = [&](int e, double u, double v) -> uint32_t {
        uint32_t best = edge_local[e].front();
        double best_d2 = std::numeric_limits<double>::max();
        for (uint32_t id : edge_local[e]) {
            const double du = dedup.uv[id].x() - u;
            const double dv = dedup.uv[id].y() - v;
            const double d2 = du * du + dv * dv;
            if (d2 < best_d2) {
                best_d2 = d2;
                best = id;
            }
        }
        return best;
    };

    Triangulation::HalfEdges half_edges;
    std::set<std::pair<uint32_t, uint32_t>> seen_edges;
    const double edge_eps = 0.51 / static_cast<double>(n - 1);

    for (const auto target : present) {
        MaskedGrid masked{&grid, target};
        const std::vector<marchsq::Ring> rings = marchsq::execute(masked, 128, {1, 1});
        for (const marchsq::Ring& ring : rings) {
            if (ring.size() < 3) continue;
            std::vector<uint32_t> ring_indices;
            ring_indices.reserve(ring.size());
            for (const marchsq::Coord& coord : ring) {
                double u = static_cast<double>(coord.r) / (n - 1);
                double v = static_cast<double>(coord.c) / (n - 1);
                clamp_to_triangle(u, v);
                const auto on = on_triangle_edge(u, v, edge_eps);
                if (on != OnTriangleEdge::None)
                    ring_indices.push_back(snap_to_edge(static_cast<int>(on), u, v));
                else
                    ring_indices.push_back(dedup.get_or_add(u, v));
            }
            auto on_same_triangle_edge = [&](uint32_t a, uint32_t b) -> bool {
                // Both endpoints already live on the same original edge.
                // Adding that segment as a CDT constraint overlaps the
                // Steiner-split hull (or a longer corner-to-corner chord
                // through a Steiner) — the same precondition that crashed
                // v1 when the outer triangle was constrained explicitly.
                // Delaunay of the point set already includes consecutive
                // hull edges; we only need interior color-boundary constraints.
                for (int e = 0; e < 3; ++e) {
                    bool a_on = false, b_on = false;
                    for (uint32_t id : edge_local[e]) {
                        if (id == a) a_on = true;
                        if (id == b) b_on = true;
                    }
                    if (a_on && b_on)
                        return true;
                }
                return false;
            };
            for (std::size_t k = 0; k < ring_indices.size(); ++k) {
                const uint32_t a = ring_indices[k];
                const uint32_t b = ring_indices[(k + 1) % ring_indices.size()];
                if (on_same_triangle_edge(a, b))
                    continue;
                add_constraint_unique(seen_edges, half_edges, a, b);
            }
        }
    }

    auto emit_fan_classified = [&]() {
        emit_boundary_fan(g0, g1, g2, steiners, va, vb, vc, image, projection,
                          clusters, cluster_state, /*classify_each=*/true, centroid_state,
                          out_vertices, out_indices, out_states);
    };

    // Uniquify in scaled integer space. Two UV points that survive the
    // 1/4096 snap can still land on the same scaled() Point; feeding
    // duplicates to CDT overwrites vertex info() and is a crash source.
    std::map<Point, uint32_t> scaled_index;
    Points points;
    std::vector<uint32_t> unique_of(dedup.uv.size());
    std::vector<uint32_t> unique_to_dedup;
    points.reserve(dedup.uv.size());
    unique_to_dedup.reserve(dedup.uv.size());
    for (std::size_t i = 0; i < dedup.uv.size(); ++i) {
        const Point sp = scaled(dedup.uv[i]);
        if (auto it = scaled_index.find(sp); it != scaled_index.end()) {
            unique_of[i] = it->second;
            if (dedup.global[i] >= 0 && dedup.global[unique_to_dedup[it->second]] < 0)
                unique_to_dedup[it->second] = static_cast<uint32_t>(i);
            continue;
        }
        const auto idx = static_cast<uint32_t>(points.size());
        scaled_index.emplace(sp, idx);
        points.push_back(sp);
        unique_of[i] = idx;
        unique_to_dedup.push_back(static_cast<uint32_t>(i));
    }

    Triangulation::HalfEdges unique_edges;
    std::set<std::pair<uint32_t, uint32_t>> unique_seen;
    for (const auto& he : half_edges) {
        const uint32_t a = unique_of[he.first];
        const uint32_t b = unique_of[he.second];
        add_constraint_unique(unique_seen, unique_edges, a, b);
    }

    // Force the Steiner-split hull into the CDT. Without this, inexact
    // constructions can treat a Steiner as slightly interior and emit the
    // original long edge — a T-junction against the neighbor that did
    // split that edge. Consecutive hull segments only meet at endpoints,
    // so they do not recreate the overlapping-constraint crash.
    {
        std::unordered_map<int, uint32_t> global_to_unique;
        for (std::size_t u = 0; u < unique_to_dedup.size(); ++u) {
            const int g = dedup.global[unique_to_dedup[u]];
            if (g >= 0)
                global_to_unique[g] = static_cast<uint32_t>(u);
        }
        std::vector<int> boundary;
        std::vector<Vec2d> boundary_uv;
        build_boundary_loop(g0, g1, g2, steiners, boundary, boundary_uv);
        for (std::size_t i = 0; i < boundary.size(); ++i) {
            const int ga = boundary[i];
            const int gb = boundary[(i + 1) % boundary.size()];
            const auto ia = global_to_unique.find(ga);
            const auto ib = global_to_unique.find(gb);
            if (ia == global_to_unique.end() || ib == global_to_unique.end())
                continue;
            add_constraint_unique(unique_seen, unique_edges, ia->second, ib->second);
        }
    }

    // If the combined constraint set intersects, keep only the hull
    // segments (those are a simple polygon and must stay) and drop the
    // interior color-boundary edges rather than calling insert_constraint
    // on a precondition violation.
    if (!unique_edges.empty()) {
        Lines constraint_lines;
        constraint_lines.reserve(unique_edges.size());
        for (const auto& he : unique_edges)
            constraint_lines.emplace_back(points[he.first], points[he.second]);
        if (!get_intersections(constraint_lines).empty()) {
            unique_edges.clear();
            unique_seen.clear();
            std::unordered_map<int, uint32_t> global_to_unique;
            for (std::size_t u = 0; u < unique_to_dedup.size(); ++u) {
                const int g = dedup.global[unique_to_dedup[u]];
                if (g >= 0)
                    global_to_unique[g] = static_cast<uint32_t>(u);
            }
            std::vector<int> boundary;
            std::vector<Vec2d> boundary_uv;
            build_boundary_loop(g0, g1, g2, steiners, boundary, boundary_uv);
            for (std::size_t i = 0; i < boundary.size(); ++i) {
                const auto ia = global_to_unique.find(boundary[i]);
                const auto ib = global_to_unique.find(boundary[(i + 1) % boundary.size()]);
                if (ia == global_to_unique.end() || ib == global_to_unique.end())
                    continue;
                add_constraint_unique(unique_seen, unique_edges, ia->second, ib->second);
            }
        }
    }

    if (points.size() < 3) {
        emit_fan_classified();
        return;
    }

    const auto tri_indices = triangulate_face_points(points, unique_edges);
    if (tri_indices.empty()) {
        emit_fan_classified();
        return;
    }

    const Vec3d dva = va.cast<double>(), dvb = vb.cast<double>(), dvc = vc.cast<double>();
    auto lift = [&](uint32_t dedup_idx) -> Vec3f {
        const Vec2d& uv = dedup.uv[dedup_idx];
        return (dva + uv.x() * (dvb - dva) + uv.y() * (dvc - dva)).cast<float>();
    };
    auto classify_unique = [&](uint32_t u0, uint32_t u1, uint32_t u2) -> SelectorState {
        const Vec2d centroid_uv = (dedup.uv[unique_to_dedup[u0]] +
                                   dedup.uv[unique_to_dedup[u1]] +
                                   dedup.uv[unique_to_dedup[u2]]) / 3.0;
        const Vec3d p = dva + centroid_uv.x() * (dvb - dva) + centroid_uv.y() * (dvc - dva);
        return classify_to_state(p, image, projection, clusters, cluster_state);
    };

    std::vector<int> unique_global(points.size(), -1);
    for (std::size_t u = 0; u < points.size(); ++u) {
        const uint32_t d = unique_to_dedup[u];
        if (dedup.global[d] >= 0) {
            unique_global[u] = dedup.global[d];
            continue;
        }
        unique_global[u] = static_cast<int>(out_vertices.size());
        out_vertices.push_back(lift(d));
    }

    {
        std::set<int> boundary_globals{g0, g1, g2};
        std::set<std::pair<int, int>> allowed_hull;
        std::vector<int> boundary;
        std::vector<Vec2d> boundary_uv;
        build_boundary_loop(g0, g1, g2, steiners, boundary, boundary_uv);
        for (std::size_t i = 0; i < boundary.size(); ++i) {
            boundary_globals.insert(boundary[i]);
            const int a = boundary[i];
            const int b = boundary[(i + 1) % boundary.size()];
            allowed_hull.insert(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
        }
        bool shortcut = false;
        for (const auto& tri : tri_indices) {
            const int gs[3] = {
                unique_global[static_cast<std::size_t>(tri(0))],
                unique_global[static_cast<std::size_t>(tri(1))],
                unique_global[static_cast<std::size_t>(tri(2))]
            };
            for (int e = 0; e < 3; ++e) {
                const int a = gs[e];
                const int b = gs[(e + 1) % 3];
                if (a < 0 || b < 0)
                    continue;
                if (!boundary_globals.count(a) || !boundary_globals.count(b))
                    continue;
                const auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
                if (!allowed_hull.count(key)) {
                    shortcut = true;
                    break;
                }
            }
            if (shortcut)
                break;
        }
        if (shortcut) {
            emit_fan_classified();
            return;
        }
    }

    const std::size_t indices_before = out_indices.size();
    for (const auto& tri : tri_indices) {
        const int a = unique_global[static_cast<std::size_t>(tri(0))];
        const int b = unique_global[static_cast<std::size_t>(tri(1))];
        const int c = unique_global[static_cast<std::size_t>(tri(2))];
        if (a < 0 || b < 0 || c < 0 || a == b || a == c || b == c)
            continue;
        out_indices.push_back({a, b, c});
        out_states.push_back(classify_unique(static_cast<uint32_t>(tri(0)),
                                             static_cast<uint32_t>(tri(1)),
                                             static_cast<uint32_t>(tri(2))));
    }

    if (out_indices.size() == indices_before)
        emit_fan_classified();
}

// Canonical undirected mesh edge, keyed by original vertex indices.
inline std::uint64_t pack_edge_key(int32_t a, int32_t b) noexcept
{
    const uint32_t lo = static_cast<uint32_t>(std::min(a, b));
    const uint32_t hi = static_cast<uint32_t>(std::max(a, b));
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
}

struct EdgeCrossing {
    double t = 0.0;   // (0,1) along min-index → max-index
    Vec3f  p = Vec3f::Zero();
    int    global = -1;
};

struct MeshEdge {
    int32_t v0 = 0; // min index
    int32_t v1 = 0; // max index
    std::vector<int> faces;
    std::vector<EdgeCrossing> crossings;
};

// Sample the 3D edge once. Color-boundary transitions become Steiner
// points that both adjacent faces will share. t is exclusive of the
// endpoints — those are already the original shared vertices.
void sample_edge_crossings(MeshEdge& edge,
                           const std::vector<Vec3f>& vertices,
                           const DecodedImage& image,
                           const PlanarProjectionSettings& projection,
                           const std::vector<SourceCluster>& clusters,
                           int grid_resolution)
{
    const Vec3d p0 = vertices[edge.v0].cast<double>();
    const Vec3d p1 = vertices[edge.v1].cast<double>();
    const int samples = std::max(16, grid_resolution * 2);
    std::vector<std::uint8_t> id(static_cast<std::size_t>(samples));
    for (int k = 0; k < samples; ++k) {
        const double t = static_cast<double>(k) / static_cast<double>(samples - 1);
        id[static_cast<std::size_t>(k)] =
            classify_point(p0 + t * (p1 - p0), image, projection, clusters);
    }

    // Two crossings closer than this collapse in PointDeduper's snap grid
    // (kSnap = 1/4096 of the local [0,1] edge param). Keep them apart.
    constexpr double kMinSep = 2.0 / 4096.0;
    constexpr double kEndEps = 2.0 / 4096.0;

    for (int k = 0; k + 1 < samples; ++k) {
        if (id[static_cast<std::size_t>(k)] == id[static_cast<std::size_t>(k + 1)])
            continue;
        double lo = static_cast<double>(k) / static_cast<double>(samples - 1);
        double hi = static_cast<double>(k + 1) / static_cast<double>(samples - 1);
        const auto id_lo = id[static_cast<std::size_t>(k)];
        for (int iter = 0; iter < 20; ++iter) {
            const double mid = 0.5 * (lo + hi);
            const auto id_mid = classify_point(p0 + mid * (p1 - p0), image, projection, clusters);
            if (id_mid == id_lo) lo = mid;
            else                 hi = mid;
        }
        const double t = 0.5 * (lo + hi);
        if (t <= kEndEps || t >= 1.0 - kEndEps)
            continue;
        if (!edge.crossings.empty() && std::abs(t - edge.crossings.back().t) < kMinSep)
            continue;
        EdgeCrossing c;
        c.t = t;
        c.p = (p0 + t * (p1 - p0)).cast<float>();
        edge.crossings.push_back(c);
    }
}

FaceSteiners steiners_for_face(int ia, int ib, int ic,
                               const std::unordered_map<std::uint64_t, MeshEdge>& edge_map)
{
    FaceSteiners out;
    auto fill = [&](int a, int b, int slot) {
        const auto it = edge_map.find(pack_edge_key(a, b));
        if (it == edge_map.end())
            return;
        const bool a_is_min = a < b;
        for (const auto& c : it->second.crossings) {
            if (c.global < 0)
                continue;
            const double t_local = a_is_min ? c.t : 1.0 - c.t;
            EdgeSteiner s;
            if (slot == 0)      s.uv = {t_local, 0.0};
            else if (slot == 1) s.uv = {0.0, t_local};
            else                s.uv = {1.0 - t_local, t_local};
            s.global = c.global;
            out[static_cast<std::size_t>(slot)].push_back(s);
        }
        std::sort(out[static_cast<std::size_t>(slot)].begin(),
                  out[static_cast<std::size_t>(slot)].end(),
                  [slot](const EdgeSteiner& x, const EdgeSteiner& y) {
                      // AB: increasing u. AC / BC: increasing v
                      // (BC lives on u+v=1, so u+v is constant and cannot sort).
                      if (slot == 0)
                          return x.uv.x() < y.uv.x();
                      return x.uv.y() < y.uv.y();
                  });
    };
    fill(ia, ib, 0);
    fill(ia, ic, 1);
    fill(ib, ic, 2);
    return out;
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

    std::vector<char> is_candidate(n_faces, 0);
    for (std::size_t i = 0; i < n_faces; ++i) {
        if (!samples[i].inside)
            continue;
        const auto cid = assign_to_cluster(linear_to_lab(samples[i].linear_rgb), clusters);
        if (cid < cluster_state.size() && cluster_state[cid] != kStateNone)
            is_candidate[i] = 1;
    }

    std::unordered_map<std::uint64_t, MeshEdge> edge_map;
    edge_map.reserve(n_faces * 2);
    for (std::size_t fi = 0; fi < n_faces; ++fi) {
        const auto& t = indices[fi];
        for (int e = 0; e < 3; ++e) {
            const int32_t a = t[e];
            const int32_t b = t[(e + 1) % 3];
            auto& edge = edge_map[pack_edge_key(a, b)];
            if (edge.faces.empty()) {
                edge.v0 = std::min(a, b);
                edge.v1 = std::max(a, b);
            }
            edge.faces.push_back(static_cast<int>(fi));
        }
    }

    // Sample only edges that touch a paint candidate — back-faces that
    // happen to project onto the same UV must not grow Steiner points and
    // pull the remesh set across the whole mesh.
    for (auto& [key, edge] : edge_map) {
        (void)key;
        bool touches_candidate = false;
        for (int f : edge.faces) {
            if (is_candidate[static_cast<std::size_t>(f)]) {
                touches_candidate = true;
                break;
            }
        }
        if (!touches_candidate)
            continue;
        sample_edge_crossings(edge, vertices, image, projection, clusters, grid_resolution);
        if (cancel && cancel())
            return make_unexpected(ImagePaintError{ImagePaintErrorCode::Canceled, "Cancelled."});
    }

    // One-ring expansion: any non-candidate that shares a crossed edge
    // with a candidate is remeshed just enough to include those Steiners.
    std::vector<char> remesh_flag = is_candidate;
    for (const auto& [key, edge] : edge_map) {
        (void)key;
        if (edge.crossings.empty())
            continue;
        bool touches_candidate = false;
        for (int f : edge.faces) {
            if (is_candidate[static_cast<std::size_t>(f)]) {
                touches_candidate = true;
                break;
            }
        }
        if (!touches_candidate)
            continue;
        for (int f : edge.faces)
            remesh_flag[static_cast<std::size_t>(f)] = 1;
    }

    BakedMesh result;
    result.mesh.vertices = vertices; // original vertices keep their indices

    for (auto& [key, edge] : edge_map) {
        (void)key;
        if (edge.crossings.empty())
            continue;
        bool all_remeshed = true;
        bool any_remeshed = false;
        for (int f : edge.faces) {
            if (remesh_flag[static_cast<std::size_t>(f)])
                any_remeshed = true;
            else
                all_remeshed = false;
        }
        // Only emit Steiners when every incident face will consume them.
        // A leftover unremeshed neighbor would otherwise become a T-junction.
        if (!any_remeshed || (!all_remeshed && edge.faces.size() >= 2))
            continue;
        for (auto& c : edge.crossings) {
            c.global = static_cast<int>(result.mesh.vertices.size());
            result.mesh.vertices.push_back(c.p);
        }
    }

    for (std::size_t i = 0; i < n_faces; ++i) {
        if (cancel && cancel())
            return make_unexpected(ImagePaintError{ImagePaintErrorCode::Canceled, "Cancelled."});

        const auto& t = indices[i];
        if (!remesh_flag[i]) {
            result.mesh.indices.push_back(t);
            result.triangle_states.push_back(kStateNone);
            continue;
        }

        const FaceSteiners steiners = steiners_for_face(t[0], t[1], t[2], edge_map);
        remesh_face(vertices[t[0]], vertices[t[1]], vertices[t[2]],
                    t[0], t[1], t[2], steiners,
                    is_candidate[i] != 0,
                    image, projection, clusters, cluster_state, grid_resolution,
                    result.mesh.vertices, result.mesh.indices, result.triangle_states);
    }

    if (result.mesh.indices.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::NoEligibleFaces, "Remesh produced no geometry."});

    return result;
}

} // namespace Slic3r::ImagePaint
