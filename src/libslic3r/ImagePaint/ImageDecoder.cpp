#include "ImageDecoder.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <jpeglib.h>
#include <jerror.h>

#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace Slic3r::ImagePaint {

namespace {

bool is_jpeg(const std::filesystem::path& p)
{
    auto ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".jpg" || ext == ".jpeg";
}

// libjpeg error manager that longjmps out of fatal errors (no exit()).
struct JpegErrorMgr {
    jpeg_error_mgr pub;
    jmp_buf        setjmp_buffer;
    char           message[JMSG_LENGTH_MAX] = {};
};

void jpeg_error_exit(j_common_ptr cinfo)
{
    auto* err = reinterpret_cast<JpegErrorMgr*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, err->message);
    longjmp(err->setjmp_buffer, 1);
}

// Decode JPEG via libjpeg-turbo. OpenCV in this tree is built WITH_JPEG=OFF,
// so cv::imread cannot load .jpg — fall back here. Already linked (JPEG::JPEG).
Expected<DecodedImage, ImagePaintError>
decode_jpeg_libjpeg(const std::filesystem::path& path,
                    const ImageDecodeLimits&     limits)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageOpenFailed,
            "Could not open image file.",
            path.string()});

    std::vector<unsigned char> file_bytes(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    if (file_bytes.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageOpenFailed,
            "Image file is empty.",
            path.string()});

    jpeg_decompress_struct cinfo;
    JpegErrorMgr           jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageDecodeFailed,
            std::string("JPEG decode failed: ") + jerr.message,
            path.string()});
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, file_bytes.data(),
                 static_cast<unsigned long>(file_bytes.size()));

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageDecodeFailed,
            "Invalid JPEG header.",
            path.string()});
    }

    // Request RGB output (3 channels).
    cinfo.out_color_space = JCS_RGB;

    const uint32_t w = static_cast<uint32_t>(cinfo.image_width);
    const uint32_t h = static_cast<uint32_t>(cinfo.image_height);

    if (w == 0 || h == 0) {
        jpeg_destroy_decompress(&cinfo);
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageDecodeFailed,
            "Image has zero dimensions.",
            path.string()});
    }
    if (w > limits.max_width || h > limits.max_height) {
        jpeg_destroy_decompress(&cinfo);
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageTooLarge,
            "Image dimensions exceed the configured limit.",
            "w=" + std::to_string(w) + " h=" + std::to_string(h)});
    }
    const uint64_t pixels = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
    if (pixels > limits.max_pixels) {
        jpeg_destroy_decompress(&cinfo);
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageTooLarge,
            "Image has too many pixels.",
            std::to_string(pixels) + " pixels"});
    }
    if (pixels * 4ULL > limits.max_decoded_bytes) {
        jpeg_destroy_decompress(&cinfo);
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageTooLarge,
            "Decoded image would exceed the memory limit.",
            std::to_string(pixels * 4ULL) + " bytes"});
    }

    jpeg_start_decompress(&cinfo);

    DecodedImage img;
    img.width     = static_cast<int>(w);
    img.height    = static_cast<int>(h);
    img.has_alpha = false;
    img.rgba.resize(static_cast<std::size_t>(pixels) * 4);
    img.source_name = path.filename().string();

    const int row_stride = static_cast<int>(w) * cinfo.output_components;
    std::vector<unsigned char> row(static_cast<std::size_t>(row_stride));
    unsigned char* row_ptr = row.data();

    std::size_t dst = 0;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, &row_ptr, 1);
        if (cinfo.output_components == 3) {
            for (uint32_t x = 0; x < w; ++x) {
                img.rgba[dst++] = row[x * 3 + 0];
                img.rgba[dst++] = row[x * 3 + 1];
                img.rgba[dst++] = row[x * 3 + 2];
                img.rgba[dst++] = 255;
            }
        } else if (cinfo.output_components == 1) {
            for (uint32_t x = 0; x < w; ++x) {
                const unsigned char g = row[x];
                img.rgba[dst++] = g;
                img.rgba[dst++] = g;
                img.rgba[dst++] = g;
                img.rgba[dst++] = 255;
            }
        } else {
            jpeg_destroy_decompress(&cinfo);
            return make_unexpected(ImagePaintError{
                ImagePaintErrorCode::ImageDecodeFailed,
                "Unsupported JPEG channel count.",
                "components=" + std::to_string(cinfo.output_components)});
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return img;
}

