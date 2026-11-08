#include "ui/image_panel.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "core/session.h"
#include "core/workspace.h"
#include "render/renderer.h"
#include "ui/ui_util.h"

namespace vide {
namespace {

// Vivid colour per block type, used for the small overlay "balls".
ImU32 blockTypeColor(BlockType type) {
    switch (type) {
        case BlockType::Intra: return IM_COL32(80, 220, 120, 255);
        case BlockType::Inter: return IM_COL32(90, 150, 255, 255);
        case BlockType::Skip: return IM_COL32(190, 190, 190, 255);
        case BlockType::PCM: return IM_COL32(255, 170, 60, 255);
        case BlockType::Unknown: break;
    }
    return IM_COL32(230, 230, 230, 255);
}

// Green (few bits) -> red (many bits) heat map; `max_bits` scales the ramp.
ImU32 bitsColor(uint32_t bits, uint32_t max_bits) {
    const float t = max_bits > 0
                        ? std::min(1.0f, static_cast<float>(bits) / static_cast<float>(max_bits))
                        : 0.0f;
    const int r = static_cast<int>(t * 255.0f);
    const int g = static_cast<int>((1.0f - t) * 220.0f) + 20;
    return IM_COL32(r, g, 40, 200);
}

}  // namespace

void ImagePanel::draw(Workspace& workspace) {
    Session& session = workspace.active();
    if (!ImGui::Begin(name())) {
        ImGui::End();
        return;
    }

    if (session.generation() != last_generation_) {
        renderer_->clearImage();
        uploaded_revision_ = 0;
        has_uploaded_ = false;
        last_generation_ = session.generation();
        last_frame_ = kInvalidId;
    }

    const char* modes[] = {"Reconstructed", "Prediction", "Residual"};
    int mode = static_cast<int>(session.imageMode());
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::Combo("View", &mode, modes, IM_ARRAYSIZE(modes))) {
        session.setImageMode(static_cast<ImageMode>(mode));
    }
    ImGui::SameLine();
    ImGui::Checkbox("Fit to window", &fit_);
    ImGui::SameLine();
    ImGui::TextDisabled("(unchecked = original size)");
    ImGui::Separator();

    if (session.hasDocument()) {
        const DecodedImage* image = session.currentImage();
        if (image != nullptr && session.imageRevision() != uploaded_revision_) {
            renderer_->setImage(*image);
            uploaded_revision_ = session.imageRevision();
            has_uploaded_ = true;
        }
    }

    const ImVec2 region = ImGui::GetContentRegionAvail();

