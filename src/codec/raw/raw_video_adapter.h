// Adapter for header-less raw video files (YUV / RGB pixel dumps).
//
// The pixel layout cannot be detected from the file itself, so the caller must
// supply the geometry via OpenOptions (raw_width/raw_height/raw_format/fps).
#pragma once

#include <cstdint>
#include <fstream>

#include "codec/adapter.h"

namespace vide {

class RawVideoAdapter final : public ICodecAdapter {
public:
    CodecId codecId() const override { return CodecId::RawVideo; }
    const char* name() const override { return "RawVideo"; }

    bool probe(const std::string& path) const override;
    Result<StreamInfo> open(const OpenOptions& options) override;
    void close() override;

    CodecCapabilities capabilities() const override;

    Result<SyntaxTree> syntax() override;
    Result<FrameMeta> frameMeta(uint32_t index) override;
    Result<DecodedImage> decode(uint32_t index) override;
    Result<BlockGrid> blocks(uint32_t index) override;
    Result<DecodedImage> thumbnail(uint32_t index, int max_width, int max_height) override;

private:
    std::ifstream file_;
    StreamInfo info_{};
    int64_t frame_size_ = 0;
    int64_t frame_count_ = -1;
    OpenOptions options_{};
};

// Returns the size in bytes of a single frame, or -1 for an unknown format.
int64_t rawFrameSize(PixelFormat format, int width, int height);

}  // namespace vide
