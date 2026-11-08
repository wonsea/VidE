// Shared helpers for rendering a SyntaxTree across the syntax / CTU panels.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/syntax.h"

namespace vide {

struct Selection;

// One tab in the syntax explorer: a label plus the node to render as its root.
struct SyntaxSection {
    std::string label;
    uint32_t root = kInvalidId;
};

// Splits a frame's syntax tree into sections (VPS / SPS / PPS / Slice / Blocks /
// Header / ...), preserving order and keeping only non-empty sections.
std::vector<SyntaxSection> collectSyntaxSections(const SyntaxTree& tree);

// Renders one syntax node (and its subtree). Clicking a node that carries a
// block link updates `selection.block`/`selection.ctu`; `scroll_pending` scrolls
// the selected node into view once. `prefer_open` (may be null) lists node ids to
// force-expand by default (e.g. the ancestors of the selected node).
void drawSyntaxNode(const SyntaxTree& tree, uint32_t id, Selection& selection,
                    bool& scroll_pending, const std::vector<uint32_t>* prefer_open = nullptr);

// Returns `node` and all its ancestors (id, parent, ... up to the root).
std::vector<uint32_t> syntaxAncestors(const SyntaxTree& tree, uint32_t node);

// Returns the CTU-level node (direct child of the "coding_tree" root) whose
// subtree contains the leaf linked to `block_id`, or kInvalidId.
uint32_t findCtuNode(const SyntaxTree& tree, uint32_t block_id);

// True when the label of a node names a coding/partition tree root.
bool isCodingTreeRoot(const std::string& name);

}  // namespace vide
