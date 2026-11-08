#include "ui/ui_util.h"

#include <cstdlib>

namespace vide {
namespace {

constexpr int kNoPoc = -9999;

// Reads the index in "name[idx]" (e.g. "ref_l0_poc[2]"). Returns -1 on failure.
int bracketIndex(const std::string& name) {
    const std::size_t lb = name.find('[');
    const std::size_t rb = name.find(']', lb);
    if (lb == std::string::npos || rb == std::string::npos) {
        return -1;
    }
    return std::atoi(name.substr(lb + 1, rb - lb - 1).c_str());
}

}  // namespace

const Block* findBlock(const BlockGrid* grid, uint32_t id) {
    if (grid == nullptr || id == kInvalidId) {
        return nullptr;
    }
    for (const Block& block : grid->blocks) {
        if (block.id == id) {
            return &block;
        }
    }
    return nullptr;
}

void collectRefPocs(const SyntaxTree& tree, std::vector<int>& l0, std::vector<int>& l1) {
    for (const SyntaxNode& n : tree.nodes) {
        int list = -1;
        if (n.name.rfind("ref_l0_poc[", 0) == 0) {
            list = 0;
        } else if (n.name.rfind("ref_l1_poc[", 0) == 0) {
            list = 1;
        }
        if (list < 0) {
            continue;
        }
        const int idx = bracketIndex(n.name);
        if (idx < 0) {
            continue;
        }
        std::vector<int>& vec = (list == 0) ? l0 : l1;
        if (static_cast<std::size_t>(idx) >= vec.size()) {
            vec.resize(idx + 1, kNoPoc);
        }
        vec[idx] = std::atoi(n.value.c_str());
    }
}

uint32_t syntaxNodeForBlock(const SyntaxTree& tree, uint32_t block_id) {
    if (block_id == kInvalidId) {
        return kInvalidId;
    }
    for (const SyntaxNode& node : tree.nodes) {
        if (node.block_id == block_id) {
            return node.id;
        }
    }
    return kInvalidId;
}

bool findSyntaxValue(const SyntaxTree& tree, uint32_t root, const char* name, std::string& out) {
    std::vector<uint32_t> stack;
    if (root != kInvalidId && root < tree.nodes.size()) {
        stack.push_back(root);
    } else {
        for (uint32_t i = 0; i < tree.nodes.size(); ++i) {
            stack.push_back(i);
        }
    }
    while (!stack.empty()) {
        const uint32_t id = stack.back();
        stack.pop_back();
        const SyntaxNode* n = tree.find(id);
        if (n == nullptr) {
            continue;
        }
        if (n->name == name) {
            out = n->value;
            return true;
        }
        for (uint32_t c : n->children) {
            stack.push_back(c);
        }
    }
    return false;
}

bool findSyntaxInt(const SyntaxTree& tree, uint32_t root, const char* name, int& out) {
    std::string value;
    if (!findSyntaxValue(tree, root, name, value)) {
        return false;
    }
    out = std::atoi(value.c_str());
    return true;
}

}  // namespace vide
