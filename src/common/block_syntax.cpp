#include "common/block_syntax.h"

#include <functional>
#include <string>
#include <vector>

#include "common/syntax_builder.h"

namespace vide {
namespace {

std::string blockDescription(const Block& block) {
    std::string text = toString(block.type);
    text += " ";
    text += std::to_string(block.rect.w) + "x" + std::to_string(block.rect.h);
    text += " @(" + std::to_string(block.rect.x) + "," + std::to_string(block.rect.y) + ")";
    text += " qp=" + std::to_string(block.qp);
    text += " bits=" + std::to_string(block.bits);
    if (block.type == BlockType::Inter || block.type == BlockType::Skip) {
        text += " mv=(" + std::to_string(block.mv[0][0]) + "," + std::to_string(block.mv[0][1]) +
                ")";
    }
    return text;
}

bool contains(const Rect& outer, const Rect& inner) {
    return outer.x <= inner.x && outer.y <= inner.y && outer.right() >= inner.right() &&
           outer.bottom() >= inner.bottom();
}

// Parent = the smallest-area, deepest block that strictly contains this one.
int findParent(const BlockGrid& grid, std::size_t index) {
    const Block& child = grid.blocks[index];
    int best = -1;
    long long best_area = -1;
    for (std::size_t j = 0; j < grid.blocks.size(); ++j) {
        if (j == index) {
            continue;
        }
        const Block& candidate = grid.blocks[j];
        if (candidate.depth >= child.depth || !contains(candidate.rect, child.rect)) {
            continue;
        }
        const long long area =
            static_cast<long long>(candidate.rect.w) * candidate.rect.h;
        const int best_depth = (best < 0) ? -1 : grid.blocks[best].depth;
        if (best < 0 || area < best_area || (area == best_area && candidate.depth > best_depth)) {
            best = static_cast<int>(j);
            best_area = area;
        }
    }
    return best;
}

}  // namespace

std::size_t appendBlockSyntax(SyntaxTree& tree, const BlockGrid& grid, uint32_t au_index) {
    if (grid.blocks.empty()) {
        return 0;
    }
    const std::size_t count = grid.blocks.size();
    SyntaxBuilder builder(tree);
    const uint32_t root = builder.addRoot("coding_tree");
    tree.nodes[root].au_index = au_index;
    tree.nodes[root].value = std::to_string(count) + " blocks (" +
                             std::to_string(grid.ctu_size) + "-px CTU)";

    // Nest blocks by decoded depth + spatial containment. This reflects the
    // partition structure the reference decoder produced (the block fields are
    // real; the parent/child shape is reconstructed from depth and geometry).
    // Guarded by a size cap so a very large frame cannot trigger an O(n^2) scan.
    constexpr std::size_t kMaxNestingBlocks = 20000;
    std::vector<std::vector<int>> children(count);
    std::vector<int> roots;
    if (count <= kMaxNestingBlocks) {
        for (std::size_t i = 0; i < count; ++i) {
            const int parent = findParent(grid, i);
            if (parent < 0) {
                roots.push_back(static_cast<int>(i));
            } else {
                children[parent].push_back(static_cast<int>(i));
            }
        }
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            roots.push_back(static_cast<int>(i));
        }
    }

    std::size_t added = 0;
    std::function<void(int, uint32_t)> add = [&](int index, uint32_t parent_node) {
        const Block& block = grid.blocks[static_cast<std::size_t>(index)];
        const uint32_t node = builder.addField(parent_node, "coding_unit",
                                               blockDescription(block), -1, -1,
                                               SyntaxKind::Derived);
        tree.nodes[node].block_id = block.id;
        tree.nodes[node].au_index = au_index;
        ++added;
        for (int child : children[static_cast<std::size_t>(index)]) {
            add(child, node);
        }
    };
    for (int r : roots) {
        add(r, root);
    }
    return added;
}

}  // namespace vide
