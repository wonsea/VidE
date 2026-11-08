// VidE reference-decoder helper: AVC (H.264) via the JM decoder.
//
// JM's C headers clash with C++ standard headers, so all JM access goes through
// the C bridge (avc_bridge.*). This file only deals with VidE models.
//
// Usage: vide_avc_helper <annexb.h264> <out.vpk> [hash size mtime]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "avc_bridge.h"

#include "common/helper_common.h"
#include "common/syntax_builder.h"
#include "common/vpk.h"
#include "core/cache_store.h"

namespace {

vide::BlockGrid buildBlocks(const AvcMbInfo* mbs, int count) {
    vide::BlockGrid grid;
    for (int i = 0; i < count; ++i) {
        const AvcMbInfo& mb = mbs[i];
        vide::Block block;
        block.id = static_cast<uint32_t>(i);
        block.rect = vide::Rect{mb.x, mb.y, mb.w, mb.h};
        block.depth = 0;
        block.qp = mb.qp;
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
        block.bits = mb.bits;
        if (mb.bit_length > 0) {
            block.bit_offset = mb.bit_offset;  // NAL-RBSP-relative
            block.bit_length = mb.bit_length;
        }
        block.slice_index = static_cast<uint32_t>(mb.slice_index);
        grid.blocks.push_back(block);
    }
    return grid;
}

vide::DecodedImage planesToRgba(const uint8_t* y, const uint8_t* u, const uint8_t* v, int width,
                                int height, int cw, int ch, int bit_depth) {
    vide::DecodedImage image;
    if (y == nullptr || width <= 0 || height <= 0 || cw <= 0 || ch <= 0) {
        return image;
    }
    // The JM capture hook normalises samples to 8-bit, so no extra shift here.
    (void)bit_depth;
    const int shift = 0;
    image.width = width;
    image.height = height;
    image.format = vide::PixelFormat::RGBA8888;
    image.bit_depth = vide::BitDepth::B8;
    image.data.resize(static_cast<std::size_t>(width) * height * 4);

    auto clamp8 = [](int val) {
        return static_cast<uint8_t>(val < 0 ? 0 : (val > 255 ? 255 : val));
    };
    for (int yy = 0; yy < height; ++yy) {
        for (int xx = 0; xx < width; ++xx) {
            const int Y = y[static_cast<std::size_t>(yy) * width + xx] >> shift;
            const int cy = yy * ch / height;
            const int cx = xx * cw / width;
            const int U = u[static_cast<std::size_t>(cy) * cw + cx] >> shift;
            const int V = v[static_cast<std::size_t>(cy) * cw + cx] >> shift;
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
                // Residual rendered as a 128-biased grey image (luma only).
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

vide::SyntaxTree buildSyntax(int width, int height, int bit_depth, int poc,
                             const vide::BlockGrid& grid, const AvcMbInfo* mbs, int mb_count,
                             const AvcSliceInfo* slices, int slice_count) {
    vide::SyntaxTree tree;
    vide::SyntaxBuilder builder(tree);
    (void)width;
    (void)height;
    (void)bit_depth;

    // Per-frame slice headers (H.264 7.3.3), read straight from JM's decoder.
    for (int si = 0; si < slice_count; ++si) {
        const AvcSliceInfo& slice = slices[si];
        const uint32_t sh = builder.addRoot("slice_header");
        const char* type_name =
            (slice.slice_type == 0 || slice.slice_type == 5)   ? "P"
            : (slice.slice_type == 1 || slice.slice_type == 6) ? "B"
            : (slice.slice_type == 2 || slice.slice_type == 7) ? "I"
            : (slice.slice_type == 3 || slice.slice_type == 8) ? "SP"
            : (slice.slice_type == 4 || slice.slice_type == 9) ? "SI"
                                                               : "?";
        auto field = [&](const char* name, const std::string& v) {
            builder.addField(sh, name, v, -1, -1, vide::SyntaxKind::Derived);
        };
        field("index", std::to_string(si));
        field("slice_index", std::to_string(slice.slice_index));
        field("nal_ref_idc", std::to_string(slice.nal_ref_idc));
        field("idr_flag", std::to_string(slice.idr_flag));
        field("slice_type", std::to_string(slice.slice_type));
        field("slice_type_name", type_name);
        field("pic_parameter_set_id", std::to_string(slice.pic_parameter_set_id));
        field("frame_num", std::to_string(slice.frame_num));
        field("field_pic_flag", std::to_string(slice.field_pic_flag));
        if (slice.field_pic_flag) {
            field("bottom_field_flag", std::to_string(slice.bottom_field_flag));
        }
        if (slice.idr_flag) {
            field("idr_pic_id", std::to_string(slice.idr_pic_id));
        }
        field("pic_order_cnt_lsb", std::to_string(slice.pic_order_cnt_lsb));
        field("delta_pic_order_cnt_bottom", std::to_string(slice.delta_pic_order_cnt_bottom));
        field("delta_pic_order_cnt[0]", std::to_string(slice.delta_pic_order_cnt0));
        field("delta_pic_order_cnt[1]", std::to_string(slice.delta_pic_order_cnt1));
        field("redundant_pic_cnt", std::to_string(slice.redundant_pic_cnt));
        field("num_ref_idx_l0_active", std::to_string(slice.num_ref_idx_l0_active));
        field("num_ref_idx_l1_active", std::to_string(slice.num_ref_idx_l1_active));
        field("slice_qp_delta", std::to_string(slice.slice_qp_delta));
        field("slice_qp", std::to_string(slice.slice_qp));
        for (int j = 0; j < slice.ref_l0_count && j < 16; ++j) {
            field(("ref_l0_poc[" + std::to_string(j) + "]").c_str(),
                  std::to_string(slice.ref_l0_poc[j]));
        }
        for (int j = 0; j < slice.ref_l1_count && j < 16; ++j) {
            field(("ref_l1_poc[" + std::to_string(j) + "]").c_str(),
                  std::to_string(slice.ref_l1_poc[j]));
        }
    }

    // Per-macroblock coding tree.
    if (!grid.blocks.empty()) {
        const uint32_t tree_root = builder.addRoot("coding_tree");
        builder.addField(tree_root, "poc", std::to_string(poc), -1, -1,
                         vide::SyntaxKind::Derived);
        for (const vide::Block& block : grid.blocks) {
            const std::size_t mb_index = block.id;
            const uint32_t mb_node = builder.addStructure(tree_root, "macroblock");
            tree.nodes[mb_node].au_index = 0;
            tree.nodes[mb_node].block_id = block.id;
            auto fld = [&](const char* name, const std::string& v) {
                builder.addField(mb_node, name, v, -1, -1, vide::SyntaxKind::Derived);
            };
            fld("x", std::to_string(block.rect.x));
            fld("y", std::to_string(block.rect.y));
            fld("w", std::to_string(block.rect.w));
            fld("h", std::to_string(block.rect.h));
            fld("type", toString(block.type));
            fld("qp", std::to_string(block.qp));
            fld("qp_source", toString(block.qp_source));
            fld("bits", std::to_string(block.bits));
            if (block.type == vide::BlockType::Inter || block.type == vide::BlockType::Skip) {
                if (block.ref_idx[0] >= 0) {
                    fld("ref_l0", std::to_string(block.ref_idx[0]));
                    fld("mv_l0", std::to_string(block.mv[0][0]) + "," +
                                     std::to_string(block.mv[0][1]));
                }
                if (block.ref_idx[1] >= 0) {
                    fld("ref_l1", std::to_string(block.ref_idx[1]));
                    fld("mv_l1", std::to_string(block.mv[1][0]) + "," +
                                     std::to_string(block.mv[1][1]));
                }
            }
            if (mb_index >= static_cast<std::size_t>(mb_count)) {
                continue;
            }
            const AvcMbInfo& mb = mbs[mb_index];
            // 4 x 8x8 sub-macroblock partitions (AVC's only sub-MB recursion).
            for (int k = 0; k < 4; ++k) {
                const uint32_t sub = builder.addStructure(mb_node, "sub_macroblock");
                tree.nodes[sub].au_index = 0;
                builder.addField(sub, "x", std::to_string(block.rect.x + (k % 2) * 8), -1, -1,
                                 vide::SyntaxKind::Derived);
                builder.addField(sub, "y", std::to_string(block.rect.y + (k / 2) * 8), -1, -1,
                                 vide::SyntaxKind::Derived);
                builder.addField(sub, "b8mode", std::to_string(mb.b8mode[k]), -1, -1,
                                 vide::SyntaxKind::Derived);
                builder.addField(sub, "b8pdir", std::to_string(mb.b8pdir[k]), -1, -1,
                                 vide::SyntaxKind::Derived);
            }
        }
    }
    return tree;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <annexb.h264> <out.vpk> [hash size mtime]\n", argv[0]);
        return 1;
    }

    vide::SourceFingerprint fp;
    if (argc >= 6) {
        fp.hash = std::strtoull(argv[3], nullptr, 10);
        fp.size = std::strtoull(argv[4], nullptr, 10);
        fp.mtime = std::strtoll(argv[5], nullptr, 10);
        fp.valid = true;
    }

    if (avc_open(argv[1]) != 0) {
        vide::helperEmitError(std::string("cannot open ") + argv[1]);
        return 1;
    }

    vide::VpkWriter writer;
    bool writer_open = false;
    int width = 0;
    int height = 0;
    int bit_depth = 8;
    uint32_t frames_written = 0;
    std::vector<AvcMbInfo> mb_buffer;

    for (;;) {
        int w = 0, h = 0, bd = 8, poc = 0, mb_count = 0, cw = 0, ch = 0;
        uint8_t* ry = nullptr;
        uint8_t* ru = nullptr;
        uint8_t* rv = nullptr;
        AvcSliceInfo slices[64]{};
        int slice_count = 0;
        int16_t* py = nullptr;
        int16_t* pu = nullptr;
        int16_t* pv = nullptr;
        int16_t* resy = nullptr;
        int16_t* resu = nullptr;
        int16_t* resv = nullptr;
        int has_pred = 0;
        // First pass to learn the MB count, second to fetch (decode only once
        // per call; we size the buffer generously from the known geometry).
        mb_buffer.resize(4096);
        const int ret = avc_decode_next(&w, &h, &bd, &poc, &ry, &ru, &rv, &cw, &ch,
                                        mb_buffer.data(), static_cast<int>(mb_buffer.size()),
                                        &mb_count, slices, 64, &slice_count, &py, &pu, &pv,
                                        &resy, &resu, &resv, &has_pred);
        if (ret != 0) {
            if (ret == 1) {
                break;  // end of stream
            }
            continue;  // frame not ready yet
        }

        if (width == 0) {
            width = w;
            height = h;
            bit_depth = bd;
            vide::helperEmitHello(vide::CodecId::H264, width, height,
                                  bit_depth == 10 ? vide::BitDepth::B10 : vide::BitDepth::B8);
            vide::Status s = writer.begin(argv[2], vide::CodecId::H264, fp, width, height,
                                          bit_depth == 10 ? vide::BitDepth::B10
                                                          : vide::BitDepth::B8,
                                          0);
            if (!s.ok()) {
                vide::helperEmitError(s.message());
                std::free(ry); std::free(ru); std::free(rv);
                return 2;
            }
            writer_open = true;
        }

        vide::FrameMeta meta;
        meta.index = frames_written;
        meta.poc = poc;
        meta.width = width;
        meta.height = height;
        meta.codec = vide::CodecId::H264;
        meta.bit_depth = bit_depth == 10 ? vide::BitDepth::B10 : vide::BitDepth::B8;

        vide::BlockGrid grid = buildBlocks(mb_buffer.data(), mb_count);
        grid.width = width;
        grid.height = height;
        grid.ctu_size = 16;
        vide::DecodedImage reco = planesToRgba(ry, ru, rv, w, h, cw, ch, bit_depth);
        vide::DecodedImage pred;
        vide::DecodedImage resi;
        if (has_pred) {
            pred = planes16ToRgba(py, pu, pv, w, h, cw, ch, true);
            resi = planes16ToRgba(resy, resu, resv, w, h, cw, ch, false);
        }
        vide::SyntaxTree syntax =
            buildSyntax(width, height, bit_depth, poc, grid, mb_buffer.data(), mb_count, slices,
                        slice_count);
        std::free(ry);
        std::free(ru);
        std::free(rv);
        std::free(py);
        std::free(pu);
        std::free(pv);
        std::free(resy);
        std::free(resu);
        std::free(resv);

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
    avc_close();

    vide::helperEmitProgress(frames_written);
    vide::helperEmitDone();
    return 0;
}
