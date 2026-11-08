// Bitstream grid renderer: a classic offset-gutter dump of raw bytes, either
// hex (16 B/row) or binary (8 B/row, bit-accurate), with an optional highlighted
// bit range. Shared by the Bitstream panel.
#pragma once

#include <cstdint>
#include <cstddef>

namespace vide {

struct BitstreamGrid {
    const uint8_t* bytes = nullptr;  // unit bytes (starting at base_offset)
    std::size_t size = 0;
    uint64_t base_offset = 0;        // absolute file offset of bytes[0]
    bool binary = false;             // false = hex, true = bit view
    bool has_highlight = false;
    uint64_t hl_bit_start = 0;       // absolute file bit range (inclusive)
    uint64_t hl_bit_end = 0;
    int target_row = 0;              // row to auto-scroll to when pending
};

// Renders the grid in a scrollable child window. Clears `scroll_pending` once it
// has scrolled to `target_row` (even if that row is off-screen).
void drawBitstreamGrid(const BitstreamGrid& grid, bool& scroll_pending);

}  // namespace vide
