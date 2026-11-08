#include "ui/ctu_view_panel.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "core/session.h"
#include "core/workspace.h"
#include "render/renderer.h"
#include "ui/ui_util.h"

namespace vide {
namespace {

ImU32 typeColor(BlockType type) {
    switch (type) {
        case BlockType::Intra: return IM_COL32(80, 220, 120, 255);
        case BlockType::Inter: return IM_COL32(90, 150, 255, 255);
        case BlockType::Skip: return IM_COL32(190, 190, 190, 255);
        case BlockType::PCM: return IM_COL32(255, 170, 60, 255);
        case BlockType::Unknown: break;
    }
    return IM_COL32(230, 230, 230, 255);
}

ImU32 typeFill(BlockType type) {
    ImU32 c = typeColor(type);
    return (c & 0x00FFFFFF) | (64u << 24);  // low-alpha tint
}

}  // namespace

void CtuViewPanel::draw(Workspace& workspace) {
    if (!ImGui::Begin(name())) {
        ImGui::End();
        return;
    }
    drawBody(workspace);
    ImGui::End();
}

void CtuViewPanel::drawBody(Workspace& workspace) {
    Session& session = workspace.active();

    if (!session.hasDocument() || renderer_ == nullptr || !renderer_->hasImage()) {
        ImGui::TextDisabled("No picture.");
        return;
    }
    const BlockGrid* grid = session.currentBlocks();
    if (grid == nullptr || grid->blocks.empty() || session.selection().ctu == kInvalidId) {
        ImGui::TextDisabled("Click a block in the picture to select a %s.",
                            blockGroupTerm(session.info().codec));
        return;
    }

    const int ctu = grid->ctu_size > 0 ? grid->ctu_size : 64;
    const int cols = (grid->width + ctu - 1) / ctu;
    const int cy = static_cast<int>(session.selection().ctu) / cols;
    const int cx = static_cast<int>(session.selection().ctu) % cols;
    const int x0 = cx * ctu;
    const int y0 = cy * ctu;
    const int x1 = std::min(x0 + ctu, grid->width);
    const int y1 = std::min(y0 + ctu, grid->height);

    const uint32_t ctu_index = session.selection().ctu;

    ImGui::Text("%s (%d, %d)  %dx%d  size %d  | click a CU to select",
                blockGroupTerm(session.info().codec), cx, cy, x1 - x0, y1 - y0, ctu);

    const float img_w = static_cast<float>(renderer_->width());
    const float img_h = static_cast<float>(renderer_->height());
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float scale = std::min(avail.x / static_cast<float>(x1 - x0),
                                 avail.y / static_cast<float>(y1 - y0));
    const ImVec2 size((x1 - x0) * scale, (y1 - y0) * scale);
    const ImVec2 uv0(x0 / img_w, y0 / img_h);
    const ImVec2 uv1(x1 / img_w, y1 / img_h);

    ImGui::Image(static_cast<ImTextureID>(renderer_->textureId()), size, uv0, uv1);
    const ImVec2 pmin = ImGui::GetItemRectMin();
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* draw = ImGui::GetWindowDrawList();

    for (const Block& block : grid->blocks) {
        const int bctu = (block.rect.y / ctu) * cols + (block.rect.x / ctu);
        if (static_cast<uint32_t>(bctu) != ctu_index) {
            continue;
        }
        const ImVec2 p0(pmin.x + (block.rect.x - x0) * scale,
                        pmin.y + (block.rect.y - y0) * scale);
        const ImVec2 p1(pmin.x + (block.rect.right() - x0) * scale,
                        pmin.y + (block.rect.bottom() - y0) * scale);

        const bool is_selected = (block.id == session.selection().block);
        // Tint by type so adjacent CUs are distinguishable.
        draw->AddRectFilled(p0, p1, typeFill(block.type));
        const float t = is_selected ? 3.0f : 1.5f;
        draw->AddRect(p0, p1, typeColor(block.type), 0.0f, 0, t);

        // Info overlay (req 8): type + QP + bits; MV arrow when inter (req 8).
        const float bw = p1.x - p0.x;
        const float bh = p1.y - p0.y;
        char line[64];
        if (block.qp >= 0) {
            std::snprintf(line, sizeof(line), "%s Q%d B%u", toString(block.type), block.qp,
                          block.bits);
        } else {
            std::snprintf(line, sizeof(line), "%s B%u", toString(block.type), block.bits);
        }
        if (bw > 40.0f && bh > 12.0f) {
            draw->AddText(ImVec2(p0.x + 1.0f, p0.y + 1.0f), IM_COL32(0, 0, 0, 220), line);
            draw->AddText(p0, IM_COL32(255, 255, 255, 255), line);
        } else if (block.qp >= 0 && bw > 14.0f && bh > 10.0f) {
            char qp_label[8];
            std::snprintf(qp_label, sizeof(qp_label), "%d", block.qp);
            draw->AddText(ImVec2(p0.x + 1.0f, p0.y + 1.0f), IM_COL32(0, 0, 0, 220), qp_label);
            draw->AddText(p0, IM_COL32(255, 255, 255, 255), qp_label);
        }

        if ((block.type == BlockType::Inter || block.type == BlockType::Skip) &&
            block.ref_idx[0] >= 0) {
            const ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            const ImVec2 tip(center.x + (block.mv[0][0] / 4.0f) * scale,
                             center.y + (block.mv[0][1] / 4.0f) * scale);
            const ImU32 color = IM_COL32(255, 80, 80, 240);
            draw->AddLine(center, tip, color, 2.0f);
            draw->AddCircleFilled(tip, 3.0f, color);
        }
    }

    // Click a CU inside the magnified CTU to select it (req 5), same selection
    // model as the main picture so both views stay in sync.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int px = x0 + static_cast<int>((mouse.x - pmin.x) / scale);
        const int py = y0 + static_cast<int>((mouse.y - pmin.y) / scale);
        const Block* hit = nullptr;
        for (const Block& block : grid->blocks) {
            const int bctu = (block.rect.y / ctu) * cols + (block.rect.x / ctu);
            if (static_cast<uint32_t>(bctu) != ctu_index) {
                continue;
            }
            if (px >= block.rect.x && px < block.rect.right() && py >= block.rect.y &&
                py < block.rect.bottom()) {
                if (hit == nullptr || block.rect.w * block.rect.h < hit->rect.w * hit->rect.h) {
                    hit = &block;
                }
            }
        }
        if (hit != nullptr) {
            session.selection().block = hit->id;
            if (const SyntaxTree* syntax = session.currentSyntax()) {
                const uint32_t node = syntaxNodeForBlock(*syntax, hit->id);
                if (node != kInvalidId) {
                    session.selection().syntax_node = node;
                }
            }
        }
    }
}

}  // namespace vide
