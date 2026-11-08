// Derived block-level syntax.
//
// Codecs whose helper exports a real CU/PU/TU tree (HEVC) do not need this.
// For the others (VVC/AVC/VP9/AV1) this module appends a hierarchical
// "coding_tree" subtree to a frame's syntax tree: one node per coding block,
// nested by decoded depth + spatial containment, each tagged with its
// `block_id` so the UI can map a syntax node to the corresponding picture
// region (and back), which is what R1/S-3 require. Block field values come
// straight from the cache; the parent/child shape is reconstructed from the
// decoded depth and geometry.
#pragma once

#include <cstddef>

#include "common/frame.h"
#include "common/syntax.h"

namespace vide {

// Appends a hierarchical `coding_tree` root to `tree` with one derived node per
// block in `grid`, nested by depth/containment. Each block node carries
// `block_id` and `au_index`. Returns the number of block nodes appended (0 when
// the grid is empty).
std::size_t appendBlockSyntax(SyntaxTree& tree, const BlockGrid& grid, uint32_t au_index = 0);

}  // namespace vide
