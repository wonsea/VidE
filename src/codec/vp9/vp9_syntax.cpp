#include "codec/vp9/vp9_syntax.h"

#include <cstdio>
#include <string>

#include "common/bitreader.h"

namespace vide {
namespace {

std::string num(uint32_t value) { return std::to_string(value); }

uint32_t readLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t readLe16(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8);
}

void parseFrameHeader(const uint8_t* frame, std::size_t size, SyntaxBuilder& builder,
                      uint32_t parent, uint32_t frame_index) {
    (void)frame_index;
    BitReader reader(frame, size);
    const uint32_t base = builder.addStructure(parent, "uncompressed_header");
    auto elem = [&](const char* name, int bits) -> uint32_t {
        const std::size_t start = reader.bitPosition();
        const uint32_t v = reader.readBits(bits);
        builder.addField(base, name, num(v), static_cast<int64_t>(start),
                         static_cast<int64_t>(reader.bitPosition() - start));
        return v;
    };
    auto sync = [&](const char* name) {
        const std::size_t start = reader.bitPosition();
        const uint32_t v = reader.readBits(24);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%06X", v);
        builder.addField(base, name, buf, static_cast<int64_t>(start), 24);
    };
    auto colorConfig = [&](uint32_t profile) {
        if (profile >= 2) {
            elem("ten_or_twelve_bit", 1);
        }
        const uint32_t color_space = elem("color_space", 3);
        if (color_space != 7 /*CS_SRGB*/) {
            elem("color_range", 1);
            if (profile == 1 || profile == 3) {
                elem("subsampling_x", 1);
                elem("subsampling_y", 1);
                elem("reserved_zero", 1);
            }
        } else if (profile == 1 || profile == 3) {
            elem("reserved_zero", 1);
        }
    };
    auto frameSize = [&]() {
        elem("frame_width_minus_1", 16);
        elem("frame_height_minus_1", 16);
        if (elem("render_and_frame_size_different", 1) != 0) {
            elem("render_width_minus_1", 16);
            elem("render_height_minus_1", 16);
        }
    };

    elem("frame_marker", 2);
    const uint32_t profile_low = elem("profile_low_bit", 1);
    const uint32_t profile_high = elem("profile_high_bit", 1);
    const uint32_t profile = (profile_high << 1) | profile_low;
    builder.addField(base, "profile", num(profile), -1, -1, SyntaxKind::Derived);
    if (profile == 3) {
        elem("reserved_zero", 1);
    }

    const uint32_t show_existing = elem("show_existing_frame", 1);
    if (show_existing != 0) {
        elem("frame_to_show_map_idx", 3);
        return;
    }

    const uint32_t frame_type = elem("frame_type", 1);
    builder.addField(base, "frame_type_name", frame_type ? "INTER" : "KEY_FRAME", -1, -1,
                     SyntaxKind::Derived);
    const uint32_t show_frame = elem("show_frame", 1);
    const uint32_t error_resilient = elem("error_resilient_mode", 1);

    if (frame_type == 0) {  // key frame
        sync("frame_sync_code");
        colorConfig(profile);
        // Key frames implicitly refresh every reference buffer (0xFF).
        builder.addField(base, "refresh_frame_flags", "0xFF (implicit)", -1, -1,
                         SyntaxKind::Derived);
        frameSize();
    } else {
        const uint32_t intra_only = show_frame ? 0 : elem("intra_only", 1);
        if (!error_resilient) {
            elem("reset_frame_context", 2);
        } else {
            builder.addField(base, "reset_frame_context", "0 (error resilient)", -1, -1,
                             SyntaxKind::Derived);
        }
        if (intra_only) {
            sync("frame_sync_code");
            if (profile > 0) {
                colorConfig(profile);
            } else {
                builder.addField(base, "color_space", "BT_601 4:2:0 (profile 0 default)", -1, -1,
                                 SyntaxKind::Derived);
            }
            elem("refresh_frame_flags", 8);
            frameSize();
        } else {
            elem("refresh_frame_flags", 8);
            for (int i = 0; i < 3; ++i) {
                elem(("ref_frame_idx[" + num(i) + "]").c_str(), 3);
                elem(("ref_frame_sign_bias[" + num(i) + "]").c_str(), 1);
            }
            builder.addField(base, "note", "inter-frame header not expanded", -1, -1,
                             SyntaxKind::Reserved);
            return;
        }
    }

    if (!error_resilient) {
        elem("refresh_frame_context", 1);
        elem("frame_parallel_decoding_mode", 1);
    } else {
        builder.addField(base, "refresh_frame_context", "0 (error resilient)", -1, -1,
                         SyntaxKind::Derived);
        builder.addField(base, "frame_parallel_decoding_mode", "1 (error resilient)", -1, -1,
                         SyntaxKind::Derived);
    }
    elem("frame_context_idx", 2);
    builder.addField(base, "note", "compressed header (bool-coded) not expanded", -1, -1,
                     SyntaxKind::Reserved);
}

}  // namespace

