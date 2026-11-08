#include "common/block_syntax.h"

#include "test_framework.h"

using namespace vide;

int runBlockSyntaxTests() {
    // Empty grid: nothing appended.
    {
        SyntaxTree tree;
        const std::size_t added = appendBlockSyntax(tree, BlockGrid{}, 0);
        CHECK(added == 0);
        CHECK(tree.au_roots.empty());
        CHECK(tree.nodes.empty());
    }

    // Two same-depth blocks: both attach directly under the tree root.
    {
        BlockGrid grid;
        grid.width = 320;
        grid.height = 240;
        grid.ctu_size = 64;

        Block intra;
        intra.id = 0;
        intra.rect = Rect{0, 0, 16, 16};
        intra.type = BlockType::Intra;
        intra.qp = 28;
        intra.bits = 255;
        grid.blocks.push_back(intra);

        Block inter;
        inter.id = 1;
        inter.rect = Rect{16, 0, 16, 16};
        inter.type = BlockType::Inter;
        inter.qp = 30;
        inter.bits = 42;
        inter.ref_idx[0] = 0;
        inter.mv[0][0] = 8;
        inter.mv[0][1] = -4;
        grid.blocks.push_back(inter);

        SyntaxTree tree;
        const std::size_t added = appendBlockSyntax(tree, grid, 5);
        CHECK(added == 2);
        CHECK(tree.au_roots.size() == 1);

        const uint32_t root_id = tree.au_roots[0];
        const SyntaxNode* root = tree.find(root_id);
        CHECK(root != nullptr);
        CHECK(root->name == "coding_tree");
        CHECK(root->kind == SyntaxKind::Structure);
        CHECK(root->au_index == 5);
        CHECK(root->children.size() == 2);

        const SyntaxNode* first = tree.find(root->children[0]);
        CHECK(first != nullptr);
        CHECK(first->block_id == 0);
        CHECK(first->au_index == 5);
        CHECK(first->kind == SyntaxKind::Derived);
        CHECK(first->value.find("Intra") != std::string::npos);

        const SyntaxNode* second = tree.find(root->children[1]);
        CHECK(second != nullptr);
        CHECK(second->block_id == 1);
        CHECK(second->value.find("Inter") != std::string::npos);
        CHECK(second->value.find("mv=") != std::string::npos);
    }

    // Depth nesting: a deeper block becomes a child of the smallest enclosing one.
    {
        BlockGrid grid;
        grid.ctu_size = 32;

        Block parent;
        parent.id = 0;
        parent.rect = Rect{0, 0, 32, 32};
        parent.depth = 0;
        parent.type = BlockType::Intra;
        grid.blocks.push_back(parent);

        Block child_a;
        child_a.id = 1;
        child_a.rect = Rect{0, 0, 16, 16};
        child_a.depth = 1;
        child_a.type = BlockType::Intra;
        grid.blocks.push_back(child_a);

        Block child_b;
        child_b.id = 2;
        child_b.rect = Rect{16, 0, 16, 16};
        child_b.depth = 1;
        child_b.type = BlockType::Inter;
        grid.blocks.push_back(child_b);

        SyntaxTree tree;
        const std::size_t added = appendBlockSyntax(tree, grid, 0);
        CHECK(added == 3);

        const SyntaxNode* root = tree.find(tree.au_roots[0]);
        CHECK(root != nullptr);
        CHECK(root->children.size() == 1);  // only the depth-0 block is a root

        const SyntaxNode* parent_node = tree.find(root->children[0]);
        CHECK(parent_node != nullptr);
        CHECK(parent_node->block_id == 0);
        CHECK(parent_node->children.size() == 2);

        const SyntaxNode* nested_a = tree.find(parent_node->children[0]);
        CHECK(nested_a != nullptr);
        CHECK(nested_a->block_id == 1);
        const SyntaxNode* nested_b = tree.find(parent_node->children[1]);
        CHECK(nested_b != nullptr);
        CHECK(nested_b->block_id == 2);
    }

    return test::report("block syntax");
}
