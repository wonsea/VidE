#include "common/bitreader.h"
#include "common/syntax_builder.h"
#include "test_framework.h"

using namespace vide;

int runBitReaderTests() {
    // 1010 0110
    const uint8_t bits0[] = {0xA6};
    BitReader reader0(bits0, sizeof(bits0));
    CHECK(reader0.readBits(3) == 0x5);
    CHECK(reader0.readBits(5) == 0x6);
    CHECK(reader0.good());

    // ue(3) = 00100, se(-2) = 00101  -> 0010000101 000000
    const uint8_t bits1[] = {0x21, 0x40};
    BitReader reader1(bits1, sizeof(bits1));
    CHECK(reader1.readUE() == 3u);
    CHECK(reader1.readSE() == -2);

    BitReader reader2(bits1, sizeof(bits1));
    const uint32_t first = reader2.readBits(3);
    CHECK(first == 1u);

    return test::report("bitreader");
}

int runSyntaxBuilderTests() {
    SyntaxTree tree;
    SyntaxBuilder builder(tree);

    const uint32_t root = builder.addRoot("sps");
    const uint32_t field = builder.addField(root, "profile_idc", "0x64", 0, 8);
    builder.addField(root, "level_idc", "13", 24, 8);

    CHECK(tree.au_roots.size() == 1);
    CHECK(tree.au_roots[0] == root);
    CHECK(tree.nodes.size() == 3);

    const SyntaxNode* root_node = tree.find(root);
    CHECK(root_node != nullptr);
    CHECK(root_node->kind == SyntaxKind::Structure);
    CHECK(root_node->children.size() == 2);
    CHECK(root_node->children[0] == field);

    const SyntaxNode* field_node = tree.find(field);
    CHECK(field_node != nullptr);
    CHECK(field_node->name == "profile_idc");
    CHECK(field_node->value == "0x64");
    CHECK(field_node->bit_offset == 0);
    CHECK(field_node->bit_length == 8);
    CHECK(field_node->parent == root);

    return test::report("syntax builder");
}
