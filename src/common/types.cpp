#include "common/types.h"

namespace vide {

const char* toString(CodecId codec) {
    switch (codec) {
        case CodecId::H264: return "AVC";
        case CodecId::HEVC: return "HEVC";
        case CodecId::VVC: return "VVC";
        case CodecId::VP9: return "VP9";
        case CodecId::AV1: return "AV1";
        case CodecId::JPEG: return "JPEG";
        case CodecId::PNG: return "PNG";
        case CodecId::RawVideo: return "RawVideo";
        case CodecId::Unknown: break;
    }
    return "Unknown";
}

const char* toString(PixelFormat format) {
    switch (format) {
        case PixelFormat::I420: return "I420";
        case PixelFormat::NV12: return "NV12";
        case PixelFormat::NV21: return "NV21";
        case PixelFormat::YUV422P: return "YUV422P";
        case PixelFormat::YUV400: return "YUV400";
        case PixelFormat::YUV444P: return "YUV444P";
        case PixelFormat::P010: return "P010";
        case PixelFormat::P012: return "P012";
        case PixelFormat::P016: return "P016";
        case PixelFormat::RGB888: return "RGB888";
        case PixelFormat::RGBA8888: return "RGBA8888";
        case PixelFormat::Unknown: break;
    }
    return "Unknown";
}

const char* toString(BitDepth depth) {
    switch (depth) {
        case BitDepth::B8: return "8-bit";
        case BitDepth::B10: return "10-bit";
        case BitDepth::B12: return "12-bit";
        case BitDepth::B16: return "16-bit";
    }
    return "unknown";
}

}  // namespace vide
