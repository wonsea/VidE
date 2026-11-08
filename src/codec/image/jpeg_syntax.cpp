#include "codec/image/jpeg_syntax.h"

#include <cstdio>
#include <string>

namespace vide {
namespace {

std::string num(unsigned value) { return std::to_string(value); }

const char* markerName(uint8_t marker) {
    switch (marker) {
        case 0xC0: return "SOF0 (baseline DCT)";
        case 0xC1: return "SOF1 (extended sequential)";
        case 0xC2: return "SOF2 (progressive)";
        case 0xC3: return "SOF3 (lossless)";
        case 0xC4: return "DHT (huffman table)";
        case 0xC9: return "SOF9 (arithmetic)";
        case 0xCA: return "SOF10 (progressive arithmetic)";
        case 0xCC: return "DAC (arithmetic conditioning)";
        case 0xD8: return "SOI (start of image)";
        case 0xD9: return "EOI (end of image)";
        case 0xDA: return "SOS (start of scan)";
        case 0xDB: return "DQT (quantisation table)";
        case 0xDD: return "DRI (restart interval)";
        case 0xE0: return "APP0 (JFIF)";
        case 0xE1: return "APP1 (Exif/XMP)";
        case 0xE2: return "APP2 (ICC)";
        case 0xEE: return "APP14 (Adobe)";
        case 0xFE: return "COM (comment)";
        default: break;
    }
    if (marker >= 0xE0 && marker <= 0xEF) {
        return "APPn";
    }
    return "marker";
}

std::string sofComponentInfo(const uint8_t* segment, std::size_t length) {
    // segment points at the first byte after the length field.
    if (length < 6) {
        return "";
    }
    const int precision = segment[0];
    const int height = (segment[1] << 8) | segment[2];
    const int width = (segment[3] << 8) | segment[4];
    const int components = segment[5];
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "precision=%d %dx%d components=%d", precision, width,
                  height, components);
    return buffer;
}

}  // namespace

void parseJpegSyntax(const uint8_t* data, std::size_t size, SyntaxBuilder& builder,
                     uint32_t parent) {
    if (data == nullptr || size < 2 || data[0] != 0xFF || data[1] != 0xD8) {
        return;
    }

    std::size_t offset = 0;
    while (offset + 1 < size) {
        if (data[offset] != 0xFF) {
            ++offset;
            continue;
        }
        const uint8_t marker = data[offset + 1];
        if (marker == 0xFF) {  // fill byte
            ++offset;
            continue;
        }
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            // Standalone markers: no length field.
            builder.addField(parent, "marker", std::string(markerName(marker)) + " (0xFF" +
                                                               num(marker) + ")",
                             static_cast<int64_t>(offset) * 8, 16);
            offset += 2;
            if (marker == 0xD8) {
                continue;
            }
            continue;
        }

        if (offset + 3 >= size) {
            break;
        }
        const std::size_t seg_len = (static_cast<std::size_t>(data[offset + 2]) << 8) |
                                    data[offset + 3];
        const int64_t seg_bit_offset = static_cast<int64_t>(offset) * 8;
        const int64_t seg_bit_length = (2 + seg_len) * 8;

        const uint32_t node = builder.addField(
            parent, "marker",
            std::string(markerName(marker)) + " (0xFF" + num(marker) + ")",
            seg_bit_offset, seg_bit_length);

        const std::size_t payload = offset + 4;
        const std::size_t payload_len = seg_len >= 2 ? seg_len - 2 : 0;

        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 &&
            marker != 0xCC) {
            builder.addField(node, "frame_info", sofComponentInfo(data + payload, payload_len),
                             -1, -1);
        } else if (marker == 0xDB) {
            builder.addField(node, "quantisation_table_present", "1", -1, -1);
        } else if (marker == 0xC4) {
            builder.addField(node, "huffman_tables_present", "1", -1, -1);
        } else if (marker >= 0xE0 && marker <= 0xEF && payload_len >= 5 &&
                   data[payload] == 'J' && data[payload + 1] == 'F') {
            builder.addField(node, "identifier", "JFIF", -1, -1);
        }

        if (marker == 0xDA) {
            // Entropy-coded scan data follows; byte-level syntax ends here.
            builder.addField(node, "scan_data", "entropy-coded (not parsed)", -1, -1,
                             SyntaxKind::Reserved);
            break;
        }

        offset += 2 + seg_len;
    }

    builder.addField(parent, "byte_size", num(static_cast<unsigned>(size)), 0, -1,
                     SyntaxKind::Derived);
}

}  // namespace vide
