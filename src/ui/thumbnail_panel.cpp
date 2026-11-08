#include "ui/thumbnail_panel.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "core/session.h"
#include "core/workspace.h"
#include "render/renderer.h"

namespace vide {
namespace {

uint64_t thumbKey(uint64_t generation, uint32_t frame) {
    return (generation << 32) | static_cast<uint64_t>(frame);
}

}  // namespace

void ThumbnailPanel::draw(Workspace& workspace) {
    if (!ImGui::Begin(name())) {
        ImGui::End();
        return;
    }

    // A stream was removed: drop stale textures (they re-generate on demand).
    if (workspace.count() < last_count_) {
        renderer_->clearThumbnails();
    }
    last_count_ = workspace.count();

    if (workspace.empty()) {
        ImGui::TextDisabled("Add a bitstream to begin (all streams share this view).");
        ImGui::End();
        return;
    }

    const float thumb_w = 160.0f;
    const float thumb_h = 90.0f;

    // One outer scroll region. Each stream occupies exactly one horizontal row
    // (no wrapping); a single horizontal scrollbar scrolls every row together,
    // so there is never a scrollbar per stream.
    ImGui::BeginChild("thumbnail_rows", ImVec2(0, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);

    for (int i = 0; i < workspace.count(); ++i) {
        Session* session = workspace.at(i);
        if (session == nullptr || !session->hasDocument()) {
            continue;
        }
        ImGui::PushID(i);

        const std::string name =
            std::filesystem::path(session->path()).filename().string();
        const uint32_t ready =
            session->codec() != nullptr ? session->codec()->framesReady() : 0;
        const bool analysing = session->codec() != nullptr &&
                               session->codec()->state() == SessionState::Analyzing;
        if (i == workspace.activeIndex()) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "[%d] %s", i + 1, name.c_str());
        } else {
            ImGui::Text("[%d] %s", i + 1, name.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("ready %u%s", ready, analysing ? " (analysing...)" : "");

        // All of this stream's thumbnails go on one line; the next stream starts
        // a fresh line because EndGroup advances the cursor past the row.
        const int64_t frame_count = session->frameCount();
        for (int64_t f = 0; f < frame_count; ++f) {
            if (f > 0) {
                ImGui::SameLine();
            }

            ImGui::PushID(static_cast<int>(f));
            ImGui::BeginGroup();

            const uint64_t key = thumbKey(session->generation(), static_cast<uint32_t>(f));
            uint32_t tex = renderer_->thumbnailTexture(key);
            if (tex == 0 && session->codec() != nullptr) {
                session->codec()->requestThumbnail(static_cast<uint32_t>(f));
                DecodedImage image;
                if (session->codec()->copyThumbnail(static_cast<uint32_t>(f), image)) {
                    renderer_->setThumbnail(key, image);
                    tex = renderer_->thumbnailTexture(key);
                }
            }

            bool clicked = false;
            if (tex != 0) {
                clicked = ImGui::ImageButton("thumb", static_cast<ImTextureID>(tex),
                                             ImVec2(thumb_w, thumb_h));
            } else {
                clicked = ImGui::Button("...", ImVec2(thumb_w, thumb_h));
            }
            if (i == workspace.activeIndex() && f == session->selection().frame) {
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                                    ImGui::GetItemRectMax(),
                                                    IM_COL32(255, 200, 0, 255), 0.0f, 0, 2.0f);
            }
            if (clicked) {
                workspace.setActive(i);
                session->gotoFrame(static_cast<uint32_t>(f));
            }

            // Display order / coding order / frame type (req 2).
            FrameMeta meta;
            bool have_meta = false;
            if (session->codec() != nullptr) {
                auto m = session->codec()->frameMeta(static_cast<uint32_t>(f));
                if (m.ok()) {
                    meta = std::move(m.value());
                    have_meta = true;
                }
            }
            if (have_meta) {
                ImGui::TextDisabled("d%lld c%lld %s", static_cast<long long>(meta.poc),
                                    static_cast<long long>(meta.index),
                                    toString(meta.frame_type));
            } else {
                ImGui::TextDisabled("c%lld", static_cast<long long>(f));
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace vide