    if (renderer_ == nullptr || !renderer_->hasImage()) {
        const char* hint;
        if (!session.hasDocument()) {
            hint = "No picture. Add a bitstream from the toolbar.";
        } else if (session.selection().frame == kInvalidId) {
            hint = "Select a frame from the thumbnail strip.";
        } else {
            hint = "Frame not ready yet (analysis in progress).";
        }
        const ImVec2 text_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2((region.x - text_size.x) * 0.5f,
                                   (region.y - text_size.y) * 0.5f));
        ImGui::TextDisabled("%s", hint);
        ImGui::End();
        return;
    }

    const float img_w = static_cast<float>(renderer_->width());
    const float img_h = static_cast<float>(renderer_->height());
    float scale = 1.0f;
    if (fit_ && img_w > 0.0f && img_h > 0.0f) {
        scale = std::min(region.x / img_w, region.y / img_h);
    }
    const ImVec2 image_size(img_w * scale, img_h * scale);

    ImGui::BeginChild("picture_viewport", ImVec2(0, 0), ImGuiChildFlags_Borders,
                      fit_ ? ImGuiWindowFlags_None : ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 view = ImGui::GetContentRegionAvail();

    if (fit_) {
        if (image_size.x < view.x) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (view.x - image_size.x) * 0.5f);
        }
        if (image_size.y < view.y) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (view.y - image_size.y) * 0.5f);
        }
    }

    ImGui::Image(static_cast<ImTextureID>(renderer_->textureId()), image_size);

    const ImVec2 image_min = ImGui::GetItemRectMin();
    const ImVec2 image_max = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();

    const OverlaySettings& overlay = session.overlay();
    const BlockGrid* grid = session.currentBlocks();
    if (grid != nullptr && grid->width > 0 && grid->height > 0 && !grid->blocks.empty()) {
        const float sx = (image_max.x - image_min.x) / static_cast<float>(grid->width);
        const float sy = (image_max.y - image_min.y) / static_cast<float>(grid->height);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        const int ctu = grid->ctu_size > 0 ? grid->ctu_size : 64;
        const int ctu_cols = (grid->width + ctu - 1) / ctu;

        uint32_t max_bits = 1;
        if (overlay.bits_heatmap) {
            for (const Block& block : grid->blocks) {
                max_bits = std::max(max_bits, block.bits);
            }
        }

        const Block* selected = nullptr;
        for (const Block& block : grid->blocks) {
            const ImVec2 p0(image_min.x + block.rect.x * sx,
                            image_min.y + block.rect.y * sy);
            const ImVec2 p1(image_min.x + block.rect.right() * sx,
                            image_min.y + block.rect.bottom() * sy);

            if (overlay.bits_heatmap) {
                draw->AddRectFilled(p0, p1, bitsColor(block.bits, max_bits));
            }
            if (overlay.block_type) {
                // Type "ball": diameter = 2/3 of the block's shorter edge
                // (radius = edge/3), in screen pixels of the drawn block.
                const float edge = std::min(p1.x - p0.x, p1.y - p0.y);
                const float r = std::max(1.5f, edge / 3.0f);
                const ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
                draw->AddCircleFilled(center, r, blockTypeColor(block.type), 16);
            }
            if (overlay.block_borders) {
                const float thickness = block.depth <= 1 ? 1.5f : 1.0f;
                draw->AddRect(p0, p1, blockTypeColor(block.type), 0.0f, 0, thickness);
            }
            // QP numbers (overlay "QP"): text only, never a colour fill — the
            // picture itself must stay unchanged (req 12). Inherited QPs are
            // marked with a trailing '*' (req 7).
            if (overlay.qp_heatmap && block.qp >= 0 && (p1.x - p0.x) > 14.0f &&
                (p1.y - p0.y) > 10.0f) {
                char label[8];
                if (block.qp_source == QpSource::Inherited) {
                    std::snprintf(label, sizeof(label), "%d*", block.qp);
                } else {
                    std::snprintf(label, sizeof(label), "%d", block.qp);
                }
                draw->AddText(ImVec2(p0.x + 1.0f, p0.y + 1.0f), IM_COL32(0, 0, 0, 220), label);
                draw->AddText(p0, IM_COL32(255, 255, 255, 255), label);
            }
            if (overlay.motion_vectors &&
                (block.type == BlockType::Inter || block.type == BlockType::Skip) &&
                block.ref_idx[0] >= 0) {
                const ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
                const ImVec2 tip(center.x + (block.mv[0][0] / 4.0f) * sx,
                                 center.y + (block.mv[0][1] / 4.0f) * sy);
                const ImU32 color = IM_COL32(255, 80, 80, 220);
                draw->AddLine(center, tip, color, 1.5f);
                draw->AddCircleFilled(tip, 2.0f, color);
            }
            if (block.id == session.selection().block) {
                selected = &block;
            }
        }

        // Current CTU / CU highlighting (req 6): the selected CTU outline, every
        // CU inside it, and the selected CU on top.
        if (session.selection().ctu != kInvalidId) {
            const int cy = static_cast<int>(session.selection().ctu) / ctu_cols;
            const int cx = static_cast<int>(session.selection().ctu) % ctu_cols;
            const ImVec2 c0(image_min.x + cx * ctu * sx, image_min.y + cy * ctu * sy);
            const ImVec2 c1(image_min.x + std::min((cx + 1) * ctu, grid->width) * sx,
                            image_min.y + std::min((cy + 1) * ctu, grid->height) * sy);
            draw->AddRect(c0, c1, IM_COL32(255, 140, 0, 255), 0.0f, 0, 2.5f);
            for (const Block& block : grid->blocks) {
                const int bctu =
                    (block.rect.y / ctu) * ctu_cols + (block.rect.x / ctu);
                if (static_cast<uint32_t>(bctu) == session.selection().ctu) {
                    const ImVec2 p0(image_min.x + block.rect.x * sx,
                                    image_min.y + block.rect.y * sy);
                    const ImVec2 p1(image_min.x + block.rect.right() * sx,
                                    image_min.y + block.rect.bottom() * sy);
                    draw->AddRect(p0, p1, IM_COL32(255, 220, 0, 255), 0.0f, 0, 1.5f);
                }
            }
        }
        if (selected != nullptr) {
            const ImVec2 p0(image_min.x + selected->rect.x * sx,
                            image_min.y + selected->rect.y * sy);
            const ImVec2 p1(image_min.x + selected->rect.right() * sx,
                            image_min.y + selected->rect.bottom() * sy);
            draw->AddRect(p0, p1, IM_COL32(255, 0, 0, 255), 0.0f, 0, 3.0f);
        }

        // Click: select the smallest CU under the cursor and its CTU.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int px = static_cast<int>((mouse.x - image_min.x) / sx);
            const int py = static_cast<int>((mouse.y - image_min.y) / sy);
            const Block* hit = nullptr;
            for (const Block& block : grid->blocks) {
                if (px >= block.rect.x && px < block.rect.right() && py >= block.rect.y &&
                    py < block.rect.bottom()) {
                    if (hit == nullptr ||
                        block.rect.w * block.rect.h < hit->rect.w * hit->rect.h) {
                        hit = &block;
                    }
                }
            }
            session.selection().block = (hit != nullptr) ? hit->id : kInvalidId;
            if (hit != nullptr) {
                session.selection().ctu =
                    static_cast<uint32_t>((hit->rect.y / ctu) * ctu_cols + (hit->rect.x / ctu));
                // Reverse link: select the matching syntax node (S-3).
                if (const SyntaxTree* syntax = session.currentSyntax()) {
                    const uint32_t node = syntaxNodeForBlock(*syntax, hit->id);
                    if (node != kInvalidId) {
                        session.selection().syntax_node = node;
                    }
                }
            } else {
                session.selection().ctu = kInvalidId;
            }
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

}  // namespace vide
