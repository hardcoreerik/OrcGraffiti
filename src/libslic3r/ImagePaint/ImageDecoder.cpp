#include "ImageDecoder.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cassert>
#include <string>

namespace Slic3r::ImagePaint {

namespace {

bool is_jpeg(const std::filesystem::path& p)
{
    auto ext = p.extension().string();
    // Case-insensitive comparison
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".jpg" || ext == ".jpeg";
}

} // namespace

std::expected<DecodedImage, ImagePaintError>
decode_image(const std::filesystem::path& path,
             const ImageDecodeLimits&     limits)
{
    // 1. Read header only to get dimensions before full decode.
    // IMREAD_UNCHANGED preserves alpha channel if present.
    cv::Mat raw = cv::imread(path.string(), cv::IMREAD_UNCHANGED);
    if (raw.empty())
        return std::unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageOpenFailed,
            "Could not open or decode image.",
            path.string()});

    // 2. Validate dimensions before any allocation.
    const uint32_t w = static_cast<uint32_t>(raw.cols);
    const uint32_t h = static_cast<uint32_t>(raw.rows);

    if (w == 0 || h == 0)
        return std::unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageDecodeFailed,
            "Image has zero dimensions.",
            path.string()});

    if (w > limits.max_width || h > limits.max_height)
        return std::unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageTooLarge,
            "Image dimensions exceed the configured limit.",
            "w=" + std::to_string(w) + " h=" + std::to_string(h)});

    // 3. Overflow-safe pixel and byte count check.
    const uint64_t pixels = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
    if (pixels > limits.max_pixels)
        return std::unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageTooLarge,
            "Image has too many pixels.",
            std::to_string(pixels) + " pixels"});

    const uint64_t decoded_bytes = pixels * 4ULL;
    if (decoded_bytes > limits.max_decoded_bytes)
        return std::unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageTooLarge,
            "Decoded image would exceed the memory limit.",
            std::to_string(decoded_bytes) + " bytes"});

    // 4. Normalize to RGBA8.
    cv::Mat rgba;
    const int channels = raw.channels();

    if (raw.depth() != CV_8U) {
        // Only 8-bit supported for MVP.
        return std::unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageDecodeFailed,
            "Only 8-bit per channel images are supported.",
            "depth=" + std::to_string(raw.depth())});
    }

    switch (channels) {
    case 1: // GRAY
        cv::cvtColor(raw, rgba, cv::COLOR_GRAY2RGBA);
        break;
    case 3: // BGR
        cv::cvtColor(raw, rgba, cv::COLOR_BGR2RGBA);
        break;
    case 4: // BGRA
        cv::cvtColor(raw, rgba, cv::COLOR_BGRA2RGBA);
        break;
    default:
        return std::unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageDecodeFailed,
            "Unsupported number of image channels.",
            "channels=" + std::to_string(channels)});
    }

    assert(rgba.type() == CV_8UC4);
    assert(rgba.isContinuous());

    // 5. Copy to owned vector.
    DecodedImage img;
    img.width  = static_cast<int>(w);
    img.height = static_cast<int>(h);
    img.rgba.assign(rgba.datastart, rgba.dataend);

    // JPEG files have no meaningful alpha regardless of channel data.
    const bool jpeg = is_jpeg(path);
    img.has_alpha = !jpeg && (channels == 4);
    if (jpeg) {
        // Force all alpha bytes to 255 for JPEG.
        for (std::size_t i = 3; i < img.rgba.size(); i += 4)
            img.rgba[i] = 255;
    }

    img.source_name = path.filename().string();
    return img;
}

} // namespace Slic3r::ImagePaint
