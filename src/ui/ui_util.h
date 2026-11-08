// Small shared helpers used by several UI panels (block/syntax lookup, reference
// POC extraction). Keeping them here avoids copy-pasted scans across panels.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/frame.h"
#include "common/syntax.h"
#include "common/types.h"

namespace vide {

// Returns the block with `id` in `grid`, or nullptr.
const Block* findBlock(const BlockGrid* grid, uint32_t id);

// Extracts the frame-level reference POCs the helpers emit as
// `ref_l0_poc[i]` / `ref_l1_poc[i]`. Missing entries are left at the sentinel
// -9999 so callers can distinguish "no reference" from POC 0.
void collectRefPocs(const SyntaxTree& tree, std::vector<int>& l0, std::vector<int>& l1);

// Returns the id of the syntax node linked to `block_id`, or kInvalidId.
uint32_t syntaxNodeForBlock(const SyntaxTree& tree, uint32_t block_id);

// Finds a field named `name` anywhere under `root` (or the whole tree when
// `root` is kInvalidId) and parses its integer value. Returns false when absent.
bool findSyntaxInt(const SyntaxTree& tree, uint32_t root, const char* name, int& out);

// Same as findSyntaxInt but returns the raw string value.
bool findSyntaxValue(const SyntaxTree& tree, uint32_t root, const char* name, std::string& out);

}  // namespace vide
