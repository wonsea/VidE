// VidE reference-decoder helper: AV1 via libaom.
//
// libaom's C headers clash with C++, so all libaom access goes through the C
// bridge (av1_bridge.*). Block/reco data comes from the VidE capture hook
// compiled into libaom.
//
// Usage: vide_av1_helper <stream.obu|stream.ivf> <out.vpk> [hash size mtime]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "av1_bridge.h"

#include "common/helper_common.h"
#include "common/syntax_builder.h"
#include "common/vpk.h"
#include "core/cache_store.h"

namespace {

vide::BlockGrid buildBlocks(const Av1BlockInfoC* mbs, int count) {
    vide::BlockGrid grid;
    for (int i = 0; i < count; ++i) {
        const Av1BlockInfoC& mb = mbs[i];
        vide::Block block;
        block.id = static_cast<uint32_t>(i);
        block.rect = vide::Rect{mb.x, mb.y, mb.w, mb.h};
        block.depth = 0;
        block.qp = mb.qp;
        block.bits = mb.bits;
        if (mb.bit_length > 0) {
            block.bit_offset = mb.bit_offset;
            block.bit_length = mb.bit_length;
        }
        switch (mb.block_type) {
            case 0: block.type = vide::BlockType::Intra; break;
            case 1: block.type = vide::BlockType::Inter; break;
            case 2: block.type = vide::BlockType::Skip; break;
            default: block.type = vide::BlockType::Unknown; break;
        }
        block.pred_mode = vide::PredictionMode::Unknown;
        block.qp_source = block.qp < 0
                              ? vide::QpSource::Na
                              : (mb.qp_coded ? vide::QpSource::Coded
                                             : vide::QpSource::Inherited);
        block.mv[0][0] = static_cast<int16_t>(mb.mv0x);
        block.mv[0][1] = static_cast<int16_t>(mb.mv0y);
        block.mv[1][0] = static_cast<int16_t>(mb.mv1x);
        block.mv[1][1] = static_cast<int16_t>(mb.mv1y);
        block.ref_idx[0] = static_cast<int8_t>(mb.ref0);
        block.ref_idx[1] = static_cast<int8_t>(mb.ref1);
        grid.blocks.push_back(block);
    }
    return grid;
}

vide::DecodedImage planesToRgba(const uint8_t* y, const uint8_t* u, const uint8_t* v, int width,
                                int height, int cw, int ch, int bit_depth) {
    vide::DecodedImage image;
    if (y == nullptr || width <= 0 || height <= 0) {
        return image;
    }
    const int bytes = bit_depth > 8 ? 2 : 1;
    image.width = width;
    image.height = height;
    image.format = vide::PixelFormat::RGBA8888;
    image.bit_depth = vide::BitDepth::B8;
    image.data.resize(static_cast<std::size_t>(width) * height * 4);

    auto sample = [bytes](const uint8_t* p, int i) -> int {
        if (bytes == 2) {
            const uint16_t* p16 = reinterpret_cast<const uint16_t*>(p);
            return p16[i];
        }
        return p[i];
    };
    const int shift = bit_depth > 8 ? (bit_depth - 8) : 0;
    auto clamp8 = [](int val) {
        return static_cast<uint8_t>(val < 0 ? 0 : (val > 255 ? 255 : val));
    };
    for (int yy = 0; yy < height; ++yy) {
        for (int xx = 0; xx < width; ++xx) {
            const int Y = sample(y, yy * width + xx) >> shift;
            const int U = sample(u, (yy / 2) * cw + (xx / 2)) >> shift;
            const int V = sample(v, (yy / 2) * cw + (xx / 2)) >> shift;
            const int c = Y - 16;
            const int d = U - 128;
            const int e = V - 128;
            const int scaled = c < 0 ? 0 : c;
            std::size_t idx = (static_cast<std::size_t>(yy) * width + xx) * 4;
            image.data[idx + 0] = clamp8((298 * scaled + 409 * e + 128) >> 8);
            image.data[idx + 1] = clamp8((298 * scaled - 100 * d - 208 * e + 128) >> 8);
            image.data[idx + 2] = clamp8((298 * scaled + 516 * d + 128) >> 8);
            image.data[idx + 3] = 255;
        }
    }
    return image;
}

// Converts captured prediction / residual int16 planes to RGBA, right-shifting
// to 8-bit. Residual luma is rendered as a 128-biased grey image.
vide::DecodedImage planes16ToRgba(const int16_t* y, const int16_t* u, const int16_t* v,
                                  int width, int height, int cw, int ch, int bit_depth,
                                  bool is_pred) {
    vide::DecodedImage image;
    if (y == nullptr || width <= 0 || height <= 0) {
        return image;
    }
    const int shift = bit_depth > 8 ? (bit_depth - 8) : 0;
    image.width = width;
    image.height = height;
    image.format = vide::PixelFormat::RGBA8888;
    image.bit_depth = vide::BitDepth::B8;
    image.data.resize(static_cast<std::size_t>(width) * height * 4);
    auto clamp8 = [](int val) {
        return static_cast<uint8_t>(val < 0 ? 0 : (val > 255 ? 255 : val));
    };
    const bool has_chroma = (u != nullptr && v != nullptr && cw > 0 && ch > 0);
    for (int yy = 0; yy < height; ++yy) {
        for (int xx = 0; xx < width; ++xx) {
            const std::size_t idx = (static_cast<std::size_t>(yy) * width + xx) * 4;
            const int Y = y[static_cast<std::size_t>(yy) * width + xx] >> shift;
            if (!is_pred) {
                const uint8_t g = clamp8(128 + Y);
                image.data[idx + 0] = g;
                image.data[idx + 1] = g;
                image.data[idx + 2] = g;
                image.data[idx + 3] = 255;
                continue;
            }
            int U = 128;
            int V = 128;
            if (has_chroma) {
                const std::size_t ci = static_cast<std::size_t>(yy / 2) * cw + (xx / 2);
                U = u[ci] >> shift;
                V = v[ci] >> shift;
            }
            const int c = Y - 16;
            const int d = U - 128;
            const int e = V - 128;
            const int scaled = c < 0 ? 0 : c;
            image.data[idx + 0] = clamp8((298 * scaled + 409 * e + 128) >> 8);
            image.data[idx + 1] = clamp8((298 * scaled - 100 * d - 208 * e + 128) >> 8);
            image.data[idx + 2] = clamp8((298 * scaled + 516 * d + 128) >> 8);
            image.data[idx + 3] = 255;
        }
    }
    return image;
}

vide::SyntaxTree buildSyntax(int width, int height, int bit_depth, int keyframe,
                             const Av1NodeC* nodes, int node_count,
                             const vide::BlockGrid& grid) {
    vide::SyntaxTree tree;
    vide::SyntaxBuilder builder(tree);
    const uint32_t root = builder.addRoot("AV1 picture");
    builder.addField(root, "width", std::to_string(width), -1, -1, vide::SyntaxKind::Derived);
    builder.addField(root, "height", std::to_string(height), -1, -1, vide::SyntaxKind::Derived);
    builder.addField(root, "bit_depth", std::to_string(bit_depth), -1, -1,
                     vide::SyntaxKind::Derived);
    builder.addField(root, "keyframe", keyframe ? "1" : "0", -1, -1,
                     vide::SyntaxKind::Derived);

    if (node_count > 0) {
        // Real AV1 partition tree (parent links from decode_partition).
        const uint32_t tree_root = builder.addRoot("coding_tree");
        std::vector<uint32_t> ids(static_cast<std::size_t>(node_count), vide::kInvalidId);
        for (int i = 0; i < node_count; ++i) {
            const Av1NodeC& n = nodes[i];
            const uint32_t parent =
                (n.parent >= 0 && n.parent < i) ? ids[static_cast<std::size_t>(n.parent)]
                                                : tree_root;
            const bool is_block = n.block_index >= 0;
            const uint32_t id = builder.addStructure(parent, is_block ? "block" : "partition");
            tree.nodes[id].au_index = 0;
            builder.addField(id, "x", std::to_string(n.x), -1, -1, vide::SyntaxKind::Derived);
            builder.addField(id, "y", std::to_string(n.y), -1, -1, vide::SyntaxKind::Derived);
            builder.addField(id, "width", std::to_string(n.w), -1, -1, vide::SyntaxKind::Derived);
            builder.addField(id, "height", std::to_string(n.h), -1, -1, vide::SyntaxKind::Derived);
            builder.addField(id, "depth", std::to_string(n.depth), -1, -1,
                             vide::SyntaxKind::Derived);
            if (is_block && n.block_index < static_cast<int>(grid.blocks.size())) {
                const vide::Block& b = grid.blocks[static_cast<std::size_t>(n.block_index)];
                builder.addField(id, "type", toString(b.type), -1, -1, vide::SyntaxKind::Derived);
                builder.addField(id, "qp", std::to_string(b.qp), -1, -1,
                                 vide::SyntaxKind::Derived);
                builder.addField(id, "bits", std::to_string(b.bits), -1, -1,
                                 vide::SyntaxKind::Derived);
                builder.addField(id, "mv",
                                 std::to_string(b.mv[0][0]) + "," + std::to_string(b.mv[0][1]),
                                 -1, -1, vide::SyntaxKind::Derived);
                tree.nodes[id].block_id = b.id;
            }
            ids[static_cast<std::size_t>(i)] = id;
        }
    }
    return tree;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <stream.obu> <out.vpk> [hash size mtime]\n", argv[0]);
        return 1;
    }

