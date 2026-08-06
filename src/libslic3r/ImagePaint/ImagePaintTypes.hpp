#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace Slic3r::ImagePaint {

// Opaque index types — prevent accidental mixing.
using FaceIndex    = std::uint32_t;
using FilamentIndex = std::uint16_t;

// SelectorState maps to EnforcerBlockerType cast to uint8_t.
// 0 = NONE (unpainted). 1 = Extruder1 .. 16 = Extruder16.
// Conversion to/from EnforcerBlockerType is done only in the central utility
// filament_index_to_selector_state() in PaintStateMerge.hpp.
using SelectorState = std::uint8_t;

constexpr SelectorState kStateNone     = 0;
constexpr SelectorState kStateExtruderMin = 1;   // Extruder1
constexpr SelectorState kStateExtruderMax = 16;  // Extruder16

struct ColorRgb8 {
    std::uint8_t r = 0, g = 0, b = 0;
};

struct ColorRgba8 {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct ColorRgbf {
    float r = 0.f, g = 0.f, b = 0.f;
};

struct ColorLab {
    double l = 0.0, a = 0.0, b = 0.0;
};

// Immutable decoded image — normalized to RGBA8, (0,0) = top-left.
struct DecodedImage {
    int width  = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4 bytes
    bool has_alpha   = false;
    std::string source_name;
};

struct ImageDecodeLimits {
    std::uint32_t max_width        = 16384;
    std::uint32_t max_height       = 16384;
    std::uint64_t max_pixels       = 100'000'000ULL;
    std::uint64_t max_decoded_bytes = 400'000'000ULL;
};

// Filament entry from the active project palette.
struct FilamentColor {
    FilamentIndex project_index = 0;  // 0-based internal
    std::string   name;
    std::string   preset_id;
    ColorRgb8     display_rgb;
};

// One cluster produced by color quantization.
struct SourceCluster {
    std::uint32_t id             = 0;
    ColorRgb8     representative;
    std::uint64_t sample_count   = 0;
    double        projected_area_mm2 = 0.0;
};

// The result of matching one cluster to a filament.
struct ClusterMatch {
    std::uint32_t  cluster_id      = 0;
    FilamentIndex  filament_index  = 0;
    double         delta_e         = 0.0;
    bool           user_overridden = false;
};

// Per-face diagnostic counters returned with every FacePaintPlan.
struct PaintDiagnostics {
    std::uint64_t total_faces        = 0;
    std::uint64_t candidate_faces    = 0;
    std::uint64_t painted_faces      = 0;
    std::uint64_t transparent_faces  = 0;
    std::uint64_t back_facing_faces  = 0;
    std::uint64_t occluded_faces     = 0;
    std::uint64_t connected_components = 0;
    std::uint64_t tiny_components    = 0;
    double        painted_surface_area_mm2 = 0.0;
    bool          coarse_mesh_warning = false;
    std::vector<std::string> warnings;
};

} // namespace Slic3r::ImagePaint
