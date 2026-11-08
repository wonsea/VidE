#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "common/vpk.h"
#include "common/xxhash.h"
#include "core/cache_store.h"
#include "test_framework.h"

using namespace vide;

namespace {

DecodedImage makeReco(int w, int h, uint8_t value) {
    DecodedImage image;
    image.width = w;
    image.height = h;
    image.format = PixelFormat::RGBA8888;
    image.bit_depth = BitDepth::B8;
    image.data.assign(static_cast<std::size_t>(w) * h * 4, value);
    for (std::size_t i = 3; i < image.data.size(); i += 4) {
        image.data[i] = 255;
    }
    return image;
}

BlockGrid makeGrid(int w, int h, int qp) {
    BlockGrid grid;
    grid.width = w;
    grid.height = h;
    grid.ctu_size = 64;
    Block block;
    block.id = 0;
    block.rect = Rect{0, 0, 16, 16};
    block.type = BlockType::Intra;
    block.qp = qp;
    grid.blocks.push_back(block);
    return grid;
}

}  // namespace

int runVpkFileTests() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "vide_vpk_roundtrip_test.vpk";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    SourceFingerprint fp;
    fp.size = 123;
    fp.mtime = 456;
    fp.hash = 0xABCDEF;
    fp.valid = true;

    VpkWriter writer;
    CHECK(writer.begin(path.string(), CodecId::HEVC, fp, 320, 240, BitDepth::B8, 0).ok());

    // Append frame 0, then open a reader while the writer is still live.
    FrameMeta meta0;
    meta0.index = 0;
    meta0.poc = 0;
    meta0.is_keyframe = true;
    const BlockGrid grid0 = makeGrid(320, 240, 28);
    const DecodedImage reco0 = makeReco(320, 240, 10);
    CHECK(writer.appendFrame(meta0, nullptr, &grid0, &reco0).ok());

    VpkReader reader;
    CHECK(reader.open(path.string()).ok());
    CHECK(reader.frameCount() == 1);
    CHECK(!reader.complete());

    auto read_blocks = reader.blocks(0);
    CHECK(read_blocks.ok());
    if (read_blocks.ok() && !read_blocks.value().blocks.empty()) {
        CHECK(read_blocks.value().blocks.size() == 1);
        CHECK(read_blocks.value().blocks[0].qp == 28);
    }
    CHECK(read_blocks.value().width == 320);

    auto read_reco = reader.reco(0);
    CHECK(read_reco.ok());
    if (read_reco.ok() && !read_reco.value().data.empty()) {
        CHECK(read_reco.value().data.size() == static_cast<std::size_t>(320) * 240 * 4);
        CHECK(read_reco.value().data[0] == 10);
    }

    // Second frame + refresh of the partial pack.
    FrameMeta meta1;
    meta1.index = 1;
    meta1.poc = 1;
    const BlockGrid grid1 = makeGrid(320, 240, 33);
    const DecodedImage reco1 = makeReco(320, 240, 20);
    CHECK(writer.appendFrame(meta1, nullptr, &grid1, &reco1).ok());
    CHECK(reader.refresh().ok());
    CHECK(reader.frameCount() == 2);
    auto blocks1 = reader.blocks(1);
    CHECK(blocks1.ok());
    if (blocks1.ok() && !blocks1.value().blocks.empty()) {
        CHECK(blocks1.value().blocks[0].qp == 33);
    }

    // Frame 2 was never written: the request must fail cleanly.
    CHECK(!reader.blocks(2).ok());

    CHECK(writer.finish().ok());
    CHECK(reader.refresh().ok());
    CHECK(reader.complete());
    CHECK(reader.frameCount() == 2);
    CHECK(reader.header().width == 320);
    CHECK(reader.header().source_hash == 0xABCDEF);

    std::filesystem::remove(path, ec);
    return test::report("vpk file");
}

int runVpkCorruptTests() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    const std::filesystem::path path = dir / "vide_vpk_corrupt_test.vpk";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    // Missing file.
    CHECK(!VpkReader().open((dir / "vide_vpk_missing_xyz.vpk").string()).ok());

    // Empty file: header read must fail (no crash / no huge alloc).
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
    }
    CHECK(!VpkReader().open(path.string()).ok());

    // Garbage header (magic 0).
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        char junk[64] = {0};
        out.write(junk, sizeof(junk));
    }
    CHECK(!VpkReader().open(path.string()).ok());

    // Valid magic but absurd frame_count: must reject before allocating.
    {
        VpkHeader header;
        header.frame_count = 0x0FFFFFFFu;
        header.index_offset = static_cast<uint32_t>(sizeof(VpkHeader));
        header.data_offset = static_cast<uint32_t>(sizeof(VpkHeader));
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }
    CHECK(!VpkReader().open(path.string()).ok());

    std::filesystem::remove(path, ec);
    return test::report("vpk corrupt");
}

