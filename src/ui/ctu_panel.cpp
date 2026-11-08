#include "ui/ctu_panel.h"

#include <algorithm>

#include <imgui.h>

#include "core/workspace.h"

namespace vide {

void CtuPanel::draw(Workspace& workspace) {
    if (!ImGui::Begin(name())) {
        ImGui::End();
        return;
    }

    // Vertical split: CTU image (top) / block info (bottom), with a draggable
    // separator so the user can rebalance the two.
    static float top_ratio = 0.55f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float sep = 6.0f;
    const float usable = std::max(avail.y - sep, 1.0f);
    const float top_h = std::max(60.0f, usable * top_ratio);

    ImGui::BeginChild("ctu_top", ImVec2(0.0f, top_h), ImGuiChildFlags_Borders);
    view_.drawBody(workspace);
    ImGui::EndChild();

    // Draggable splitter.
    ImGui::InvisibleButton("ctu_split", ImVec2(-1.0f, sep));
    if (ImGui::IsItemActive()) {
        top_ratio += ImGui::GetIO().MouseDelta.y / usable;
        top_ratio = std::min(std::max(top_ratio, 0.15f), 0.9f);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }

    ImGui::BeginChild("ctu_bottom", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    info_.drawBody(workspace);
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace vide
