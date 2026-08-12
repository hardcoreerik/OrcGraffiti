// orcgraffiti — headless CLI for OrcGraffiti Image Paint agent surface.
// AS-1 (CLI skeleton): version, help, info. See docs/OrcGraffiti/Agent_Surface.md §6.
//
// Single-TU executable linking libslic3r only — no wxWidgets/ImGui/OpenGL/Plater.
// Mirrors the OrcaSlicer_profile_validator pattern (dev-utils) for nanosvg ordering.
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

#include <set>
#include "libslic3r_version.h"
#include "libslic3r/ImagePaint/TopologyFingerprint.hpp"
#include "libslic3r/ImagePaint/ImagePaintPipeline.hpp"
#include "libslic3r/ImagePaint/ImageDecoder.hpp"

#include <boost/filesystem.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <utility>

using namespace Slic3r;
using json = nlohmann::json;

namespace {

void print_help()
{
    std::cout <<
        "orcgraffiti <command> [options]\n"
        "\n"
        "Commands:\n"
        "  version              Print version / build type\n"
        "  help                 Show this help\n"
        "  info <input>         Inspect a model/project (JSON)\n"
        "  paint <input>        Run the paint pipeline (--dry-run report, or --out write)\n"
        "\n"
        "Global options:\n"
        "  --report <path.json> Write machine-readable report to a file\n"
        "\n"
        "paint options:\n"
        "  --image <path>       Source image (PNG/JPG/BMP), required\n"
        "  --filaments <path>   Filament palette JSON (see Agent_Surface.md §6.7), required\n"
        "  --object <i>         Object index (default 0)\n"
        "  --volume <i>         Volume index within object (default 0)\n"
        "  --view <preset>      front|back|left|right|top|bottom (default front)\n"
        "  --colors <n>         Target quantizer colors, 1-16 (default 4)\n"
        "  --quality <q>        fast|threepoint|gaussian7 (default gaussian7)\n"
        "  --merge <policy>     overwrite|preserve (default overwrite)\n"
        "  --dry-run            Run pipeline, write report only, no 3MF write\n"
        "  --out <path.3mf>     Write mode: apply the plan and save a BBS-native 3MF\n"
        "  --force              Allow overwriting an existing --out path\n"
        "  --allow-in-place     Allow --out to equal the input path (default: refused)\n"
        "\n"
        "Exactly one of --dry-run or --out is required for paint.\n"
        "\n"
        "NOTE: --out writes via Slic3r::store_bbs_3mf, this fork's native format.\n"
        "GUI reopen has not been exhaustively verified across all input shapes yet —\n"
        "a manual GUI check is still recommended for anything beyond a simple single-\n"
        "object model. See AI_STATUS.md's AS-3 notes and Agent_Surface.md.\n"
        "\n"
        "See docs/OrcGraffiti/Agent_Surface.md for the full CLI contract.\n";
}

void print_version()
{
    std::cout << "orcgraffiti " << SLIC3R_VERSION
               << " (agent-surface AS-3, ImagePaint core)\n";
}

std::string hex64(std::uint64_t v)
{
    char buf[19];
    std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(v));
    return std::string(buf);
}

json fingerprint_json(const indexed_triangle_set& its)
{
    std::vector<float> verts;
    verts.reserve(its.vertices.size() * 3);
    for (const auto& v : its.vertices) {
        verts.push_back(v.x());
        verts.push_back(v.y());
        verts.push_back(v.z());
    }
    std::vector<std::int32_t> idxs;
    idxs.reserve(its.indices.size() * 3);
    for (const auto& t : its.indices) {
        idxs.push_back(t[0]);
        idxs.push_back(t[1]);
        idxs.push_back(t[2]);
    }

    const auto fp = ImagePaint::fingerprint_from_arrays(verts, idxs);
    json j;
    j["connectivity_hash"] = hex64(fp.connectivity_hash);
    j["geometry_hash"]     = hex64(fp.geometry_hash);
    j["vertex_count"]      = fp.vertex_count;
    j["triangle_count"]    = fp.triangle_count;
    return j;
}