bool parseVp9Syntax(const uint8_t* data, std::size_t size, SyntaxBuilder& builder,
                    uint32_t parent) {
    if (data == nullptr || size < 32) {
        return false;
    }
    if (!(data[0] == 'D' && data[1] == 'K' && data[2] == 'I' && data[3] == 'F')) {
        return false;
    }

    const uint32_t version = readLe16(data + 4);
    const uint32_t header_size = readLe16(data + 6);
    const uint32_t fourcc = readLe32(data + 8);
    const uint32_t width = readLe16(data + 12);
    const uint32_t height = readLe16(data + 14);
    const uint32_t frame_count = readLe32(data + 24);

    const uint32_t container = builder.addStructure(parent, "ivf_header");
    builder.addField(container, "signature", "DKIF", 0, 32);
    builder.addField(container, "version", num(version), 32, 16);
    builder.addField(container, "header_size", num(header_size), 48, 16);
    {
        char code[8] = {};
        code[0] = static_cast<char>(fourcc & 0xFF);
        code[1] = static_cast<char>((fourcc >> 8) & 0xFF);
        code[2] = static_cast<char>((fourcc >> 16) & 0xFF);
        code[3] = static_cast<char>((fourcc >> 24) & 0xFF);
        const std::string fourcc_str = code[0] ? code : std::string("?");
        builder.addField(container, "fourcc", fourcc_str, 64, 32);
        if (fourcc_str == "VP90") {
            builder.addField(container, "codec", "VP9", -1, -1, SyntaxKind::Derived);
        }
    }
    builder.addField(container, "width", num(width), 96, 16);
    builder.addField(container, "height", num(height), 112, 16);
    builder.addField(container, "frame_count", num(frame_count), 192, 32);

    std::size_t offset = header_size >= 32 ? header_size : 32;
    uint32_t index = 0;
    while (offset + 12 <= size && index < 8) {
        const uint32_t frame_size = readLe32(data + offset);
        const int64_t ts = static_cast<int64_t>(readLe32(data + offset + 4)) |
                           (static_cast<int64_t>(readLe32(data + offset + 8)) << 32);
        const uint32_t entry = builder.addStructure(parent, "ivf_frame[" + num(index) + "]");
        builder.addField(entry, "frame_size", num(frame_size),
                         static_cast<int64_t>(offset) * 8, 32);
        builder.addField(entry, "timestamp", std::to_string(ts),
                         static_cast<int64_t>(offset + 4) * 8, 64);

        const std::size_t payload = offset + 12;
        if (frame_size == 0 || payload + frame_size > size) {
            break;
        }
        if (index < 3) {
            parseFrameHeader(data + payload, frame_size, builder, entry, index);
        }
        offset = payload + frame_size;
        ++index;
    }

    return true;
}

}  // namespace vide
