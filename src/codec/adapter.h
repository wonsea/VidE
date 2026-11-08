// Codec adapter interface.
//
// This is the only boundary between VidE and third-party codec libraries
// (FFmpeg, JM, HM, VTM, libvpx, libaom, libjpeg-turbo, libpng). Adapters convert
// library-specific state into the common models in `common/`.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/frame.h"
#include "common/status.h"
#include "common/syntax.h"
#include "common/types.h"

namespace vide {

// Codec-specific name for the top-level coding block group (terminology that
// should follow the protocol in the UI): AVC = macroblock, HEVC/VVC = CTU,
// VP9/AV1 = superblock.
inline const char* blockGroupTerm(CodecId codec) {
    switch (codec) {
        case CodecId::H264: return "MB";
        case CodecId::HEVC:
        case CodecId::VVC: return "CTU";
        case CodecId::VP9:
        case CodecId::AV1: return "SB";
        default: return "block";
    }
}

struct StreamInfo {
    CodecId codec = CodecId::Unknown;
    PixelFormat format = PixelFormat::Unknown;
    BitDepth bit_depth = BitDepth::B8;
    ChromaFormat chroma = ChromaFormat::C420;
    int width = 0;
    int height = 0;
    int fps_num = 0;
    int fps_den = 0;
    int64_t frame_count = -1;            // -1 when unknown
    std::string container;               // "raw", "mp4", "mkv", ...
};

// Advertises what an adapter can actually deliver so the UI can degrade
// gracefully instead of showing empty panels.
struct CodecCapabilities {
    bool syntax_tree = false;
    bool block_info = false;
    bool accurate_qp = false;
    bool accurate_bits = false;
    bool motion_vectors = false;
    bool thumbnail = true;
};

struct OpenOptions {
    std::string path;
    // Raw-video parameters (ignored for self-describing bitstreams).
    int raw_width = 0;
    int raw_height = 0;
    int raw_fps_num = 0;
    int raw_fps_den = 1;
    PixelFormat raw_format = PixelFormat::Unknown;
};

class ICodecAdapter {
public:
    virtual ~ICodecAdapter() = default;

    virtual CodecId codecId() const = 0;
    virtual const char* name() const = 0;

    // Cheap test used by the registry to pick an adapter for `path`.
    virtual bool probe(const std::string& path) const = 0;

    virtual Result<StreamInfo> open(const OpenOptions& options) = 0;
    virtual void close() = 0;

    virtual CodecCapabilities capabilities() const = 0;

    virtual Result<SyntaxTree> syntax() = 0;
    virtual Result<FrameMeta> frameMeta(uint32_t index) = 0;
    virtual Result<DecodedImage> decode(uint32_t index) = 0;
    virtual Result<BlockGrid> blocks(uint32_t index) = 0;

    // Optional: decode `index` and scale it down to fit inside max_width x
    // max_height. Adapters that cannot produce thumbnails may keep the default.
    virtual Result<DecodedImage> thumbnail(uint32_t index, int max_width, int max_height) {
        (void)index;
        (void)max_width;
        (void)max_height;
        return Status::error(StatusCode::Unsupported, "thumbnails not supported");
    }
};

// Process-wide list of available adapters, queried in registration order.
class CodecRegistry {
public:
    static CodecRegistry& instance() {
        static CodecRegistry registry;
        return registry;
    }

    void add(std::unique_ptr<ICodecAdapter> adapter) {
        if (adapter) {
            adapters_.push_back(std::move(adapter));
        }
    }

    ICodecAdapter* detect(const std::string& path) const {
        for (const auto& adapter : adapters_) {
            if (adapter->probe(path)) {
                return adapter.get();
            }
        }
        return nullptr;
    }

    ICodecAdapter* findByCodec(CodecId codec) const {
        for (const auto& adapter : adapters_) {
            if (adapter->codecId() == codec) {
                return adapter.get();
            }
        }
        return nullptr;
    }

    const std::vector<std::unique_ptr<ICodecAdapter>>& adapters() const {
        return adapters_;
    }

    std::size_t size() const { return adapters_.size(); }

private:
    CodecRegistry() = default;
    std::vector<std::unique_ptr<ICodecAdapter>> adapters_;
};

}  // namespace vide