json bbox_json(const BoundingBoxf3& bb)
{
    json j;
    j["min"] = { bb.min.x(), bb.min.y(), bb.min.z() };
    j["max"] = { bb.max.x(), bb.max.y(), bb.max.z() };
    return j;
}

// Model::read_from_file's own default (LoadStrategy::AddDefaultInstances alone)
// omits LoadModel/LoadConfig/LoadAuxiliary — for .3mf inputs this makes the BBS
// 3MF importer populate zero objects (silently — no error, just an empty model).
// OrcaSlicer.cpp's own CLI ORs these three in for any .3mf load; do the same
// here. The extra bits are unused by the STL/OBJ loaders, so always including
// them is harmless for non-3MF inputs.
LoadStrategy full_load_strategy()
{
    return LoadStrategy::LoadModel | LoadStrategy::LoadConfig
         | LoadStrategy::AddDefaultInstances | LoadStrategy::LoadAuxiliary;
}

// Model::read_from_archive (the GUI's own "Open Project" path, per
// Plater.cpp) was tried here to make this CLI's self-check match what the
// GUI does for .3mf files, since it detects Prusa/generic 3MF and can read
// the slic3rpe:mmu_segmentation attribute Slic3r::store_3mf writes — but it
// segfaults even on a known-good fixture (tests/data/test_3mf), both before
// and after our own paint. Reverted to plain Model::read_from_file for all
// inputs. Consequence: this CLI's own info/paint --dry-run cannot currently
// detect paint written via --out on a .3mf output (see AI_STATUS.md) — a
// human GUI check remains the only way to confirm --out actually worked.
Model load_model_for_cli(const std::string& input_path)
{
    return Model::read_from_file(input_path, nullptr, nullptr, full_load_strategy());
}

// AS-1 exit gate: "info" JSON per Agent_Surface.md §6.4 on a fixture input.
int cmd_info(const std::string& input_path, const std::string& report_path)
{
    json report;
    report["command"] = "info";
    report["input"]   = input_path;

    Model model;
    try {
        model = load_model_for_cli(input_path);
    } catch (const std::exception& ex) {
        report["ok"] = false;
        report["error"] = { {"code", "ModelLoadFailed"}, {"message", ex.what()} };
        std::cerr << "orcgraffiti: failed to load '" << input_path << "': " << ex.what() << "\n";
        if (!report_path.empty()) {
            std::ofstream out(report_path);
            out << report.dump(2);
        } else {
            std::cout << report.dump(2) << "\n";
        }
        return 2; // domain error
    }

    json objects = json::array();
    for (std::size_t oi = 0; oi < model.objects.size(); ++oi) {
        const ModelObject* obj = model.objects[oi];
        json volumes = json::array();
        for (std::size_t vi = 0; vi < obj->volumes.size(); ++vi) {
            const ModelVolume* vol = obj->volumes[vi];
            const TriangleMesh& mesh = vol->mesh();
            const indexed_triangle_set& its = mesh.its;

            json jv;
            jv["index"]          = vi;
            jv["name"]           = vol->name;
            jv["triangle_count"] = its.indices.size();
            jv["vertex_count"]   = its.vertices.size();
            jv["bbox_mm"]        = bbox_json(mesh.bounding_box());
            jv["has_mmu_paint"]  = vol->is_mm_painted();
            jv["fingerprint"]    = fingerprint_json(its);
            volumes.push_back(std::move(jv));
        }

        json jo;
        jo["index"]   = oi;
        jo["name"]    = obj->name;
        jo["volumes"] = std::move(volumes);
        objects.push_back(std::move(jo));
    }

    report["ok"]      = true;
    report["objects"] = std::move(objects);

    const std::string text = report.dump(2);
    if (!report_path.empty()) {
        std::ofstream out(report_path);
        out << text;
    } else {
        std::cout << text << "\n";
    }
    return 0;
}

