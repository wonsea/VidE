#include "common/syntax_json.h"

#include <cstdio>
#include <sstream>

namespace vide {
namespace {

const char* kindName(SyntaxKind kind) {
    switch (kind) {
        case SyntaxKind::Field: return "field";
        case SyntaxKind::Structure: return "structure";
        case SyntaxKind::Array: return "array";
        case SyntaxKind::Reserved: return "reserved";
        case SyntaxKind::Derived: return "derived";
    }
    return "unknown";
}

std::string escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

void writeNode(std::ostringstream& os, const SyntaxTree& tree, uint32_t id,
               const std::string& indent) {
    const SyntaxNode* node = tree.find(id);
    if (node == nullptr) {
        os << "null";
        return;
    }

    os << "{\n";
    const std::string child_indent = indent + "  ";
    os << child_indent << "\"name\": \"" << escape(node->name) << "\",\n";
    os << child_indent << "\"value\": \"" << escape(node->value) << "\",\n";
    os << child_indent << "\"kind\": \"" << kindName(node->kind) << "\",\n";
    os << child_indent << "\"bitOffset\": " << node->bit_offset << ",\n";
    os << child_indent << "\"bitLength\": " << node->bit_length;

    if (node->block_id != kInvalidId) {
        os << ",\n" << child_indent << "\"blockId\": " << node->block_id;
    }

    if (!node->children.empty()) {
        os << ",\n" << child_indent << "\"children\": [\n";
        for (std::size_t i = 0; i < node->children.size(); ++i) {
            os << child_indent << "  ";
            writeNode(os, tree, node->children[i], child_indent + "  ");
            if (i + 1 < node->children.size()) {
                os << ",";
            }
            os << "\n";
        }
        os << child_indent << "]";
    }

    os << "\n" << indent << "}";
}

}  // namespace

std::string syntaxTreeToJson(const SyntaxTree& tree) {
    std::ostringstream os;
    os << "{\n  \"accessUnits\": [\n";
    for (std::size_t i = 0; i < tree.au_roots.size(); ++i) {
        os << "    ";
        writeNode(os, tree, tree.au_roots[i], "    ");
        if (i + 1 < tree.au_roots.size()) {
            os << ",";
        }
        os << "\n";
    }
    os << "  ]\n}\n";
    return os.str();
}

}  // namespace vide
