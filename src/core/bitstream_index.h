// Elementary-stream bitstream index for the hex viewer. The main process scans
// the Annex-B elementary stream (already extracted for the reference helpers)
// into NAL units so the UI can show raw bytes and map a frame to its slice NAL.
// This is display-only: no bitstream syntax is interpreted here.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/types.h"

namespace vide {

// One NAL unit in an Annex-B stream.
struct BsUnit {
    uint64_t offset = 0;  // byte offset of the NAL header (after the start code)
    uint32_t size = 0;    // bytes from the NAL header to the next start code
    int type = -1;        // codec-specific NAL unit type
    int temporal_id = -1; // VVC/HEVC temporal id (else -1)
    bool vcl = false;     // slice / coded-picture NAL
};

struct BsIndex {
    CodecId codec = CodecId::Unknown;
    uint64_t file_size = 0;
    std::vector<BsUnit> units;
    std::vector<uint32_t> vcl_units;  // indices into `units`, in stream (decode) order
    uint64_t total_units = 0;
    bool truncated = false;  // scanning stopped before end of file
    bool annex_b = true;     // false for IVF (VP9/AV1): no start codes / EPB
};

// Scans `path` (Annex-B for AVC/HEVC/VVC) into NAL units. At most `max_units`
// units are retained; `truncated` is set when the file continues past that.
BsIndex indexAnnexB(const std::string& path, CodecId codec, uint64_t max_units = 200000);

// Scans an IVF container (VP9/AV1) into per-frame units (offset = frame data
// start, size = compressed frame size). `annex_b` is false.
BsIndex indexIvf(const std::string& path, CodecId codec, uint64_t max_units = 200000);

// Human-readable NAL unit type name for the given codec.
std::string nalTypeName(CodecId codec, int type);

// Reads `size` bytes at `offset` from `path` (clamped to EOF). Empty on failure.
std::vector<uint8_t> readByteRange(const std::string& path, uint64_t offset, uint32_t size);

// Maps an RBSP byte index (emulation-prevention bytes removed) to the raw NAL
// byte index. `map[i]` is the raw index of the i-th RBSP byte; bytes that are
// EPB (0x03 after 0x00 0x00) are skipped. Used to convert a decoder-side RBSP
// bit offset to an absolute byte offset in the elementary stream.
std::vector<uint32_t> rbspToRawByteMap(const uint8_t* raw, std::size_t size);

}  // namespace vide
