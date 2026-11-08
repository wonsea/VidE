#include "common/vpk.h"

#include <cstring>

namespace vide {
namespace {

void appendU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&v),
               reinterpret_cast<const uint8_t*>(&v) + 4);
}
void appendU64(std::vector<uint8_t>& out, uint64_t v) {
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&v),
               reinterpret_cast<const uint8_t*>(&v) + 8);
}
void appendI32(std::vector<uint8_t>& out, int32_t v) {
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&v),
               reinterpret_cast<const uint8_t*>(&v) + 4);
}
void appendString(std::vector<uint8_t>& out, const std::string& s) {
    appendU32(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

struct Reader {
    const uint8_t* p;
    std::size_t size;
    std::size_t pos = 0;
    bool ok = true;

    bool take(std::size_t n, const uint8_t*& out) {
        if (!ok || pos + n > size) {
            ok = false;
            return false;
        }
        out = p + pos;
        pos += n;
        return true;
    }
    uint8_t u8() {
        const uint8_t* q = nullptr;
        if (!take(1, q)) return 0;
        return *q;
    }
    uint32_t u32() {
        const uint8_t* q = nullptr;
        if (!take(4, q)) return 0;
        uint32_t v;
        std::memcpy(&v, q, 4);
        return v;
    }
    int32_t i32() {
        const uint8_t* q = nullptr;
        if (!take(4, q)) return 0;
        int32_t v;
        std::memcpy(&v, q, 4);
        return v;
    }
    std::string str() {
        const uint32_t n = u32();
        const uint8_t* q = nullptr;
        if (!take(n, q)) return {};
        return std::string(reinterpret_cast<const char*>(q), n);
    }
};

}  // namespace

std::vector<uint8_t> encodeSyntaxTree(const SyntaxTree& tree) {
    std::vector<uint8_t> out;
    const uint32_t count = static_cast<uint32_t>(tree.nodes.size());
    appendU32(out, count);
    for (const SyntaxNode& node : tree.nodes) {
        appendU32(out, node.id);
        appendU32(out, node.parent);
        appendString(out, node.name);
        appendString(out, node.value);
        appendU8(out, static_cast<uint8_t>(node.kind));
        appendU64(out, static_cast<uint64_t>(node.bit_offset));
        appendU64(out, static_cast<uint64_t>(node.bit_length));
        appendU32(out, node.au_index);
        appendU32(out, node.block_id);
        appendU32(out, static_cast<uint32_t>(node.children.size()));
        for (uint32_t child : node.children) {
            appendU32(out, child);
        }
    }
    appendU32(out, static_cast<uint32_t>(tree.au_roots.size()));
    for (uint32_t root : tree.au_roots) {
        appendU32(out, root);
    }
    return out;
}

Result<SyntaxTree> decodeSyntaxTree(const uint8_t* data, std::size_t size) {
    Reader r{data, size};
    SyntaxTree tree;
    const uint32_t count = r.u32();
    tree.nodes.reserve(count);
    for (uint32_t i = 0; i < count && r.ok; ++i) {
        SyntaxNode node;
        node.id = r.u32();
        node.parent = r.u32();
        node.name = r.str();
        node.value = r.str();
        node.kind = static_cast<SyntaxKind>(r.u8());
        node.bit_offset = static_cast<int64_t>(r.u32() | (static_cast<uint64_t>(r.u32()) << 32));
        node.bit_length = static_cast<int64_t>(r.u32() | (static_cast<uint64_t>(r.u32()) << 32));
        node.au_index = r.u32();
        node.block_id = r.u32();
        const uint32_t children = r.u32();
        node.children.reserve(children);
        for (uint32_t c = 0; c < children && r.ok; ++c) {
            node.children.push_back(r.u32());
        }
        tree.nodes.push_back(std::move(node));
    }
    const uint32_t roots = r.u32();
    for (uint32_t i = 0; i < roots && r.ok; ++i) {
        tree.au_roots.push_back(r.u32());
    }
    if (!r.ok) {
        return Status::error(StatusCode::ParseError, "corrupt syntax blob");
    }
    return tree;
}

std::vector<uint8_t> encodeBlockGrid(const BlockGrid& grid) {
    std::vector<uint8_t> out;
    appendI32(out, grid.width);
    appendI32(out, grid.height);
    appendI32(out, grid.ctu_size);
    appendU32(out, static_cast<uint32_t>(grid.blocks.size()));
    for (const Block& block : grid.blocks) {
        appendI32(out, block.rect.x);
        appendI32(out, block.rect.y);
        appendI32(out, block.rect.w);
        appendI32(out, block.rect.h);
        appendI32(out, block.depth);
        appendI32(out, block.qp);
        appendU32(out, block.bits);
        appendU8(out, static_cast<uint8_t>(block.type));
        appendU8(out, static_cast<uint8_t>(block.pred_mode));
        appendU8(out, static_cast<uint8_t>(block.qp_source));
        appendU8(out, static_cast<uint8_t>(block.ref_idx[0]));
        appendU8(out, static_cast<uint8_t>(block.ref_idx[1]));
        // motion vectors (reserved but serialised for future use)
        for (int list = 0; list < 2; ++list) {
            for (int xy = 0; xy < 2; ++xy) {
                out.insert(out.end(), reinterpret_cast<const uint8_t*>(&block.mv[list][xy]),
                           reinterpret_cast<const uint8_t*>(&block.mv[list][xy]) + 2);
            }
        }
        // v6: per-block bit range in the elementary stream (0/0 = unknown).
        appendU32(out, block.bit_offset);
        appendU32(out, block.bit_length);
        // v7: global VCL slice ordinal (AVC multi-slice pairing).
        appendU32(out, block.slice_index);
    }
    return out;
}

Result<BlockGrid> decodeBlockGrid(const uint8_t* data, std::size_t size) {
    Reader r{data, size};
    BlockGrid grid;
    grid.width = r.i32();
    grid.height = r.i32();
    grid.ctu_size = r.i32();
    const uint32_t count = r.u32();
    // Detect the legacy per-block record size (no qp_source byte) so old packs
    // still decode. Legacy record = x,y,w,h (16) + depth (4) + qp (4) + bits (4)
    // + type + pred + ref0 + ref1 (4) + MV 2*2*2 (8) = 40 bytes; v5 adds one
    // qp_source byte = 41; v6 adds bit_offset + bit_length (8) = 49; v7 adds
    // slice_index (4) = 53.
    constexpr std::size_t kHeaderBytes = 4 + 4 + 4 + 4;
    const std::size_t per_record =
        count > 0 && size >= kHeaderBytes ? (size - kHeaderBytes) / count : 0;
    bool has_qp_source = per_record >= 41;
    bool has_bit_range = per_record >= 49;
    bool has_slice_index = per_record >= 53;
    grid.blocks.reserve(count);
    for (uint32_t i = 0; i < count && r.ok; ++i) {
        Block block;
        block.rect = Rect{r.i32(), r.i32(), r.i32(), r.i32()};
        block.depth = r.i32();
        block.qp = r.i32();
        block.bits = r.u32();
        block.type = static_cast<BlockType>(r.u8());
        block.pred_mode = static_cast<PredictionMode>(r.u8());
        if (has_qp_source) {
            block.qp_source = static_cast<QpSource>(r.u8());
        } else {
            block.qp_source = block.qp >= 0 ? QpSource::Coded : QpSource::Na;
        }
        block.ref_idx[0] = static_cast<int8_t>(r.u8());
        block.ref_idx[1] = static_cast<int8_t>(r.u8());
        for (int list = 0; list < 2 && r.ok; ++list) {
            for (int xy = 0; xy < 2 && r.ok; ++xy) {
                const uint8_t* q = nullptr;
                if (!r.take(2, q)) break;
                std::memcpy(&block.mv[list][xy], q, 2);
            }
        }
        if (has_bit_range) {
            block.bit_offset = r.u32();
            block.bit_length = r.u32();
        }
        if (has_slice_index) {
            block.slice_index = r.u32();
        }
        block.id = i;
        grid.blocks.push_back(block);
    }
    if (!r.ok) {
        return Status::error(StatusCode::ParseError, "corrupt block blob");
    }
    return grid;
}

}  // namespace vide
