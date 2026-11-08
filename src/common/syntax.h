// Generic bitstream syntax tree model.
//
// Every codec adapter converts its codec-specific parse result into this tree
// so that the UI only ever deals with one representation. Recording bit ranges
// enables bi-directional navigation between syntax elements and picture regions.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/types.h"

namespace vide {

enum class SyntaxKind {
    Field = 0,   // a single syntax element (e.g. slice_qp_delta)
    Structure,   // a named aggregation (e.g. seq_parameter_set_rbsp)
    Array,       // repeated elements
    Reserved,    // reserved / stuffing bits
    Derived,     // value computed from other elements
};

struct SyntaxNode {
    uint32_t id = kInvalidId;
    uint32_t parent = kInvalidId;
    std::string name;
    std::string value;                  // pre-formatted for display
    SyntaxKind kind = SyntaxKind::Field;
    int64_t bit_offset = -1;           // relative to the owning access unit
    int64_t bit_length = -1;           // -1 when variable / unknown
    uint32_t au_index = 0;             // owning access unit
    uint32_t block_id = kInvalidId;    // linked block, when applicable
    std::vector<uint32_t> children;
};

struct SyntaxTree {
    std::vector<SyntaxNode> nodes;      // nodes[0] is the root
    std::vector<uint32_t> au_roots;     // one root per access unit

    bool empty() const { return nodes.empty(); }

    const SyntaxNode* find(uint32_t id) const {
        return id < nodes.size() ? &nodes[id] : nullptr;
    }
};

}  // namespace vide
