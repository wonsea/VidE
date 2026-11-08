// VidE Pack (.vpk): single-file, content-addressed cache produced by a helper
// process and consumed by the main process.
//
// Layout (v2):
//   Header      (fixed, 56 B)
//   FrameData   (append-only: per-frame syntax / blocks / reco (+ reserved))
//   FrameIndex  (fixed-size array, one entry per frame, rewritten on append)
//   Footer      (fixed, updated on completion)
//
// The index sits at the *end* so the file can be written before the total frame
// count is known (reference decoders do not report it up front) and so a reader
// can refresh a partial pack while the helper is still running. The index is
// read whole into memory; frame payloads are read on demand via offsets.
//
// Header.index_offset points at the current index; Header.data_offset is fixed
// at sizeof(VpkHeader). Readers must treat `frame_count` as a high-water mark:
// entries are only valid once the helper has flushed the corresponding append.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/frame.h"
#include "common/status.h"
#include "common/syntax.h"
#include "common/types.h"

namespace vide {

inline constexpr uint32_t kVpkMagic = 0x4B50564Bu;        // 'V''P''K'
inline constexpr uint32_t kVpkFooterMagic = 0x4B465056u;  // 'V''P''K''F'
inline constexpr uint32_t kVpkVersion = 7;                // v7: per-block global slice ordinal (AVC multi-slice)

// Per-frame payload section tags stored in the index.
enum class VpkSection : uint8_t {
    Syntax = 0,
    Blocks = 1,
    Reco = 2,
    Pred = 3,  // reserved
    Resi = 4,  // reserved
};

#pragma pack(push, 1)
struct VpkHeader {
    uint32_t magic = kVpkMagic;
    uint32_t version = kVpkVersion;
    uint8_t codec = 0;
    uint8_t bit_depth = 8;
    uint16_t reserved = 0;
    int32_t width = 0;
    int32_t height = 0;
    uint32_t index_offset = 0;   // byte offset of the frame index (end of data)
    uint32_t frame_count = 0;    // frames written so far (high-water mark)
    uint32_t data_offset = 0;    // byte offset where frame data begins (fixed)
    uint64_t source_hash = 0;    // xxHash64 of the source file
    uint64_t source_size = 0;
    int64_t source_mtime = 0;
};

struct VpkFrameEntry {
    int32_t poc = 0;
    uint8_t is_keyframe = 0;
    uint8_t is_ready = 0;
    uint16_t reserved = 0;
    uint64_t syntax_offset = 0;
    uint32_t syntax_size = 0;
    uint64_t block_offset = 0;
    uint32_t block_size = 0;
    uint64_t reco_offset = 0;
    uint32_t reco_size = 0;
    uint64_t pred_offset = 0;
    uint32_t pred_size = 0;
    uint64_t resi_offset = 0;
    uint32_t resi_size = 0;
};

struct VpkFooter {
    uint32_t magic = kVpkFooterMagic;
    uint32_t ready_count = 0;
    uint32_t complete = 0;
    uint32_t reserved = 0;
};
#pragma pack(pop)

// Serialises a syntax tree into a compact blob (and back).
std::vector<uint8_t> encodeSyntaxTree(const SyntaxTree& tree);
Result<SyntaxTree> decodeSyntaxTree(const uint8_t* data, std::size_t size);

// Serialises a block grid into a compact blob (and back).
std::vector<uint8_t> encodeBlockGrid(const BlockGrid& grid);
Result<BlockGrid> decodeBlockGrid(const uint8_t* data, std::size_t size);

}  // namespace vide
