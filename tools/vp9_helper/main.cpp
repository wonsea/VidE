// VidE reference-decoder helper: VP9 via libvpx.
//
// libvpx's C headers clash with C++, so all libvpx access goes through the C
// bridge (vp9_bridge.*). Block/reco data comes from the VidE capture hook
// compiled into libvpx.
//
// Usage: vide_vp9_helper <stream.ivf|stream.webm> <out.vpk> [hash size mtime]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "vp9_bridge.h"

#include "common/helper_common.h"
#include "common/syntax_builder.h"
#include "common/vpk.h"
#include "core/cache_store.h"

namespace {

vide::BlockGrid buildBlocks(const Vp9BlockInfoC* mbs, int count) {
    vide::BlockGrid grid;
    for (int i = 0; i < count; ++i) {
        const Vp9BlockInfoC& mb = mbs[i];
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

vide::DecodedImage planes16ToRgba(const int16_t* y, const int16_t* u, const int16_t* v,
                                  int width, int height, int cw, int ch, bool is_pred) {
    vide::DecodedImage image;
    if (y == nullptr || width <= 0 || height <= 0) {
        return image;
    }
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
            const int Y = y[static_cast<std::size_t>(yy) * width + xx];
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
                const int cy = yy * ch / height;
                const int cx = xx * cw / width;
                U = u[static_cast<std::size_t>(cy) * cw + cx];
                V = v[static_cast<std::size_t>(cy) * cw + cx];
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
                             const Vp9NodeC* nodes, int node_count,
                             const vide::BlockGrid& grid) {
    vide::SyntaxTree tree;
    vide::SyntaxBuilder builder(tree);
    const uint32_t root = builder.addRoot("VP9 picture");
    builder.addField(root, "width", std::to_string(width), -1, -1, vide::SyntaxKind::Derived);
    builder.addField(root, "height", std::to_string(height), -1, -1, vide::SyntaxKind::Derived);
    builder.addField(root, "bit_depth", std::to_string(bit_depth), -1, -1,
                     vide::SyntaxKind::Derived);
    builder.addField(root, "keyframe", keyframe ? "1" : "0", -1, -1,
                     vide::SyntaxKind::Derived);

    if (node_count > 0) {
        // Real VP9 partition tree (parent links from decode_partition).
        const uint32_t tree_root = builder.addRoot("coding_tree");
        std::vector<uint32_t> ids(static_cast<std::size_t>(node_count), vide::kInvalidId);
        for (int i = 0; i < node_count; ++i) {
            const Vp9NodeC& n = nodes[i];
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
        std::fprintf(stderr, "usage: %s <stream.ivf> <out.vpk> [hash size mtime]\n", argv[0]);
        return 1;
    }

    vide::SourceFingerprint fp;
    if (argc >= 6) {
        fp.hash = std::strtoull(argv[3], nullptr, 10);
        fp.size = std::strtoull(argv[4], nullptr, 10);
        fp.mtime = std::strtoll(argv[5], nullptr, 10);
        fp.valid = true;
    }

    if (vp9_open(argv[1]) != 0) {
        vide::helperEmitError(std::string("cannot open ") + argv[1]);
        return 1;
    }

    vide::VpkWriter writer;
    bool writer_open = false;
    int width = 0;
    int height = 0;
    int bit_depth = 8;
    uint32_t frames_written = 0;
    std::vector<Vp9BlockInfoC> mb_buffer(65536);
    std::vector<Vp9NodeC> node_buffer(1 << 17);

    for (;;) {
        int w = 0, h = 0, bd = 8, kf = 0, cw = 0, ch = 0, mb_count = 0, node_count = 0;
        uint8_t* y = nullptr;
        uint8_t* u = nullptr;
        uint8_t* v = nullptr;
        int16_t* py = nullptr;
        int16_t* pu = nullptr;
        int16_t* pv = nullptr;
        int16_t* resy = nullptr;
        int16_t* resu = nullptr;
        int16_t* resv = nullptr;
        int has_pred = 0;
        const int ret = vp9_decode_next(&w, &h, &bd, &kf, &y, &u, &v, &cw, &ch,
                                        mb_buffer.data(), static_cast<int>(mb_buffer.size()),
                                        &mb_count, node_buffer.data(),
                                        static_cast<int>(node_buffer.size()), &node_count,
                                        &py, &pu, &pv, &resy, &resu, &resv, &has_pred);
        if (ret == 1) {
            break;  // end of stream
        }
        if (ret != 0) {
            if (ret < 0) {
                break;  // error
            }
            continue;  // retry
        }

        if (width == 0) {
            width = w;
            height = h;
            bit_depth = bd;
            vide::helperEmitHello(vide::CodecId::VP9, width, height,
                                  bit_depth == 10 ? vide::BitDepth::B10 : vide::BitDepth::B8);
            vide::Status s = writer.begin(argv[2], vide::CodecId::VP9, fp, width, height,
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
        meta.codec = vide::CodecId::VP9;
        meta.bit_depth = bit_depth == 10 ? vide::BitDepth::B10 : vide::BitDepth::B8;
        meta.is_keyframe = kf != 0;

        vide::BlockGrid grid = buildBlocks(mb_buffer.data(), mb_count);
        grid.width = width;
        grid.height = height;
        vide::DecodedImage reco = planesToRgba(y, u, v, w, h, cw, ch, bit_depth);
        vide::DecodedImage pred;
        vide::DecodedImage resi;
        if (has_pred) {
            pred = planes16ToRgba(py, pu, pv, w, h, cw, ch, true);
            resi = planes16ToRgba(resy, resu, resv, w, h, cw, ch, false);
        }
        vide::SyntaxTree syntax =
            buildSyntax(width, height, bit_depth, kf, node_buffer.data(), node_count, grid);
        std::free(y); std::free(u); std::free(v);
        std::free(py); std::free(pu); std::free(pv);
        std::free(resy); std::free(resu); std::free(resv);

        if (writer_open) {
            writer.appendFrame(meta, &syntax, &grid, reco.valid() ? &reco : nullptr,
                               pred.valid() ? &pred : nullptr, resi.valid() ? &resi : nullptr);
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
    vp9_close();

    vide::helperEmitProgress(frames_written);
    vide::helperEmitDone();
    return 0;
}
