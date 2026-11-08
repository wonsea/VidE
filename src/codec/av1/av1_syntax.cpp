#include "codec/av1/av1_syntax.h"

#include <cstdint>
#include <string>

#include "common/bitreader.h"

#ifdef VIDE_WITH_LIBAOM
#include <aom/aom_decoder.h>
#endif

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

}  // namespace

// The libaom build in external/ predates OBU framing: each frame starts with an
// uncompressed header whose leading 2-bit marker is 0b10 (AOM_FRAME_MARKER).
// This mirrors libaom's own read_uncompressed_header() for the key-frame path.
void parseAv1Syntax(const uint8_t* data, std::size_t size, SyntaxBuilder& builder,
                    uint32_t parent) {
    if (data == nullptr || size < 8) {
        return;
    }

    const uint8_t* frame = data;
    std::size_t frame_size = size;
    if (size >= 36 && data[0] == 'D' && data[1] == 'K' && data[2] == 'I' && data[3] == 'F') {
        const uint32_t container = builder.addStructure(parent, "ivf_header");
        builder.addField(container, "signature", "DKIF", 0, 32);
        builder.addField(container, "version", num(readLe16(data + 4)), 32, 16);
        builder.addField(container, "header_size", num(readLe16(data + 6)), 48, 16);
        char code[5] = {};
        code[0] = static_cast<char>(data[8]);
        code[1] = static_cast<char>(data[9]);
        code[2] = static_cast<char>(data[10]);
        code[3] = static_cast<char>(data[11]);
        builder.addField(container, "fourcc", code[0] ? std::string(code) : std::string("?"), 64, 32);
        builder.addField(container, "width", num(readLe16(data + 12)), 96, 16);
        builder.addField(container, "height", num(readLe16(data + 14)), 112, 16);
        builder.addField(container, "frame_count", num(readLe32(data + 24)), 192, 32);

        const uint32_t fs = readLe32(data + 32);
        if (fs == 0 || 44u + fs > size) {
            return;
        }
        frame = data + 44;
        frame_size = fs;
    }

    const uint32_t node = builder.addStructure(parent, "uncompressed_header");
    BitReader r(frame, frame_size);
    auto elem = [&](const char* name, int bits) -> uint32_t {
        const std::size_t start = r.bitPosition();
        const uint32_t v = r.readBits(bits);
        builder.addField(node, name, num(v), static_cast<int64_t>(start),
                         static_cast<int64_t>(r.bitPosition() - start));
        return v;
    };

    const uint32_t marker = elem("frame_marker", 2);
    if (marker != 2) {
        // Not the pre-OBU (superframe) layout this build's libaom uses — the
        // stream is modern OBU (or another format) and cannot be parsed here.
        builder.addField(node, "note",
                         "modern OBU AV1 stream (bundled libaom is pre-OBU); "
                         "header not expanded",
                         -1, -1, SyntaxKind::Reserved);
        return;
    }
    uint32_t profile = elem("profile_low_bit", 1);
    profile |= elem("profile_high_bit", 1) << 1;
    if (profile > 2) {
        profile += elem("reserved_profile_bit", 1);
    }
    builder.addField(node, "profile", num(profile), -1, -1, SyntaxKind::Derived);

    const uint32_t show_existing = elem("show_existing_frame", 1);
    if (show_existing != 0) {
        elem("frame_to_show_map_idx", 3);
        elem("display_frame_id", 15);
        return;
    }

    const uint32_t frame_type = elem("frame_type", 1);
    builder.addField(node, "frame_type_name", frame_type ? "INTER" : "KEY_FRAME", -1, -1,
                     SyntaxKind::Derived);
    elem("show_frame", 1);
    elem("error_resilient_mode", 1);
    // FRAME_ID_NUMBERS_PRESENT_FLAG == 1 and FRAME_ID_LENGTH_MINUS7 == 8 in this build.
    elem("current_frame_id", 15);

    if (frame_type != 0) {
        builder.addField(node, "note", "inter-frame header not expanded", -1, -1,
                         SyntaxKind::Reserved);
        return;
    }

    elem("frame_sync_code_0", 8);
    elem("frame_sync_code_1", 8);
    elem("frame_sync_code_2", 8);

    if (profile >= 2) {
        elem("high_bitdepth", 1);
    }
    const uint32_t color_space = elem("color_space", 3);
    if (color_space != 7 /*AOM_CS_SRGB*/) {
        elem("color_range", 1);
        if (profile == 1 || profile == 3) {
            elem("subsampling_x", 1);
            elem("subsampling_y", 1);
            elem("reserved_zero", 1);
        }
    }

    elem("frame_width_minus_1", 16);
    elem("frame_height_minus_1", 16);
    if (elem("render_and_frame_size_different", 1)) {
        elem("render_width_minus_1", 16);
        elem("render_height_minus_1", 16);
    }
    elem("allow_screen_content_tools", 1);
}

}  // namespace vide
