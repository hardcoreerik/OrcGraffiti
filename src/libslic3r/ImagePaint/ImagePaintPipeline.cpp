#include "ImagePaintPipeline.hpp"

#include "ImageDecoder.hpp"
#include "ColorSpace.hpp"
#include "ColorDifference.hpp"
#include "FaceAdjacency.hpp"
#include "FilamentMatcher.hpp"

#include <cassert>
#include <cmath>
#include <limits>

namespace Slic3r::ImagePaint {

namespace {

struct FaceGeometry {
    Vec3d  normal;
    double area_mm2;
};

FaceGeometry compute_face_geometry(const Vec3f& va, const Vec3f& vb, const Vec3f& vc)
{
    const Vec3d ab = (vb - va).cast<double>();
    const Vec3d ac = (vc - va).cast<double>();
    const Vec3d cross = ab.cross(ac);
    const double len = cross.norm();
    const Vec3d  normal = len > 1e-12 ? Vec3d(cross / len) : Vec3d::UnitZ();
    return {normal, 0.5 * len};
}

// Assign a face Lab colour to the nearest cluster centroid by CIE76 squared distance.
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

Expected<FacePaintPlan, ImagePaintError>
run_pipeline(const ImagePaintRequest& req, const DecodedImage& image,
             const std::function<bool()>& cancel)
{
    const std::size_t n_faces = req.indices.size();

    if (req.vertices.empty() || req.indices.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::NoEligibleFaces, "Mesh has no faces to paint."});

    if (req.filaments.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::NoAvailableFilaments, "No filaments provided."});

    // --- Topology fingerprint ---
    std::vector<float>   flat_verts;
    std::vector<int32_t> flat_idx;
    flat_verts.reserve(req.vertices.size() * 3);
    flat_idx.reserve(req.indices.size() * 3);
    for (const auto& v : req.vertices) {
        flat_verts.push_back(v[0]); flat_verts.push_back(v[1]); flat_verts.push_back(v[2]);
    }
    for (const auto& t : req.indices) {
        flat_idx.push_back(t[0]); flat_idx.push_back(t[1]); flat_idx.push_back(t[2]);
    }
    const auto fp = fingerprint_from_arrays(flat_verts, flat_idx);

    // --- Per-face geometry ---
    std::vector<Vec3d>  face_normals(n_faces);
    std::vector<double> face_areas(n_faces);
    for (std::size_t i = 0; i < n_faces; ++i) {
        const auto& t = req.indices[i];
        const auto fg = compute_face_geometry(req.vertices[t[0]], req.vertices[t[1]], req.vertices[t[2]]);
        face_normals[i] = fg.normal;
        face_areas[i]   = fg.area_mm2;
    }

    if (cancel && cancel())
        return make_unexpected(ImagePaintError{ImagePaintErrorCode::Canceled, "Cancelled."});

    // --- Sample faces ---
    const auto samples = sample_faces(req.vertices, req.indices, image,
                                       req.projection, req.quality, 0, n_faces, cancel);

    if (cancel && cancel())
        return make_unexpected(ImagePaintError{ImagePaintErrorCode::Canceled, "Cancelled."});

    // --- Build colour samples (weight = face area × alpha) ---
    std::vector<ColorSample> color_samples;
    color_samples.reserve(samples.size());
    for (const auto& s : samples) {
        if (!s.inside) continue;
        const ColorLab lab = linear_to_lab(s.linear_rgb);
        const double weight = std::max(face_areas[s.face_index] * static_cast<double>(s.alpha), 1e-6);
        color_samples.push_back({lab, weight});
    }

    PaintDiagnostics diag;
    diag.total_faces     = n_faces;
    diag.candidate_faces = color_samples.size();

    if (color_samples.empty()) {
        FacePaintPlan plan;
        plan.states = req.existing_states.empty()
                      ? make_blank_states(n_faces) : req.existing_states;
        plan.diagnostics = diag;
        plan.fingerprint = fp;
        return plan;
    }

    // --- Quantize ---
    const auto clusters = quantize_colors(color_samples, req.quantization, cancel);
    if (clusters.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::QuantizationFailed, "Colour quantization produced no clusters."});

    if (cancel && cancel())
        return make_unexpected(ImagePaintError{ImagePaintErrorCode::Canceled, "Cancelled."});

    // --- Match clusters to filaments ---
    auto match_result = match_clusters_to_filaments(
        clusters, req.filaments, req.quantization.one_to_one_filament_match);
    if (!match_result)
        return make_unexpected(match_result.error());
    const auto& matches = *match_result;

    // Build cluster_id → SelectorState table.
    std::vector<SelectorState> cluster_state(clusters.size(), kStateNone);
    for (const auto& m : matches) {
        if (m.cluster_id >= cluster_state.size()) continue;
        auto sr = filament_index_to_selector_state(m.filament_index);
        if (sr) cluster_state[m.cluster_id] = *sr;
    }

    // --- Assign per-face states ---
    std::vector<SelectorState> proposed(n_faces, kStateNone);
    std::uint64_t painted_faces = 0;
    double painted_area = 0.0;

    for (const auto& s : samples) {
        if (!s.inside) continue;
        const std::uint32_t cid = assign_to_cluster(linear_to_lab(s.linear_rgb), clusters);
        if (cid < cluster_state.size() && cluster_state[cid] != kStateNone) {
            proposed[s.face_index] = cluster_state[cid];
            ++painted_faces;
            painted_area += face_areas[s.face_index];
        }
    }

    diag.painted_faces            = painted_faces;
    diag.painted_surface_area_mm2 = painted_area;

    if (cancel && cancel())
        return make_unexpected(ImagePaintError{ImagePaintErrorCode::Canceled, "Cancelled."});

    // --- Clean tiny regions ---
    if (req.cleanup.enabled && n_faces > 0) {
        const auto adj = build_face_adjacency(req.indices);
        diag.tiny_components = clean_tiny_regions(
            proposed, adj, face_normals, face_areas, req.cleanup, cancel);
    }

    // --- Merge with existing states ---
    const auto existing = req.existing_states.empty()
                          ? make_blank_states(n_faces) : req.existing_states;
    const auto merged = merge_paint_states(existing, proposed, req.merge_policy);

    FacePaintPlan plan;
    plan.states      = merged;
    plan.matches     = matches;
    plan.diagnostics = diag;
    plan.fingerprint = fp;
    return plan;
}

} // namespace

Expected<FacePaintPlan, ImagePaintError>
run_image_paint(const ImagePaintRequest& request, const std::function<bool()>& cancel)
{
    auto img = decode_image(request.image_path, request.decode_limits);
    if (!img)
        return make_unexpected(img.error());
    return run_pipeline(request, *img, cancel);
}

Expected<FacePaintPlan, ImagePaintError>
run_image_paint(const ImagePaintRequest& request, const DecodedImage& image,
                const std::function<bool()>& cancel)
{
    return run_pipeline(request, image, cancel);
}

} // namespace Slic3r::ImagePaint
