// Decoded picture, block-level analysis and thumbnail models.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/types.h"

namespace vide {

// A decoded picture in a tightly packed buffer. Row alignment is handled by the
// renderer, never stored here.
struct DecodedImage {
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::Unknown;
    BitDepth bit_depth = BitDepth::B8;
    std::vector<uint8_t> data;

    bool valid() const { return width > 0 && height > 0 && !data.empty(); }
    void clear() {
        width = height = 0;
        format = PixelFormat::Unknown;
        data.clear();
        data.shrink_to_fit();
    }
};

enum class BlockType {
    Intra = 0,
    Inter,
    Skip,
    PCM,
    Unknown,
};

enum class PredictionMode {
    DC = 0,
    Planar,
    Angular,
    Palette,
    IntraBC,
    Unknown,
};

// How a block's QP value was obtained (req 7):
//   Coded     - explicitly signalled for this block (cu_qp_delta / mb qp).
//   Inherited - not coded here; taken from the segment / slice / frame QP
//               (e.g. skip blocks, PCM without explicit qp).
//   Na        - no meaningful QP (e.g. intra-with-no-residual, MV-fallback blocks).
enum class QpSource : uint8_t {
    Na = 0,
    Coded,
    Inherited,
};

inline const char* toString(QpSource source) {
    switch (source) {
        case QpSource::Coded: return "coded";
        case QpSource::Inherited: return "inherited";
        case QpSource::Na: break;
    }
    return "n/a";
}

// Codec-agnostic coding block. `rect` expresses the spatial position and size,
// the remaining fields describe the coding decisions.
struct Block {
    uint32_t id = kInvalidId;
    Rect rect{};
    BlockType type = BlockType::Unknown;
    PredictionMode pred_mode = PredictionMode::Unknown;
    int qp = -1;
    QpSource qp_source = QpSource::Na;   // how `qp` was obtained (req 7)
    uint32_t bits = 0;                   // 0 when the adapter cannot compute it
    // Bit range of this block in the elementary stream, when the reference
    // helper can report it (0/0 = unknown). `bit_offset` is relative to the
    // owning slice-data substream; `bit_length` is the coded length in bits.
    uint32_t bit_offset = 0;
    uint32_t bit_length = 0;
    // Global VCL slice ordinal within the stream (AVC multi-slice pairing); the
    // block belongs to the `slice_index`-th VCL NAL. 0 when not applicable.
    uint32_t slice_index = 0;
    int8_t ref_idx[2] = {-1, -1};
    int16_t mv[2][2] = {{0, 0}, {0, 0}}; // [list][x/y], quarter-pel
    int depth = 0;                       // partitioning depth
    int16_t transform_size = -1;         // TU size, -1 when unknown
    uint32_t syntax_node = kInvalidId;   // linked SyntaxNode, when available
};

// Flat list of blocks for one picture. Hierarchy is expressed via `depth` and
// nesting rectangles rather than a linked structure, which keeps traversal and
// rendering simple.
struct BlockGrid {
    int ctu_size = 0;
    int width = 0;
    int height = 0;
    std::vector<Block> blocks;

    bool empty() const { return blocks.empty(); }
};

enum class FrameType : uint8_t {
    Unknown = 0,
    I = 1,
    P = 2,
    B = 3,
    Key = 4,  // IDR / key frame that also decodes as intra
};

inline const char* toString(FrameType type) {
    switch (type) {
        case FrameType::I: return "I";
        case FrameType::P: return "P";
        case FrameType::B: return "B";
        case FrameType::Key: return "IDR";
        case FrameType::Unknown: break;
    }
    return "?";
}

struct FrameMeta {
    uint32_t index = 0;                  // coding (decode) order
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::Unknown;
    BitDepth bit_depth = BitDepth::B8;
    CodecId codec = CodecId::Unknown;
    bool is_keyframe = false;
    FrameType frame_type = FrameType::Unknown;
    int64_t poc = 0;                     // picture order count (display order)
    int64_t file_offset = -1;            // byte offset in the bitstream
    int64_t size_bytes = 0;
};

inline const char* toString(BlockType type) {
    switch (type) {
        case BlockType::Intra: return "Intra";
        case BlockType::Inter: return "Inter";
        case BlockType::Skip: return "Skip";
        case BlockType::PCM: return "PCM";
        case BlockType::Unknown: break;
    }
    return "Unknown";
}

inline const char* toString(PredictionMode mode) {
    switch (mode) {
        case PredictionMode::DC: return "DC";
        case PredictionMode::Planar: return "Planar";
        case PredictionMode::Angular: return "Angular";
        case PredictionMode::Palette: return "Palette";
        case PredictionMode::IntraBC: return "IntraBC";
        case PredictionMode::Unknown: break;
    }
    return "Unknown";
}

}  // namespace vide
