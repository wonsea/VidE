#include "ui/syntax_panel.h"

#include <vector>

#include <imgui.h>

#include "core/session.h"
#include "core/workspace.h"
#include "ui/syntax_view.h"

namespace vide {

void SyntaxPanel::draw(Workspace& workspace) {
    Session& session = workspace.active();
    if (!ImGui::Begin(name())) {
        ImGui::End();
        return;
    }

    if (session.generation() != last_generation_) {
        last_generation_ = session.generation();
        last_selection_ = kInvalidId;
        scroll_pending_ = false;
        active_section_ = 0;
    }

    if (!session.hasDocument()) {
        ImGui::TextDisabled("Add a bitstream to inspect its syntax.");
        ImGui::End();
        return;
    }

    const SyntaxTree* tree = session.currentSyntax();
    if (tree == nullptr || tree->empty()) {
        ImGui::TextWrapped(
            "Syntax for this frame is not available yet (analysis in progress or unsupported).");
        ImGui::End();
        return;
    }

    const std::vector<SyntaxSection> sections = collectSyntaxSections(*tree);
    if (sections.empty()) {
        ImGui::TextWrapped("This frame has no syntax tree.");
        ImGui::End();
        return;
    }
    if (active_section_ < 0 || active_section_ >= static_cast<int>(sections.size())) {
        active_section_ = 0;
    }

    if (ImGui::BeginTabBar("syntax_sections", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        // Disambiguate duplicate labels (e.g. several PPS / slice_header roots)
        // so every section gets its own tab instead of an id collision.
        std::vector<int> seen(sections.size(), 0);
        for (int i = 0; i < static_cast<int>(sections.size()); ++i) {
            std::string label = sections[i].label;
            for (int j = 0; j < i; ++j) {
                if (sections[j].label == sections[i].label) {
                    ++seen[i];
                }
            }
            if (seen[i] > 0) {
                label += " " + std::to_string(seen[i] + 1);
            }
            const std::string tab_id =
                label + "###syntax_sec_" + std::to_string(sections[i].root);
            if (ImGui::BeginTabItem(tab_id.c_str())) {
                active_section_ = i;
                if (session.selection().syntax_node != last_selection_) {
                    last_selection_ = session.selection().syntax_node;
                    scroll_pending_ = (last_selection_ != kInvalidId);
                }
                ImGui::BeginChild("syntax_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
                std::vector<uint32_t> open_path =
                    syntaxAncestors(*tree, session.selection().syntax_node);
                drawSyntaxNode(*tree, sections[i].root, session.selection(), scroll_pending_,
                               open_path.empty() ? nullptr : &open_path);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

}  // namespace vide
