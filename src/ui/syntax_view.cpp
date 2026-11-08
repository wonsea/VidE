#include "ui/syntax_view.h"

#include "core/session.h"

#include <imgui.h>

namespace vide {
namespace {

std::string structureLabel(const std::string& name) {
    if (name.find("seq_parameter_set") != std::string::npos || name == "sps") {
        return "SPS";
    }
    if (name.find("pic_parameter_set") != std::string::npos || name == "pps") {
        return "PPS";
    }
    if (name.find("video_parameter_set") != std::string::npos || name == "vps") {
        return "VPS";
    }
    if (name.find("slice_header") != std::string::npos || name == "slice") {
        return "Header";
    }
    if (name.find("picture") != std::string::npos || name.find("frame") != std::string::npos) {
        return "Header";
    }
    return name;
}

}  // namespace

bool isCodingTreeRoot(const std::string& name) {
    return name == "coding_tree" || name == "coding_blocks";
}

namespace {

// True for VPS/SPS/PPS-style containers that deserve their own tab only when a
// parent root aggregates several of them.
bool isParameterSetName(const std::string& name) {
    return name.find("parameter_set") != std::string::npos || name == "vps" ||
           name == "sps" || name == "pps" || name == "slice_header";
}

}  // namespace

std::vector<SyntaxSection> collectSyntaxSections(const SyntaxTree& tree) {
    std::vector<SyntaxSection> sections;
    for (uint32_t root : tree.au_roots) {
        const SyntaxNode* node = tree.find(root);
        if (node == nullptr) {
            continue;
        }
        if (isCodingTreeRoot(node->name)) {
            // The per-CTU coding tree is shown in the Block Info panel's
            // "CTU syntax" tab; skip it here to avoid duplicating that list.
            continue;
        }
        if (node->children.empty()) {
            continue;  // empty wrapper root (no parameter sets / headers)
        }

        // Split off child structures (parameter sets / headers / containers such
        // as VP9's ivf_header). A root that only wraps structure children (e.g.
        // the container-metadata root) is not shown as a tab itself.
        std::vector<uint32_t> param_sets;
        bool has_direct_fields = false;
        for (uint32_t child : node->children) {
            const SyntaxNode* c = tree.find(child);
            if (c == nullptr) {
                continue;
            }
            if (c->kind == SyntaxKind::Structure) {
                if (isParameterSetName(c->name)) {
                    param_sets.push_back(child);
                }
            } else {
                has_direct_fields = true;
            }
        }

        // If the root directly carries syntax fields (HEVC/VVC NAL roots, JPEG/
        // PNG headers) it is a real tab; its nested structures stay inside it.
        if (has_direct_fields) {
            sections.push_back({structureLabel(node->name), root});
            continue;
        }

        // Otherwise the root is a wrapper. If it aggregates parameter sets,
        // promote them to their own tabs; otherwise show the wrapper as a single
        // tab with its structures nested (VP9/AV1 IVF headers + frames, ...).
        if (!param_sets.empty()) {
            for (uint32_t child : param_sets) {
                sections.push_back({structureLabel(tree.find(child)->name), child});
            }
        } else {
            sections.push_back({structureLabel(node->name), root});
        }
    }
    return sections;
}

uint32_t findCtuNode(const SyntaxTree& tree, uint32_t block_id) {
    if (block_id == kInvalidId) {
        return kInvalidId;
    }
    uint32_t leaf = kInvalidId;
    for (const SyntaxNode& node : tree.nodes) {
        if (node.block_id == block_id) {
            leaf = node.id;
            break;
        }
    }
    if (leaf == kInvalidId) {
        return kInvalidId;
    }
    // Walk up to the node whose parent is the coding-tree root: that node is the
    // CTU/partition region the leaf belongs to.
    uint32_t current = leaf;
    uint32_t child_of_root = kInvalidId;
    while (current != kInvalidId) {
        const SyntaxNode* node = tree.find(current);
        if (node == nullptr) {
            return kInvalidId;
        }
        if (isCodingTreeRoot(node->name)) {
            return child_of_root;
        }
        child_of_root = current;
        current = node->parent;
    }
    return kInvalidId;
}

void drawSyntaxNode(const SyntaxTree& tree, uint32_t id, Selection& selection,
                    bool& scroll_pending, const std::vector<uint32_t>* prefer_open) {
    const SyntaxNode* node = tree.find(id);
    if (node == nullptr) {
        return;
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node->children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (selection.syntax_node == id) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (prefer_open != nullptr) {
        for (uint32_t open_id : *prefer_open) {
            if (open_id == id) {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
                break;
            }
        }
    }

    ImGui::PushID(static_cast<int>(id));
    bool open = ImGui::TreeNodeEx(node->name.c_str(), flags, "%s", node->name.c_str());
    if (node->bit_length >= 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("= %s  [bits %lld..%lld]", node->value.c_str(),
                            static_cast<long long>(node->bit_offset),
                            static_cast<long long>(node->bit_offset + node->bit_length - 1));
    } else if (!node->value.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("= %s", node->value.c_str());
    }

    if (ImGui::IsItemClicked()) {
        selection.syntax_node = id;
        if (node->block_id != kInvalidId) {
            selection.block = node->block_id;
        }
    }
    if (ImGui::IsItemHovered() && node->block_id != kInvalidId) {
        ImGui::SetTooltip("block #%u", node->block_id);
    }

    if (scroll_pending && selection.syntax_node == id) {
        if (!ImGui::IsItemVisible()) {
            ImGui::SetScrollHereY(0.5f);
        }
        scroll_pending = false;
    }

    if (open && !node->children.empty()) {
        for (uint32_t child : node->children) {
            drawSyntaxNode(tree, child, selection, scroll_pending, prefer_open);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

std::vector<uint32_t> syntaxAncestors(const SyntaxTree& tree, uint32_t node) {
    std::vector<uint32_t> out;
    uint32_t cur = node;
    while (cur != kInvalidId && cur < tree.nodes.size()) {
        out.push_back(cur);
        cur = tree.nodes[cur].parent;
    }
    return out;
}

}  // namespace vide
