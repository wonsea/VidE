// Common scalar/enum types shared by every VidE layer.
//
// This header intentionally depends on nothing but the C++ standard library and
// must not know about any third-party codec library.
#pragma once

#include <cstdint>
#include <string>

namespace vide {

// Sentinels used across the data model.
inline constexpr uint32_t kInvalidId = 0xFFFFFFFFu;

// Supported bitstream / image / raw-video formats.
enum class CodecId {
    Unknown = 0,
    H264,
    HEVC,
    VVC,
    VP9,
    AV1,
    JPEG,
    PNG,
    RawVideo,
};

// Pixel layouts. "P0xx" formats are 16-bit little-endian semi-planar with the
// sample stored in the most-significant bits.
enum class PixelFormat {
    Unknown = 0,
    I420,
    NV12,
    NV21,
    YUV422P,
    YUV400,
    YUV444P,
    P010,
    P012,
    P016,
    RGB888,
    RGBA8888,
};

enum class ChromaFormat {
    Monochrome = 0,
    C420,
    C422,
    C444,
};

enum class BitDepth : int {
    B8 = 8,
    B10 = 10,
    B12 = 12,
    B16 = 16,
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    constexpr int right() const { return x + w; }
    constexpr int bottom() const { return y + h; }
    constexpr bool empty() const { return w <= 0 || h <= 0; }
};

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
};

// Returns the number of stored bytes per luma sample for the given format.
inline int bytesPerSample(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::P010:
        case PixelFormat::P012:
        case PixelFormat::P016:
            return 2;
        default:
            return 1;
    }
}

const char* toString(CodecId codec);
const char* toString(PixelFormat format);
const char* toString(BitDepth depth);

}  // namespace vide
