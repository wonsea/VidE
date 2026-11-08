#include "ui/frame_panel.h"

#include <cstdint>
#include <vector>

#include <imgui.h>

#include "core/session.h"
#include "core/workspace.h"
#include "ui/ui_util.h"

namespace vide {

void FramePanel::draw(Workspace& workspace) {
    Session& session = workspace.active();
    if (!ImGui::Begin(name())) {
        ImGui::End();
        return;
    }

    if (!session.hasDocument()) {
        ImGui::TextDisabled("Open a bitstream to inspect per-frame info.");
        ImGui::End();
        return;
    }

    FrameMeta meta;
    bool have_meta = false;
    if (session.codec() != nullptr && session.selection().frame != kInvalidId) {
        auto m = session.codec()->frameMeta(session.selection().frame);
        if (m.ok()) {
            meta = std::move(m.value());
            have_meta = true;
        }
    }

    ImGui::Text("Frame: %u / %lld", session.selection().frame,
                static_cast<long long>(session.frameCount()));
    if (have_meta) {
        ImGui::Text("Display order (POC): %lld    coding order: %lld    type: %s",
                    static_cast<long long>(meta.poc), static_cast<long long>(meta.index),
                    toString(meta.frame_type));
    }

    // Reference POCs emitted by the helper as ref_l0_poc[i] / ref_l1_poc[i].
    std::vector<int> ref0;
    std::vector<int> ref1;
    if (const SyntaxTree* tree = session.currentSyntax()) {
        collectRefPocs(*tree, ref0, ref1);
    }
    const bool have_refs = !ref0.empty() || !ref1.empty();

    ImGui::SeparatorText("Frame references (DPB)");
    if (!have_refs) {
        ImGui::TextDisabled("Reference POC not exposed by this codec "
                            "(or intra-only frame).");
        ImGui::End();
        return;
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("dpb_refs", 3, flags)) {
        ImGui::TableSetupColumn("List", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("Ref", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Ref POC", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        auto row = [](int list, int ref, int poc) {
            if (poc == -9999) {
                return;
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("L%d", list);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", ref);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", poc);
        };
        for (std::size_t i = 0; i < ref0.size(); ++i) {
            row(0, static_cast<int>(i), ref0[i]);
        }
        for (std::size_t i = 0; i < ref1.size(); ++i) {
            row(1, static_cast<int>(i), ref1[i]);
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

}  // namespace vide
