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
#include "libslic3r_version.h"
#include "libslic3r/ImagePaint/TopologyFingerprint.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>

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
        "\n"
        "Options:\n"
        "  --report <path.json> Write machine-readable report to a file\n"
        "\n"
        "See docs/OrcGraffiti/Agent_Surface.md for the full CLI contract.\n";
}

void print_version()
{
    std::cout << "orcgraffiti " << SLIC3R_VERSION
               << " (agent-surface AS-1, ImagePaint core)\n";
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

// AS-1 exit gate: "info" JSON per Agent_Surface.md §6.4 on a fixture input.
int cmd_info(const std::string& input_path, const std::string& report_path)
{
    json report;
    report["command"] = "info";
    report["input"]   = input_path;

    Model model;
    try {
        model = Model::read_from_file(input_path);
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

    std::cerr << "orcgraffiti: unknown command '" << command << "'\n";
    print_help();
    return 1;
}