// View presets, volume-local, right-handed. "front" and "top" are locked to the
// golden-test conventions in tests/libslic3r/test_image_paint_pipeline.cpp
// (see Agent_Surface.md §6.5 implementation note); back/left/bottom are the
// mirror of front/right/top pending their own golden tests.
std::optional<std::pair<Vec3d, Vec3d>> view_preset(const std::string& name)
{
    if (name == "front")  return std::make_pair(Vec3d(0, 1, 0),  Vec3d(0, 0, 1));
    if (name == "back")   return std::make_pair(Vec3d(0, -1, 0), Vec3d(0, 0, 1));
    if (name == "right")  return std::make_pair(Vec3d(-1, 0, 0), Vec3d(0, 0, 1));
    if (name == "left")   return std::make_pair(Vec3d(1, 0, 0),  Vec3d(0, 0, 1));
    if (name == "top")    return std::make_pair(Vec3d(0, 0, -1), Vec3d(0, 1, 0));
    if (name == "bottom") return std::make_pair(Vec3d(0, 0, 1),  Vec3d(0, 1, 0));
    return std::nullopt;
}

ImagePaint::SamplingQuality quality_from_string(const std::string& s)
{
    if (s == "fast")       return ImagePaint::SamplingQuality::FastCentroid;
    if (s == "threepoint") return ImagePaint::SamplingQuality::ThreePoint;
    return ImagePaint::SamplingQuality::Gaussian7;
}

ImagePaint::MergePolicy merge_from_string(const std::string& s)
{
    if (s == "preserve") return ImagePaint::MergePolicy::PreserveExisting;
    return ImagePaint::MergePolicy::OverwriteInsideMask;
}

// Filament palette JSON per Agent_Surface.md §6.7:
// { "filaments": [ { "index": 0, "name": "...", "color_hex": "#RRGGBB" }, ... ] }
std::vector<ImagePaint::FilamentColor> load_filaments(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("cannot open filament palette file: " + path);
    json j;
    in >> j;

    std::vector<ImagePaint::FilamentColor> result;
    for (const auto& jf : j.at("filaments")) {
        ImagePaint::FilamentColor f;
        f.project_index = jf.at("index").get<ImagePaint::FilamentIndex>();
        f.name = jf.value("name", std::string());
        std::string hex = jf.at("color_hex").get<std::string>();
        if (hex.size() == 7 && hex[0] == '#') {
            f.display_rgb.r = static_cast<std::uint8_t>(std::stoi(hex.substr(1, 2), nullptr, 16));
            f.display_rgb.g = static_cast<std::uint8_t>(std::stoi(hex.substr(3, 2), nullptr, 16));
            f.display_rgb.b = static_cast<std::uint8_t>(std::stoi(hex.substr(5, 2), nullptr, 16));
        } else {
            throw std::runtime_error("filament '" + f.name + "' has malformed color_hex: " + hex);
        }
        result.push_back(f);
    }
    return result;
}

json diagnostics_json(const ImagePaint::PaintDiagnostics& d)
{
    json j;
    j["total_faces"]              = d.total_faces;
    j["candidate_faces"]          = d.candidate_faces;
    j["painted_faces"]            = d.painted_faces;
    j["transparent_faces"]        = d.transparent_faces;
    j["back_facing_faces"]        = d.back_facing_faces;
    j["occluded_faces"]           = d.occluded_faces;
    j["connected_components"]     = d.connected_components;
    j["tiny_components"]          = d.tiny_components;
    j["painted_surface_area_mm2"] = d.painted_surface_area_mm2;
    j["coarse_mesh_warning"]      = d.coarse_mesh_warning;
    j["warnings"]                 = d.warnings;
    return j;
}

json matches_json(const std::vector<ImagePaint::ClusterMatch>& matches)
{
    json arr = json::array();
    for (const auto& m : matches) {
        json jm;
        jm["cluster_id"]      = m.cluster_id;
        jm["filament_index"]  = m.filament_index;
        jm["delta_e"]         = m.delta_e;
        jm["user_overridden"] = m.user_overridden;
        arr.push_back(std::move(jm));
    }
    return arr;
}