int runXxHashTests() {
    // Known vector: xxh64("", seed=0) == 0xEF46DB3751D8E999
    {
        XxHash64 h(0);
        CHECK(h.digest() == 0xEF46DB3751D8E999ULL);
    }
    // Stability + sensitivity.
    {
        XxHash64 a;
        a.update("hello", 5);
        XxHash64 b;
        b.update("hello", 5);
        CHECK(a.digest() == b.digest());

        XxHash64 c;
        c.update("hellp", 5);
        CHECK(a.digest() != c.digest());
    }
    // Chunked update equals one-shot.
    {
        XxHash64 one;
        one.update("0123456789abcdefghijklmnopqrstuvwxyz", 36);
        XxHash64 many;
        for (int i = 0; i < 36; ++i) {
            many.update("0123456789abcdefghijklmnopqrstuvwxyz" + i, 1);
        }
        CHECK(one.digest() == many.digest());
    }
    return test::report("xxhash");
}

int runVpkRoundTripTests() {
    SyntaxTree tree;
    tree.au_roots = {0};
    SyntaxNode root;
    root.id = 0;
    root.name = "sps";
    root.kind = SyntaxKind::Structure;
    root.bit_offset = 0;
    root.bit_length = 64;
    root.children = {1};
    tree.nodes.push_back(root);
    SyntaxNode field;
    field.id = 1;
    field.parent = 0;
    field.name = "profile_idc";
    field.value = "0x64";
    field.bit_offset = 0;
    field.bit_length = 8;
    tree.nodes.push_back(field);

    const auto syntax_blob = encodeSyntaxTree(tree);
    auto decoded = decodeSyntaxTree(syntax_blob.data(), syntax_blob.size());
    CHECK(decoded.ok());
    CHECK(decoded.value().nodes.size() == 2);
    CHECK(decoded.value().nodes[1].name == "profile_idc");
    CHECK(decoded.value().nodes[1].value == "0x64");
    CHECK(decoded.value().nodes[1].bit_length == 8);
    CHECK(decoded.value().au_roots.size() == 1);

    BlockGrid grid;
    grid.width = 320;
    grid.height = 240;
    grid.ctu_size = 64;
    Block block;
    block.id = 0;
    block.rect = Rect{0, 0, 8, 8};
    block.type = BlockType::Skip;
    block.qp = 28;
    block.qp_source = QpSource::Inherited;
    block.depth = 3;
    block.bit_offset = 123;
    block.bit_length = 45;
    grid.blocks.push_back(block);

    const auto block_blob = encodeBlockGrid(grid);
    auto decoded_grid = decodeBlockGrid(block_blob.data(), block_blob.size());
    CHECK(decoded_grid.ok());
    CHECK(decoded_grid.value().width == 320);
    CHECK(decoded_grid.value().blocks.size() == 1);
    CHECK(decoded_grid.value().blocks[0].type == BlockType::Skip);
    CHECK(decoded_grid.value().blocks[0].qp_source == QpSource::Inherited);
    CHECK(decoded_grid.value().blocks[0].qp == 28);
    CHECK(decoded_grid.value().blocks[0].rect.w == 8);
    CHECK(decoded_grid.value().blocks[0].bit_offset == 123);
    CHECK(decoded_grid.value().blocks[0].bit_length == 45);

    // Legacy pack compatibility: a pre-qp_source blob (40-byte records) must still
    // decode and fall back to a derived QpSource (regression: size detection).
    {
        std::vector<uint8_t> legacy;
        auto put32 = [&legacy](int32_t v) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
            legacy.insert(legacy.end(), p, p + 4);
        };
        auto putu32 = [&legacy](uint32_t v) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
            legacy.insert(legacy.end(), p, p + 4);
        };
        put32(320);  // width
        put32(240);  // height
        put32(64);   // ctu_size
        putu32(1);   // block count
        put32(0);    // x
        put32(0);    // y
        put32(8);    // w
        put32(8);    // h
        put32(2);    // depth
        put32(30);   // qp
        putu32(17);  // bits
        legacy.push_back(0);  // type Intra
        legacy.push_back(0);  // pred_mode
        legacy.push_back(0);  // ref0
        legacy.push_back(0xFF);  // ref1 = -1
        for (int k = 0; k < 8; ++k) {
            legacy.push_back(0);  // mv
        }
        CHECK(legacy.size() == 16 + 40);
        auto legacy_grid = decodeBlockGrid(legacy.data(), legacy.size());
        CHECK(legacy_grid.ok());
        CHECK(legacy_grid.value().blocks.size() == 1);
        CHECK(legacy_grid.value().blocks[0].qp == 30);
        CHECK(legacy_grid.value().blocks[0].bits == 17);
        CHECK(legacy_grid.value().blocks[0].qp_source == QpSource::Coded);
    }

    return test::report("vpk");
}
