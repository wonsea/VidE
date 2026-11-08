#include "ui/block_info_panel.h"

#include <cstdarg>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "core/session.h"
#include "core/workspace.h"
#include "ui/syntax_view.h"
#include "ui/ui_util.h"

namespace vide {

void BlockInfoPanel::draw(Workspace& workspace) {
    if (!ImGui::Begin(name())) {
        ImGui::End();
        return;
    }
    drawBody(workspace);
    ImGui::End();
}

void BlockInfoPanel::drawBody(Workspace& workspace) {
    Session& session = workspace.active();

    if (!session.hasDocument()) {
        ImGui::TextDisabled("Block details appear here once a frame is analysed.");
        return;
    }

    const CodecSession* codec = session.codec();
    const bool block_capable = codec != nullptr && codec->hasMotionInfo();

    // Frame reference POCs (from the syntax tree) for the selected block's Ref rows.
    std::vector<int> ref0_poc;
    std::vector<int> ref1_poc;
    if (const SyntaxTree* st = session.currentSyntax()) {
        collectRefPocs(*st, ref0_poc, ref1_poc);
    }

    // Only error notes surface above the tabs; stream/capability summary was
    // dropped (redundant with the toolbar).
    if (codec != nullptr && !codec->error().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.3f, 1.0f));
        ImGui::TextWrapped("Note: %s", codec->error().c_str());
        ImGui::PopStyleColor();
    }

    // Block Info and CTU syntax share the panel, switched by tabs (req 1).
    if (ImGui::BeginTabBar("block_info_tabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Block Info")) {
            if (session.selection().block == kInvalidId) {
                ImGui::TextDisabled("Click a block in the picture to inspect it.");
            } else if (!block_capable) {
                ImGui::Text("Block #%u", session.selection().block);
                ImGui::TextDisabled("This codec does not expose block info.");
            } else {
                const BlockGrid* grid = session.currentBlocks();
                if (grid == nullptr) {
                    ImGui::TextDisabled("Block info for this frame is not available yet.");
                } else {
                    const Block* found = findBlock(grid, session.selection().block);
                    if (found == nullptr) {
                        ImGui::TextDisabled("Block #%u is not present in this frame.",
                                            session.selection().block);
                    } else {
                        drawBlockTable(*found, ref0_poc, ref1_poc);
                    }
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem((std::string(blockGroupTerm(session.info().codec)) + " syntax").c_str())) {
            const SyntaxTree* tree = session.currentSyntax();
            if (tree == nullptr) {
                ImGui::TextDisabled("Syntax not available yet.");
            } else if (session.selection().block == kInvalidId) {
                ImGui::TextDisabled("Select a CU to show its %s syntax.",
                                    blockGroupTerm(session.info().codec));
            } else {
                const uint32_t ctu = findCtuNode(*tree, session.selection().block);
                if (ctu == kInvalidId) {
                    ImGui::TextDisabled("No %s syntax node for this CU.",
                                        blockGroupTerm(session.info().codec));
                } else {
                    if (session.selection().block != last_block_) {
                        last_block_ = session.selection().block;
                        scroll_pending_ = true;
                    }
                    ImGui::BeginChild("ctu_syntax", ImVec2(0, 0), ImGuiChildFlags_Borders);
                    std::vector<uint32_t> open_path =
                        syntaxAncestors(*tree, session.selection().syntax_node);
                    drawSyntaxNode(*tree, ctu, session.selection(), scroll_pending_,
                                   open_path.empty() ? nullptr : &open_path);
                    ImGui::EndChild();
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void BlockInfoPanel::drawBlockTable(const Block& block, const std::vector<int>& ref0_poc,
                                    const std::vector<int>& ref1_poc) {
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("block_info", 2, flags)) {
        return;
    }
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    auto row = [](const char* field, const char* fmt, ...) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(field);
        ImGui::TableSetColumnIndex(1);
        va_list args;
        va_start(args, fmt);
        ImGui::TextV(fmt, args);
        va_end(args);
    };

    row("Block", "#%u", block.id);
    row("Position", "(%d, %d)", block.rect.x, block.rect.y);
    row("Size", "%dx%d", block.rect.w, block.rect.h);
    row("Depth", "%d", block.depth);
    row("Type", "%s", toString(block.type));
    row("Pred mode", "%s", toString(block.pred_mode));
    if (block.qp >= 0) {
        const char* src = toString(block.qp_source);
        if (block.qp_source == QpSource::Inherited) {
            row("QP", "%d (inherited)", block.qp);
        } else if (block.qp_source == QpSource::Coded) {
            row("QP", "%d (coded)", block.qp);
        } else {
            row("QP", "%d", block.qp);
        }
        (void)src;
    } else {
        row("QP", "n/a");
    }
    row("Bits", "%u", block.bits);
    if (block.bit_length > 0) {
        row("Bit range", "[%u, %u)  (slice data)", block.bit_offset,
            block.bit_offset + block.bit_length);
    }

    // Reference lists (req 6): L0/L1 with the reference index, reference POC
    // (when the frame exposes it) and MV per list.
    auto refRow = [&](const char* label, int ref_idx, int mvx, int mvy,
                      const std::vector<int>& pocs) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        if (ref_idx < 0) {
            ImGui::TextDisabled("(none)");
            return;
        }
        if (static_cast<std::size_t>(ref_idx) < pocs.size() && pocs[ref_idx] != -9999) {
            ImGui::Text("ref%d  POC %d  MV (%d, %d)", ref_idx, pocs[ref_idx], mvx, mvy);
        } else {
            ImGui::Text("ref%d  MV (%d, %d)", ref_idx, mvx, mvy);
        }
    };
    refRow("Ref L0", block.ref_idx[0], block.mv[0][0], block.mv[0][1], ref0_poc);
    refRow("Ref L1", block.ref_idx[1], block.mv[1][0], block.mv[1][1], ref1_poc);

    ImGui::EndTable();
}

}  // namespace vide
