#include "codec/image/png_syntax.h"

#include <cstdio>
#include <string>

namespace vide {
namespace {

const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

std::string num(unsigned value) { return std::to_string(value); }

std::string readFourCc(const uint8_t* p) {
    return std::string(reinterpret_cast<const char*>(p), 4);
}

const char* chunkMeaning(const std::string& type) {
    if (type == "IHDR") return "image header";
    if (type == "PLTE") return "palette";
    if (type == "IDAT") return "image data";
    if (type == "IEND") return "image end";
    if (type == "tRNS") return "transparency";
    if (type == "gAMA") return "gamma";
    if (type == "cHRM") return "chromaticity";
    if (type == "sRGB") return "sRGB";
    if (type == "pHYs") return "physical dimensions";
    if (type == "tEXt") return "textual data";
    if (type == "iTXt") return "international textual data";
    if (type == "zTXt") return "compressed textual data";
    if (type == "tIME") return "last modification time";
    return "ancillary";
}

}  // namespace

void parsePngSyntax(const uint8_t* data, std::size_t size, SyntaxBuilder& builder,
                    uint32_t parent) {
    if (data == nullptr || size < 8) {
        return;
    }
    for (int i = 0; i < 8; ++i) {
        if (data[i] != kSignature[i]) {
            return;
        }
    }

    builder.addField(parent, "signature", "89 50 4E 47 0D 0A 1A 0A", 0, 64);

    std::size_t offset = 8;
    while (offset + 12 <= size) {
        const uint32_t length = (static_cast<uint32_t>(data[offset]) << 24) |
                                (static_cast<uint32_t>(data[offset + 1]) << 16) |
                                (static_cast<uint32_t>(data[offset + 2]) << 8) |
                                static_cast<uint32_t>(data[offset + 3]);
        const std::string type = readFourCc(data + offset + 4);
        const int64_t bit_offset = static_cast<int64_t>(offset) * 8;

        const uint32_t node = builder.addField(
            parent, "chunk", type + " (" + chunkMeaning(type) + ")", bit_offset,
            static_cast<int64_t>(length + 12) * 8);

        builder.addField(node, "length", num(length), static_cast<int64_t>(offset) * 8, 32);
        builder.addField(node, "type", type, static_cast<int64_t>(offset + 4) * 8, 32);

        if (type == "IHDR" && length >= 13) {
            const uint8_t* p = data + offset + 8;
            const uint32_t width = (static_cast<uint32_t>(p[0]) << 24) |
                                   (static_cast<uint32_t>(p[1]) << 16) |
                                   (static_cast<uint32_t>(p[2]) << 8) | p[3];
            const uint32_t height = (static_cast<uint32_t>(p[4]) << 24) |
                                    (static_cast<uint32_t>(p[5]) << 16) |
                                    (static_cast<uint32_t>(p[6]) << 8) | p[7];
            builder.addField(node, "width", num(width), -1, -1, SyntaxKind::Derived);
            builder.addField(node, "height", num(height), -1, -1, SyntaxKind::Derived);
            builder.addField(node, "bit_depth", num(p[8]), -1, -1);
            builder.addField(node, "colour_type", num(p[9]), -1, -1);
            builder.addField(node, "compression_method", num(p[10]), -1, -1);
            builder.addField(node, "filter_method", num(p[11]), -1, -1);
            builder.addField(node, "interlace_method", num(p[12]), -1, -1);
        }

        if (length > size || offset + 12 + length > size) {
            builder.addField(node, "data", "truncated", -1, -1, SyntaxKind::Reserved);
            break;
        }

        offset += 12 + length;
        if (type == "IEND") {
            break;
        }
    }

    builder.addField(parent, "byte_size", num(static_cast<unsigned>(size)), 0, -1,
                     SyntaxKind::Derived);
}

}  // namespace vide
