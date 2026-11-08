#include "ui/layout.h"

#include <imgui_internal.h>

namespace vide {

void buildDefaultLayout(ImGuiID dockspace_id) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

    // DockBuilderSplitNode(id, dir, ratio_at_dir, out_at_dir, out_at_opposite).
    // The first output is the newly created node sitting in `dir`.
    ImGuiID toolbar = 0;
    ImGuiID rest = 0;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Up, 0.12f, &toolbar, &rest);

    ImGuiID thumbnails = 0;
    ImGuiID middle = 0;
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Down, 0.26f, &thumbnails, &middle);

    ImGuiID left_col = 0;
    ImGuiID center_and_right = 0;
    ImGui::DockBuilderSplitNode(middle, ImGuiDir_Left, 0.28f, &left_col, &center_and_right);

    ImGuiID right_col = 0;
    ImGuiID picture = 0;
    ImGui::DockBuilderSplitNode(center_and_right, ImGuiDir_Right, 0.36f, &right_col, &picture);

    // Left column: Frame / Syntax tabs.
    // Right column: Bitstream / CTU tabs (CTU itself is split image + block info).
    ImGui::DockBuilderDockWindow("Toolbar", toolbar);
    ImGui::DockBuilderDockWindow("Frame", left_col);
    ImGui::DockBuilderDockWindow("Syntax", left_col);
    ImGui::DockBuilderDockWindow("Bitstream", right_col);
    ImGui::DockBuilderDockWindow("CTU", right_col);
    ImGui::DockBuilderDockWindow("Picture", picture);
    ImGui::DockBuilderDockWindow("Thumbnails", thumbnails);

    ImGui::DockBuilderFinish(dockspace_id);
}

}  // namespace vide
