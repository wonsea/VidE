// Bitstream viewer: shows the raw bytes/bits of the stream around the current
// CU and highlights the CU's bit range. Display-only; the stream is scanned in
// the main process (Annex-B NAL units or IVF frames) and no syntax is parsed.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bitstream_index.h"
#include "ui/panel.h"

namespace vide {

class BitstreamPanel final : public IPanel {
public:
    const char* name() const override { return "Bitstream"; }
    void draw(Workspace& workspace) override;

private:
    enum class Radix { Hex, Binary };

    void refresh(Workspace& workspace);
    // Resolves the unit containing the current selection and loads its bytes.
    void ensureUnit(Workspace& workspace);

    uint64_t generation_ = 0;
    std::string path_;
    BsIndex index_;

    int unit_ = -1;          // unit currently shown
    uint32_t followed_frame_ = kInvalidId;
    Radix radix_ = Radix::Hex;
    bool scroll_pending_ = true;
    int scrolled_unit_ = -1;           // last auto-scrolled (unit, bit)
    uint64_t scrolled_bit_ = UINT64_MAX;

    // Cached bytes of `unit_`.
    std::vector<uint8_t> bytes_;

    // Current CU highlight, absolute in the file (bit-accurate).
    bool has_hl_ = false;
    uint64_t hl_bit_start_ = 0;
    uint64_t hl_bit_end_ = 0;  // inclusive
    uint32_t hl_block_ = kInvalidId;
};

}  // namespace vide
