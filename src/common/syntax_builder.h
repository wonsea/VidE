// Small helper for incrementally building a SyntaxTree.
#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "common/syntax.h"

namespace vide {

class SyntaxBuilder {
public:
    explicit SyntaxBuilder(SyntaxTree& tree) : tree_(tree) {}

    uint32_t addRoot(std::string name, SyntaxKind kind = SyntaxKind::Structure) {
        const uint32_t id = addNode(kInvalidId, std::move(name), std::string{}, kind, -1, -1);
        tree_.au_roots.push_back(id);
        return id;
    }

    uint32_t addField(uint32_t parent, std::string name, std::string value,
                      int64_t bit_offset, int64_t bit_length,
                      SyntaxKind kind = SyntaxKind::Field) {
        return addNode(parent, std::move(name), std::move(value), kind, bit_offset,
                       bit_length);
    }

    uint32_t addStructure(uint32_t parent, std::string name, int64_t bit_offset = -1,
                          int64_t bit_length = -1) {
        return addNode(parent, std::move(name), std::string{}, SyntaxKind::Structure,
                       bit_offset, bit_length);
    }

    uint32_t addNode(uint32_t parent, std::string name, std::string value, SyntaxKind kind,
                     int64_t bit_offset, int64_t bit_length) {
        SyntaxNode node;
        node.id = static_cast<uint32_t>(tree_.nodes.size());
        node.parent = parent;
        node.name = std::move(name);
        node.value = std::move(value);
        node.kind = kind;
        node.bit_offset = bit_offset;
        node.bit_length = bit_length;

        const uint32_t id = node.id;
        tree_.nodes.push_back(std::move(node));

        if (parent != kInvalidId && parent < tree_.nodes.size()) {
            tree_.nodes[parent].children.push_back(id);
        }
        return id;
    }

private:
    SyntaxTree& tree_;
};

}  // namespace vide
