#include "common/helper_common.h"

#include <cstdio>

namespace vide {

void helperEmit(const HelperMessage& message) {
    const std::vector<uint8_t> bytes = encodeHelperMessage(message);
    const uint32_t length = static_cast<uint32_t>(bytes.size());
    // IPC travels on stderr; reference software logs go to stdout (ignored).
    std::fwrite(&length, sizeof(length), 1, stderr);
    if (length > 0) {
        std::fwrite(bytes.data(), 1, bytes.size(), stderr);
    }
    std::fflush(stderr);
}

void helperEmitHello(CodecId codec, int width, int height, BitDepth bit_depth) {
    HelperMessage message;
    message.type = HelperMessageType::Hello;
    message.codec = static_cast<int>(codec);
    message.width = width;
    message.height = height;
    message.bit_depth = static_cast<int>(bit_depth);
    helperEmit(message);
}

void helperEmitProgress(uint32_t frames_ready) {
    HelperMessage message;
    message.type = HelperMessageType::Progress;
    message.frame = frames_ready;
    helperEmit(message);
}

void helperEmitReady(uint32_t frame) {
    HelperMessage message;
    message.type = HelperMessageType::Ready;
    message.frame = frame;
    helperEmit(message);
}

void helperEmitDone() {
    HelperMessage message;
    message.type = HelperMessageType::Done;
    helperEmit(message);
}

void helperEmitError(const std::string& text) {
    HelperMessage message;
    message.type = HelperMessageType::Error;
    message.text = text;
    helperEmit(message);
}

DecodedImage makeRgbaImage(int width, int height, std::vector<uint8_t>&& rgba) {
    DecodedImage image;
    image.width = width;
    image.height = height;
    image.format = PixelFormat::RGBA8888;
    image.bit_depth = BitDepth::B8;
    image.data = std::move(rgba);
    return image;
}

}  // namespace vide