std::string error_code_name(ImagePaint::ImagePaintErrorCode code)
{
    using C = ImagePaint::ImagePaintErrorCode;
    switch (code) {
    case C::NoSelection:                   return "NoSelection";
    case C::InvalidTarget:                 return "InvalidTarget";
    case C::UnsupportedVolume:              return "UnsupportedVolume";
    case C::ImageOpenFailed:               return "ImageOpenFailed";
    case C::ImageDecodeFailed:              return "ImageDecodeFailed";
    case C::ImageTooLarge:                 return "ImageTooLarge";
    case C::InvalidProjection:              return "InvalidProjection";
    case C::NoEligibleFaces:               return "NoEligibleFaces";
    case C::NoAvailableFilaments:           return "NoAvailableFilaments";
    case C::TooManyColors:                 return "TooManyColors";
    case C::QuantizationFailed:             return "QuantizationFailed";
    case C::Canceled:                      return "Canceled";
    case C::TargetDeleted:                 return "TargetDeleted";
    case C::TopologyChanged:               return "TopologyChanged";
    case C::FilamentConfigurationChanged:   return "FilamentConfigurationChanged";
    case C::FaceCountMismatch:              return "FaceCountMismatch";
    case C::FilamentOutOfRange:             return "FilamentOutOfRange";
    case C::ApplyFailed:                   return "ApplyFailed";
    case C::InternalInvariantViolation:     return "InternalInvariantViolation";
    }
    return "Unknown";
}

struct PaintOptions {
    std::string input_path;
    std::string image_path;
    std::string filaments_path;
    std::string report_path;
    std::string out_path;
    std::size_t object_index = 0;
    std::size_t volume_index = 0;
    std::string view          = "front";
    std::uint32_t colors      = 4;
    std::string quality       = "gaussian7";
    std::string merge         = "overwrite";
    bool dry_run              = false;
    bool force                = false;
    bool allow_in_place       = false;
};

int write_report_and_exit(const json& report, const std::string& report_path, int code)
{
    const std::string text = report.dump(2);
    if (!report_path.empty()) {
        std::ofstream out(report_path);
        out << text;
    } else {
        std::cout << text << "\n";
    }
    return code;
}