Expected<DecodedImage, ImagePaintError>
decode_via_opencv(const std::filesystem::path& path,
                  const ImageDecodeLimits&     limits)
{
    // IMREAD_UNCHANGED preserves alpha when present (PNG).
    // Note: this OpenCV build has WITH_JPEG=OFF — JPEG returns empty.
    cv::Mat raw = cv::imread(path.string(), cv::IMREAD_UNCHANGED);
    if (raw.empty())
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageOpenFailed,
            "Could not open or decode image.",
            path.string()});

    const uint32_t w = static_cast<uint32_t>(raw.cols);
    const uint32_t h = static_cast<uint32_t>(raw.rows);

    if (w == 0 || h == 0)
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageDecodeFailed,
            "Image has zero dimensions.",
            path.string()});

    if (w > limits.max_width || h > limits.max_height)
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageTooLarge,
            "Image dimensions exceed the configured limit.",
            "w=" + std::to_string(w) + " h=" + std::to_string(h)});

    const uint64_t pixels = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
    if (pixels > limits.max_pixels)
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageTooLarge,
            "Image has too many pixels.",
            std::to_string(pixels) + " pixels"});

    const uint64_t decoded_bytes = pixels * 4ULL;
    if (decoded_bytes > limits.max_decoded_bytes)
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageTooLarge,
            "Decoded image would exceed the memory limit.",
            std::to_string(decoded_bytes) + " bytes"});

    if (raw.depth() != CV_8U)
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageDecodeFailed,
            "Only 8-bit per channel images are supported.",
            "depth=" + std::to_string(raw.depth())});

    cv::Mat rgba;
    const int channels = raw.channels();
    switch (channels) {
    case 1: cv::cvtColor(raw, rgba, cv::COLOR_GRAY2RGBA); break;
    case 3: cv::cvtColor(raw, rgba, cv::COLOR_BGR2RGBA);  break;
    case 4: cv::cvtColor(raw, rgba, cv::COLOR_BGRA2RGBA); break;
    default:
        return make_unexpected(ImagePaintError{
            ImagePaintErrorCode::ImageDecodeFailed,
            "Unsupported number of image channels.",
            "channels=" + std::to_string(channels)});
    }

    assert(rgba.type() == CV_8UC4);
    assert(rgba.isContinuous());

    DecodedImage img;
    img.width  = static_cast<int>(w);
    img.height = static_cast<int>(h);
    img.rgba.assign(rgba.datastart, rgba.dataend);
    img.has_alpha   = (channels == 4);
    img.source_name = path.filename().string();
    return img;
}

} // namespace

Expected<DecodedImage, ImagePaintError>
decode_image(const std::filesystem::path& path,
             const ImageDecodeLimits&     limits)
{
    // Prefer OpenCV for PNG/BMP/etc. For JPEG (OpenCV built without libjpeg),
    // go straight to libjpeg so we do not fail after an empty imread.
    if (is_jpeg(path)) {
        auto jpg = decode_jpeg_libjpeg(path, limits);
        if (jpg)
            return jpg;
        // Fall through: try OpenCV in case a future build enables JPEG.
        auto cv = decode_via_opencv(path, limits);
        if (cv)
            return cv;
        return make_unexpected(jpg.error());
    }

    auto cv = decode_via_opencv(path, limits);
    if (cv)
        return cv;

    // Some systems report .jpe without is_jpeg — last resort.
    auto jpg = decode_jpeg_libjpeg(path, limits);
    if (jpg)
        return jpg;

    return make_unexpected(cv.error());
}

} // namespace Slic3r::ImagePaint
