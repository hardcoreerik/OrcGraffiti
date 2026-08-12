#pragma once

#include <string>
#include <cstdint>

namespace Slic3r::ImagePaint {

enum class ImagePaintErrorCode : std::uint32_t {
    // Selection / target errors
    NoSelection,
    InvalidTarget,
    UnsupportedVolume,
    // Image errors
    ImageOpenFailed,
    ImageDecodeFailed,
    ImageTooLarge,
    // Projection / geometry errors
    InvalidProjection,
    NoEligibleFaces,
    // Color / palette errors
    NoAvailableFilaments,
    TooManyColors,
    QuantizationFailed,
    // Pipeline control
    Canceled,
    // Apply-time validation errors
    TargetDeleted,
    TopologyChanged,
    FilamentConfigurationChanged,
    FaceCountMismatch,
    FilamentOutOfRange,
    ApplyFailed,
    // Internal
    InternalInvariantViolation,
    // Bake (real-geometry) errors — see MeshBake.hpp
    BakeInvalidGeometry,
};

struct ImagePaintError {
    ImagePaintErrorCode code;
    std::string         user_message;   // localization key or plain English for now
    std::string         technical_detail;

    explicit ImagePaintError(ImagePaintErrorCode c,
                              std::string user_msg  = {},
                              std::string technical = {})
        : code(c)
        , user_message(std::move(user_msg))
        , technical_detail(std::move(technical))
    {}
};

} // namespace Slic3r::ImagePaint
