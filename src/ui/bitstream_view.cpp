#include "ui/bitstream_view.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

namespace vide {

void drawBitstreamGrid(const BitstreamGrid& grid, bool& scroll_pending) {
    const int bytes_per_row = grid.binary ? 8 : 16;
    const std::size_t row_count = (grid.size + bytes_per_row - 1) / bytes_per_row;
    const ImVec4 kHl(1.0f, 0.85f, 0.2f, 1.0f);
    const uint64_t hl_byte_lo = grid.has_highlight ? (grid.hl_bit_start / 8) : 0;
    const uint64_t hl_byte_hi = grid.has_highlight ? (grid.hl_bit_end / 8) : 0;

    ImGui::BeginChild("bitstream_view", ImVec2(0, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX;
    if (ImGui::BeginTable("bs_grid", bytes_per_row + 2, flags)) {
        ImGui::TableSetupColumn("offset", ImGuiTableColumnFlags_WidthFixed, 96.0f);
        for (int c = 0; c < bytes_per_row; ++c) {
            ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthFixed,
                                    grid.binary ? 62.0f : 24.0f);
        }
        ImGui::TableSetupColumn("ascii", ImGuiTableColumnFlags_WidthFixed, 140.0f);

        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("address");
        for (int c = 0; c < bytes_per_row; ++c) {
            ImGui::TableSetColumnIndex(c + 1);
            ImGui::TextDisabled("%02X", c);
        }
        ImGui::TableSetColumnIndex(bytes_per_row + 1);
        ImGui::TextDisabled("ascii");

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(row_count));
        if (scroll_pending && grid.target_row >= 0 &&
            grid.target_row < static_cast<int>(row_count)) {
            clipper.IncludeItemByIndex(grid.target_row);  // force visible so we can scroll
        }
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const std::size_t base = static_cast<std::size_t>(row) * bytes_per_row;
                const uint64_t row_off = grid.base_offset + base;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%08llX", static_cast<unsigned long long>(row_off));
                if (row == grid.target_row && scroll_pending) {
                    ImGui::SetScrollHereY(0.35f);
                    scroll_pending = false;
                }

                char ascii[17];
                for (int c = 0; c < bytes_per_row; ++c) {
                    ImGui::TableSetColumnIndex(c + 1);
                    const std::size_t idx = base + static_cast<std::size_t>(c);
                    if (idx >= grid.size) {
                        ImGui::TextUnformatted("");
                        continue;
                    }
                    const uint8_t byte = grid.bytes[idx];
                    ascii[c] = (byte >= 32 && byte < 127) ? static_cast<char>(byte) : '.';

                    if (!grid.binary) {
                        const uint64_t abs = row_off + c;
                        if (grid.has_highlight && abs >= hl_byte_lo && abs <= hl_byte_hi) {
                            ImGui::TextColored(kHl, "%02X", byte);
                        } else {
                            ImGui::Text("%02X", byte);
                        }
                    } else {
                        const uint64_t abs_bit = (row_off + c) * 8;
                        const bool overlap = grid.has_highlight && grid.hl_bit_end >= abs_bit &&
                                             grid.hl_bit_start <= abs_bit + 7;
                        if (!overlap) {
                            char bits[9];
                            for (int b = 0; b < 8; ++b) {
                                bits[b] = ((byte >> (7 - b)) & 1) ? '1' : '0';
                            }
                            bits[8] = '\0';
                            ImGui::TextUnformatted(bits);
                        } else {
                            for (int b = 0; b < 8; ++b) {
                                if (b != 0) {
                                    ImGui::SameLine(0.0f, 0.0f);
                                }
                                const uint64_t ab = abs_bit + b;
                                const char ch = ((byte >> (7 - b)) & 1) ? '1' : '0';
                                if (ab >= grid.hl_bit_start && ab <= grid.hl_bit_end) {
                                    ImGui::TextColored(kHl, "%c", ch);
                                } else {
                                    ImGui::Text("%c", ch);
                                }
                            }
                        }
                    }
                }
                ascii[bytes_per_row] = '\0';
                ImGui::TableSetColumnIndex(bytes_per_row + 1);
                if (!grid.binary) {
                    ImGui::TextDisabled("%s", ascii);
                } else {
                    ImGui::TextUnformatted("");
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

}  // namespace vide