    vide::SourceFingerprint fp;
    if (argc >= 6) {
        fp.hash = std::strtoull(argv[3], nullptr, 10);
        fp.size = std::strtoull(argv[4], nullptr, 10);
        fp.mtime = std::strtoll(argv[5], nullptr, 10);
        fp.valid = true;
    }

    if (av1_open(argv[1]) != 0) {
        vide::helperEmitError(std::string("cannot open ") + argv[1]);
        return 1;
    }

    vide::VpkWriter writer;
    bool writer_open = false;
    int width = 0;
    int height = 0;
    int bit_depth = 8;
    uint32_t frames_written = 0;
    std::vector<Av1BlockInfoC> mb_buffer(262144);
    std::vector<Av1NodeC> node_buffer(1 << 18);

    for (;;) {
        int w = 0, h = 0, bd = 8, kf = 0, cw = 0, ch = 0, mb_count = 0, node_count = 0;
        uint8_t* y = nullptr;
        uint8_t* u = nullptr;
        uint8_t* v = nullptr;
        const int ret = av1_decode_next(&w, &h, &bd, &kf, &y, &u, &v, &cw, &ch,
                                        mb_buffer.data(), static_cast<int>(mb_buffer.size()),
                                        &mb_count, node_buffer.data(),
                                        static_cast<int>(node_buffer.size()), &node_count);
        if (ret == 1) {
            break;
        }
        if (ret != 0) {
            if (ret < 0) {
                break;
            }
            continue;
        }

        if (width == 0) {
            width = w;
            height = h;
            bit_depth = bd;
            vide::helperEmitHello(vide::CodecId::AV1, width, height,
                                  bit_depth == 10 ? vide::BitDepth::B10 : vide::BitDepth::B8);
            vide::Status s = writer.begin(argv[2], vide::CodecId::AV1, fp, width, height,
                                          bit_depth == 10 ? vide::BitDepth::B10
                                                          : vide::BitDepth::B8,
                                          0);
            if (!s.ok()) {
                vide::helperEmitError(s.message());
                std::free(y); std::free(u); std::free(v);
                return 2;
            }
            writer_open = true;
        }

        vide::FrameMeta meta;
        meta.index = frames_written;
        meta.poc = static_cast<int64_t>(frames_written);
        meta.width = width;
        meta.height = height;
        meta.codec = vide::CodecId::AV1;
        meta.bit_depth = bit_depth == 10 ? vide::BitDepth::B10 : vide::BitDepth::B8;
        meta.is_keyframe = kf != 0;

        vide::BlockGrid grid = buildBlocks(mb_buffer.data(), mb_count);
        grid.width = width;
        grid.height = height;
        vide::DecodedImage reco = planesToRgba(y, u, v, w, h, cw, ch, bit_depth);
        vide::SyntaxTree syntax =
            buildSyntax(width, height, bit_depth, kf, node_buffer.data(), node_count, grid);
        std::free(y); std::free(u); std::free(v);

        Av1PlanesC planes;
        std::memset(&planes, 0, sizeof(planes));
        av1_take_planes(&planes);
        vide::DecodedImage pred;
        vide::DecodedImage resi;
        if (planes.has_pred) {
            pred = planes16ToRgba(planes.pred_y, planes.pred_u, planes.pred_v, planes.width,
                                  planes.height, planes.cw, planes.ch, bit_depth, true);
        }
        if (planes.has_resi) {
            resi = planes16ToRgba(planes.resi_y, planes.resi_u, planes.resi_v, planes.width,
                                  planes.height, planes.cw, planes.ch, bit_depth, false);
        }
        std::free(planes.pred_y);
        std::free(planes.pred_u);
        std::free(planes.pred_v);
        std::free(planes.resi_y);
        std::free(planes.resi_u);
        std::free(planes.resi_v);

        if (writer_open) {
            writer.appendFrame(meta, &syntax, &grid, reco.valid() ? &reco : nullptr,
                               pred.valid() ? &pred : nullptr,
                               resi.valid() ? &resi : nullptr);
        }
        ++frames_written;
        vide::helperEmitReady(meta.index);
        if ((frames_written % 8) == 0) {
            vide::helperEmitProgress(frames_written);
        }
    }

    if (writer_open) {
        writer.finish();
    }
    av1_close();

    vide::helperEmitProgress(frames_written);
    vide::helperEmitDone();
    return 0;
}
