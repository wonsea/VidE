// Media source: container probing, elementary-stream extraction and thumbnail
// decoding via FFmpeg. Used by the main process; the reference helpers handle
// full decoding/syntax/block analysis.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/frame.h"
#include "common/status.h"
#include "common/types.h"

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace vide {

struct MediaInfo {
    CodecId codec = CodecId::Unknown;
    PixelFormat format = PixelFormat::Unknown;
    BitDepth bit_depth = BitDepth::B8;
    ChromaFormat chroma = ChromaFormat::C420;
    int width = 0;
    int height = 0;
    int fps_num = 0;
    int fps_den = 1;
    int64_t frame_count = -1;
    std::string container;
};

// Opens a media file and provides thumbnails + elementary-stream extraction.
class MediaSource {
public:
    MediaSource();
    ~MediaSource();

    MediaSource(const MediaSource&) = delete;
    MediaSource& operator=(const MediaSource&) = delete;

    Status open(const std::string& path);
    void close();

    const MediaInfo& info() const { return info_; }
    bool isOpen() const { return format_ != nullptr; }

    // Decodes frame `index` at full resolution as RGBA. Uses an internal
    // sequential decoder (random-access by seeking backwards when needed).
    Result<DecodedImage> decode(uint32_t index);

    // Decodes frame `index` and scales it to fit within max_width x max_height,
    // returning RGBA.
    Result<DecodedImage> thumbnail(uint32_t index, int max_width, int max_height);

    // Copy of the video stream's container extradata (e.g. AVCC/HVCC record),
    // used by the in-process header-syntax parsers.
    std::vector<uint8_t> extradata() const;

    // Fallback block-level info for codecs without a reference helper. FFmpeg's
    // export_mvs side data provides per-block motion vectors and block sizes for
    // H.264/HEVC/VP9/AV1; QP and bits are not available this way.
    Result<BlockGrid> motionVectors(uint32_t index);

    // Writes the stream's Annex-B elementary form to `out_path`. `bsf_name` is
    // the FFmpeg bitstream filter (e.g. "hevc_mp4toannexb"); some formats (AV1)
    // use "av1_metadata". Returns the written path.
    Result<std::string> extractEs(const std::string& out_path, const char* bsf_name) const;

    // Remuxes the AV1 video stream of a container into an IVF elementary stream
    // (the reference helper reads IVF/OBU). Returns the output path.
    Result<std::string> extractAv1Ivf(const std::string& out_path) const;

private:
    Result<DecodedImage> decodeFrame(uint32_t index);

    AVFormatContext* format_ = nullptr;
    AVCodecContext* codec_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    mutable SwsContext* sws_ = nullptr;
    int stream_index_ = -1;
    int64_t decoded_index_ = -1;
    bool sent_flush_ = false;
    MediaInfo info_{};
    std::string path_;
};

}  // namespace vide