// AS-2 exit gate: "paint --dry-run" returns diagnostics matching the unit
// pipeline within tolerance, with no 3MF mutation. See Agent_Surface.md §6.8.
int cmd_paint(const PaintOptions& opt)
{
    json report;
    report["command"]  = "paint";
    report["version"]  = "orcgraffiti-agent-surface-0.1";
    report["input"]    = opt.input_path;
    report["dry_run"]  = opt.dry_run;

    // Exactly one of --dry-run / --out is required (§6.5 "Output / safety").
    if (opt.dry_run == !opt.out_path.empty()) {
        report["ok"] = false;
        report["error"] = { {"code", "InvalidTarget"},
                             {"message", opt.dry_run
                                 ? "--dry-run and --out are mutually exclusive"
                                 : "exactly one of --dry-run or --out is required"} };
        std::cerr << "orcgraffiti: exactly one of --dry-run or --out is required\n";
        return write_report_and_exit(report, opt.report_path, 1);
    }

    namespace fs = boost::filesystem;
    if (!opt.dry_run) {
        // §11.1: refuse overwriting the caller's only copy by accident.
        bool same_path = false;
        try {
            same_path = fs::exists(opt.input_path)
                && fs::exists(opt.out_path)
                && fs::equivalent(opt.input_path, opt.out_path);
        } catch (const std::exception&) { /* one side missing yet — not equivalent */ }
        if ((same_path || opt.input_path == opt.out_path) && !opt.allow_in_place) {
            report["ok"] = false;
            report["error"] = { {"code", "InvalidTarget"},
                                 {"message", "--out equals input path; pass --allow-in-place to overwrite in place"} };
            return write_report_and_exit(report, opt.report_path, 1);
        }
        if (fs::exists(opt.out_path) && !opt.force) {
            report["ok"] = false;
            report["error"] = { {"code", "InvalidTarget"},
                                 {"message", "--out already exists; pass --force to overwrite"} };
            return write_report_and_exit(report, opt.report_path, 1);
        }
    }

    Model model;
    DynamicPrintConfig config;
    ConfigSubstitutionContext config_substitutions(ForwardCompatibilitySubstitutionRule::Enable);
    PlateDataPtrs plate_data;
    std::vector<Preset*> project_presets;
    try {
        if (opt.dry_run) {
            model = load_model_for_cli(opt.input_path);
        } else {
            // Write mode needs config/plate/preset context to write via
            // store_bbs_3mf (BBS-native writer — see AI_STATUS.md AS-3 notes).
            model = Model::read_from_file(opt.input_path, &config, &config_substitutions,
                                           full_load_strategy(), &plate_data, &project_presets);
        }
    } catch (const std::exception& ex) {
        report["ok"] = false;
        report["error"] = { {"code", "ModelLoadFailed"}, {"message", ex.what()} };
        std::cerr << "orcgraffiti: failed to load '" << opt.input_path << "': " << ex.what() << "\n";
        return write_report_and_exit(report, opt.report_path, 2);
    }

    if (opt.object_index >= model.objects.size()) {
        report["ok"] = false;
        report["error"] = { {"code", "InvalidTarget"}, {"message", "object index out of range"} };
        return write_report_and_exit(report, opt.report_path, 1);
    }
    ModelObject* obj = model.objects[opt.object_index];
    if (opt.volume_index >= obj->volumes.size()) {
        report["ok"] = false;
        report["error"] = { {"code", "InvalidTarget"}, {"message", "volume index out of range"} };
        return write_report_and_exit(report, opt.report_path, 1);
    }
    ModelVolume* vol = obj->volumes[opt.volume_index];
    report["selection"] = { {"object_index", opt.object_index}, {"volume_index", opt.volume_index} };

    const auto preset = view_preset(opt.view);
    if (!preset) {
        report["ok"] = false;
        report["error"] = { {"code", "InvalidProjection"}, {"message", "unknown --view preset: " + opt.view} };
        return write_report_and_exit(report, opt.report_path, 1);
    }

    std::vector<ImagePaint::FilamentColor> filaments;
    try {
        filaments = load_filaments(opt.filaments_path);
    } catch (const std::exception& ex) {
        report["ok"] = false;
        report["error"] = { {"code", "NoAvailableFilaments"}, {"message", ex.what()} };
        std::cerr << "orcgraffiti: " << ex.what() << "\n";
        return write_report_and_exit(report, opt.report_path, 1);
    }

    const ImagePaint::ImageDecodeLimits limits;
    auto decoded = ImagePaint::decode_image(opt.image_path, limits);
    if (!decoded.has_value()) {
        report["ok"] = false;
        report["error"] = { {"code", error_code_name(decoded.error().code)},
                             {"message", decoded.error().user_message} };
        std::cerr << "orcgraffiti: image decode failed: " << decoded.error().user_message << "\n";
        return write_report_and_exit(report, opt.report_path, 2);
    }
    report["image"] = {
        {"path", opt.image_path},
        {"width_px", decoded->width},
        {"height_px", decoded->height},
        {"aspect_w_over_h", decoded->height > 0
            ? static_cast<double>(decoded->width) / decoded->height : 0.0}
    };

    const TriangleMesh& mesh = vol->mesh();
    const indexed_triangle_set& its = mesh.its;

    ImagePaint::ImagePaintRequest req;
    req.vertices.assign(its.vertices.begin(), its.vertices.end());
    req.indices.assign(its.indices.begin(), its.indices.end());
    req.filaments        = filaments;
    req.quantization.target_colors = opt.colors;
    req.quality           = quality_from_string(opt.quality);
    req.merge_policy       = merge_from_string(opt.merge);
    req.cleanup.enabled    = true;

    const double aspect = decoded->height > 0
        ? static_cast<double>(decoded->width) / decoded->height : 0.0;
    auto fitted = ImagePaint::fit_planar_projection(
        ImagePaint::Span<const Vec3f>(req.vertices.data(), req.vertices.size()),
        preset->first, preset->second, aspect, 1.02);
    if (!fitted.has_value()) {
        report["ok"] = false;
        report["error"] = { {"code", error_code_name(fitted.error().code)},
                             {"message", fitted.error().user_message} };
        return write_report_and_exit(report, opt.report_path, 2);
    }
    req.projection = *fitted;
    req.projection.front_face_cosine_threshold = 0.05;
    req.projection.minimum_coverage            = 0.25;

    report["projection"] = {
        {"space", "mesh-local"},
        {"auto_fit", true},
        {"look", {preset->first.x(), preset->first.y(), preset->first.z()}},
        {"up",   {preset->second.x(), preset->second.y(), preset->second.z()}},
        {"width_mm", req.projection.width_mm},
        {"height_mm", req.projection.height_mm},
        {"front_face_cosine_threshold", req.projection.front_face_cosine_threshold},
        {"minimum_coverage", req.projection.minimum_coverage}
    };
    report["fingerprint"] = fingerprint_json(its);

    const auto result = ImagePaint::run_image_paint(req, *decoded);
    if (!result.has_value()) {
        report["ok"] = false;
        report["error"] = { {"code", error_code_name(result.error().code)},
                             {"message", result.error().user_message},
                             {"detail", result.error().technical_detail} };
        std::cerr << "orcgraffiti: paint pipeline failed: " << result.error().user_message << "\n";
        return write_report_and_exit(report, opt.report_path, 2);
    }

    report["diagnostics"] = diagnostics_json(result->diagnostics);
    report["matches"]     = matches_json(result->matches);

    if (opt.dry_run) {
        report["ok"]    = true;
        report["error"] = nullptr;
        return write_report_and_exit(report, opt.report_path, 0);
    }

    // AS-3 apply: reuse the ImagePaintJob::finalize pattern (deserialize existing
    // facets first so paint outside the image footprint survives, then overlay
    // only the faces the projector touched) — see src/slic3r/GUI/Jobs/ImagePaintJob.cpp.
    TriangleSelector selector(vol->mesh());
    selector.deserialize(vol->mmu_segmentation_facets.get_data(),
                          /*needs_reset=*/true,
                          EnforcerBlockerType::ExtruderMax);
    for (std::size_t i = 0; i < result->states.size(); ++i) {
        if (result->states[i] != ImagePaint::kStateNone)
            selector.set_facet(static_cast<int>(i),
                                static_cast<EnforcerBlockerType>(result->states[i]));
    }
    vol->mmu_segmentation_facets.set(selector);

    // Non-3MF inputs (STL/OBJ) carry no BBS plate structure — synthesize a
    // single default plate covering every object/instance so load_bbs_3mf
    // has a <plate> to read on reopen (see AI_STATUS.md AS-3 notes, "Bug A").
    if (plate_data.empty()) {
        std::set<std::pair<int, int>> obj_inst;
        for (int oi = 0; oi < static_cast<int>(model.objects.size()); ++oi)
            for (int ii = 0; ii < static_cast<int>(model.objects[oi]->instances.size()); ++ii)
                obj_inst.insert({oi, ii});
        plate_data.push_back(new PlateData(0, obj_inst, /*lock_state=*/false));
    }

    // The exporter only writes an <assemble_item> for instances with an
    // initialized assemble transform (bbs_3mf.cpp:8186) — without it, a
    // reference file saved by this fork's own GUI shows the assemble block
    // populated but ours didn't. Initialize it from the instance's own
    // transformation so a freshly-loaded model matches that shape.
    for (ModelObject* o : model.objects)
        for (ModelInstance* inst : o->instances)
            if (!inst->is_assemble_initialized())
                inst->set_assemble_transformation(inst->get_transformation());

    // _add_relationships_file_to_archive (bbs_3mf.cpp) unconditionally writes
    // _rels/.rels entries pointing at "Metadata/plate_1.png" and
    // "Metadata/plate_1_small.png" whenever no thumbnail_data is supplied —
    // it does NOT skip the relationship when there's no file to back it.
    // Without an actual thumbnail, the archive ends up with dangling
    // relationship targets, which crashed real slicer software when a human
    // opened the file (OrcaSlicer 2.4.2 and FlashForge Studio both crashed
    // on this session's first BBS-write attempt). Supply a minimal valid
    // placeholder (16x16 white) so _add_thumbnail_file_to_archive actually
    // writes the files the relationships reference.
    ThumbnailData thumbnail;
    thumbnail.set(16, 16);

    StoreParams store_params;
    store_params.path            = opt.out_path;
    store_params.model           = &model;
    store_params.plate_data_list = plate_data;
    store_params.project_presets = project_presets;
    store_params.config          = &config;
    store_params.thumbnail_data  = { &thumbnail };
    store_params.strategy        = SaveStrategy::Zip64 | SaveStrategy::UseLoadedId;

    const bool stored = store_bbs_3mf(store_params);
    release_PlateData_list(plate_data);

    if (!stored) {
        report["ok"] = false;
        report["error"] = { {"code", "ApplyFailed"}, {"message", "store_bbs_3mf failed to write " + opt.out_path} };
        std::cerr << "orcgraffiti: failed to write '" << opt.out_path << "'\n";
        return write_report_and_exit(report, opt.report_path, 3); // I/O error
    }

    report["ok"]     = true;
    report["output"] = opt.out_path;
    report["output_format"] = "bbs-3mf";
    report["error"]  = nullptr;
    std::cerr << "orcgraffiti: wrote " << opt.out_path
               << " — manual GUI reopen check recommended (AS-3 exit gate)\n";

    return write_report_and_exit(report, opt.report_path, 0);
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty()) {
        print_help();
        return 1;
    }

    const std::string command = args[0];

    if (command == "version") {
        print_version();
        return 0;
    }
    if (command == "help" || command == "--help" || command == "-h") {
        print_help();
        return 0;
    }
    if (command == "info") {
        if (args.size() < 2) {
            std::cerr << "orcgraffiti: 'info' requires an input path\n";
            print_help();
            return 1;
        }
        std::string input_path;
        std::string report_path;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--report" && i + 1 < args.size()) {
                report_path = args[++i];
            } else if (input_path.empty()) {
                input_path = args[i];
            }
        }
        if (input_path.empty()) {
            std::cerr << "orcgraffiti: 'info' requires an input path\n";
            return 1;
        }
        return cmd_info(input_path, report_path);
    }
    if (command == "paint") {
        if (args.size() < 2) {
            std::cerr << "orcgraffiti: 'paint' requires an input path\n";
            print_help();
            return 1;
        }
        PaintOptions opt;
        for (std::size_t i = 1; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (a == "--report" && i + 1 < args.size())        opt.report_path     = args[++i];
            else if (a == "--image" && i + 1 < args.size())    opt.image_path      = args[++i];
            else if (a == "--filaments" && i + 1 < args.size()) opt.filaments_path = args[++i];
            else if (a == "--object" && i + 1 < args.size())   opt.object_index    = std::stoul(args[++i]);
            else if (a == "--volume" && i + 1 < args.size())   opt.volume_index    = std::stoul(args[++i]);
            else if (a == "--view" && i + 1 < args.size())     opt.view            = args[++i];
            else if (a == "--colors" && i + 1 < args.size())   opt.colors          = static_cast<std::uint32_t>(std::stoul(args[++i]));
            else if (a == "--quality" && i + 1 < args.size())  opt.quality         = args[++i];
            else if (a == "--merge" && i + 1 < args.size())    opt.merge           = args[++i];
            else if (a == "--out" && i + 1 < args.size())      opt.out_path        = args[++i];
            else if (a == "--dry-run")                         opt.dry_run         = true;
            else if (a == "--force")                           opt.force           = true;
            else if (a == "--allow-in-place")                  opt.allow_in_place  = true;
            else if (opt.input_path.empty())                   opt.input_path      = a;
        }
        if (opt.input_path.empty()) {
            std::cerr << "orcgraffiti: 'paint' requires an input path\n";
            return 1;
        }
        if (opt.image_path.empty()) {
            std::cerr << "orcgraffiti: 'paint' requires --image\n";
            return 1;
        }
        if (opt.filaments_path.empty()) {
            std::cerr << "orcgraffiti: 'paint' requires --filaments\n";
            return 1;
        }
        return cmd_paint(opt);
    }

    std::cerr << "orcgraffiti: unknown command '" << command << "'\n";
    print_help();
    return 1;
}
