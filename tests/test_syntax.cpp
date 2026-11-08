#include "common/syntax.h"
#include "test_framework.h"

using namespace vide;

namespace {

SyntaxTree makeTree() {
    SyntaxTree tree;

    SyntaxNode root;
    root.id = 0;
    root.name = "seq_parameter_set_rbsp";
    root.kind = SyntaxKind::Structure;
    root.bit_offset = 0;
    root.bit_length = 128;
    root.children = {1, 2};
    tree.nodes.push_back(root);

    SyntaxNode field;
    field.id = 1;
    field.parent = 0;
    field.name = "profile_idc";
    field.value = "1";
    field.bit_offset = 0;
    field.bit_length = 8;
    tree.nodes.push_back(field);

    SyntaxNode derived;
    derived.id = 2;
    derived.parent = 0;
    derived.name = "chroma_format_idc";
    derived.value = "1";
    derived.kind = SyntaxKind::Derived;
    derived.bit_offset = 8;
    derived.bit_length = 8;
    tree.nodes.push_back(derived);

    tree.au_roots = {0};
    return tree;
}

}  // namespace

int runSyntaxTests() {
    SyntaxTree tree = makeTree();

    CHECK(!tree.empty());
    CHECK(tree.nodes.size() == 3);
    CHECK(tree.au_roots.size() == 1);
    CHECK(tree.au_roots[0] == 0);

    const SyntaxNode* root = tree.find(0);
    CHECK(root != nullptr);
    CHECK(root->kind == SyntaxKind::Structure);
    CHECK(root->children.size() == 2);

    const SyntaxNode* field = tree.find(1);
    CHECK(field != nullptr);
    CHECK(field->name == "profile_idc");
    CHECK(field->value == "1");
    CHECK(field->bit_offset == 0);
    CHECK(field->bit_length == 8);
    CHECK(field->parent == 0);

    const SyntaxNode* derived = tree.find(2);
    CHECK(derived != nullptr);
    CHECK(derived->kind == SyntaxKind::Derived);

    CHECK(tree.find(99) == nullptr);

    return test::report("syntax");
}
