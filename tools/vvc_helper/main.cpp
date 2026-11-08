// VidE reference-decoder helper: VVC via the VTM libraries.
//
// Links VTM's static libraries, decodes the Annex-B stream, and writes frames
// (syntax / blocks / reco / pred / resi) into a VidE Pack cache. Progress is
// reported to the parent on stderr. Runs as a separate process to avoid symbol
// collisions.
//
// The per-picture data is captured by the VidE VTM hook (VideCapture) inside
// DecLib::finishPicture, before VTM tears the picture's coding structures down.
// The helper drains that capture queue each iteration; it never reads VTM
// picture structures directly (which raced with VTM's DPB recycling).
//
// Usage: vide_vvc_helper <annexb.vvc> <out.vpk> [hash size mtime]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "CommonLib/CommonDef.h"
#include "CommonLib/Picture.h"
#include "CommonLib/Slice.h"
#include "CommonLib/Unit.h"
#include "DecoderLib/AnnexBread.h"
#include "DecoderLib/DecLib.h"
#include "DecoderLib/NALread.h"
#include "DecoderLib/VideCapture.h"

#include "common/helper_common.h"
#include "common/syntax_builder.h"
#include "common/vpk.h"
#include "core/cache_store.h"

namespace {

vide::BlockGrid buildBlocks(const vide_vvc::FrameCapture& fc) {
    vide::BlockGrid grid;
    grid.width = fc.width;
    grid.height = fc.height;
    for (const vide_vvc::CuInfo& cu : fc.cus) {
        int rx = cu.x;
        int ry = cu.y;
        int rw = cu.w;
        int rh = cu.h;
        if (rx >= grid.width || ry >= grid.height) {
            continue;
        }
        rw = std::min(rw, grid.width - rx);
        rh = std::min(rh, grid.height - ry);

        vide::Block block;
        block.id = static_cast<uint32_t>(grid.blocks.size());
        block.rect = vide::Rect{rx, ry, rw, rh};
        block.depth = cu.depth;
        block.qp = cu.qp;
        switch (cu.pred_mode) {
            case 0: block.type = vide::BlockType::Intra; break;
            case 1: block.type = vide::BlockType::Inter; break;
            case 2: block.type = vide::BlockType::Skip; break;
            default: block.type = vide::BlockType::Unknown; break;
        }
        block.pred_mode = vide::PredictionMode::Unknown;
        block.qp_source = block.qp < 0
                              ? vide::QpSource::Na
                              : (cu.qp_coded ? vide::QpSource::Coded
                                             : vide::QpSource::Inherited);
        block.mv[0][0] = static_cast<int16_t>(cu.mv0x);
        block.mv[0][1] = static_cast<int16_t>(cu.mv0y);
        block.mv[1][0] = static_cast<int16_t>(cu.mv1x);
        block.mv[1][1] = static_cast<int16_t>(cu.mv1y);
        block.ref_idx[0] = static_cast<int8_t>(cu.ref0);
        block.ref_idx[1] = static_cast<int8_t>(cu.ref1);
        block.bits = cu.bits;
        if (cu.bit_length > 0) {
            // NAL-RBSP-relative bit range (substream base already folded in).
            block.bit_offset = cu.bit_offset;
            block.bit_length = cu.bit_length;
        }
        grid.blocks.push_back(block);
    }
    return grid;
}

vide::SyntaxTree buildSyntax(const vide_vvc::FrameCapture& fc, const vide::BlockGrid& grid) {
    vide::SyntaxTree tree;
    vide::SyntaxBuilder builder(tree);
    const uint32_t root = builder.addRoot("VVC picture");
    builder.addField(root, "poc", std::to_string(fc.poc), -1, -1, vide::SyntaxKind::Derived);
    builder.addField(root, "width", std::to_string(fc.width), -1, -1,
                     vide::SyntaxKind::Derived);
    builder.addField(root, "height", std::to_string(fc.height), -1, -1,
                     vide::SyntaxKind::Derived);
    builder.addField(root, "bit_depth", std::to_string(fc.bit_depth), -1, -1,
                     vide::SyntaxKind::Derived);
    builder.addField(root, "keyframe", fc.is_keyframe ? "1" : "0", -1, -1,
                     vide::SyntaxKind::Derived);
    builder.addField(root, "coding_units", std::to_string(fc.cus.size()), -1, -1,
                     vide::SyntaxKind::Derived);
    for (std::size_t i = 0; i < fc.ref_l0_poc.size(); ++i) {
        builder.addField(root, ("ref_l0_poc[" + std::to_string(i) + "]").c_str(),
                         std::to_string(fc.ref_l0_poc[i]), -1, -1, vide::SyntaxKind::Derived);
    }
    for (std::size_t i = 0; i < fc.ref_l1_poc.size(); ++i) {
        builder.addField(root, ("ref_l1_poc[" + std::to_string(i) + "]").c_str(),
                         std::to_string(fc.ref_l1_poc[i]), -1, -1, vide::SyntaxKind::Derived);
    }

    // First-slice header (VTM Slice): gives VVC a "Header" with real fields.
    {
        const uint32_t sh = builder.addStructure(root, "slice_header");
        builder.addField(sh, "slice_type", std::to_string(fc.slice_type), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "slice_qp", std::to_string(fc.slice_qp), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "slice_qp_delta", std::to_string(fc.slice_qp_delta), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "poc_lsb", std::to_string(fc.poc_lsb), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "temporal_id", std::to_string(fc.temporal_id), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "is_irap", fc.is_irap ? "1" : "0", -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "num_ref_idx_l0_active", std::to_string(fc.num_ref_l0), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "num_ref_idx_l1_active", std::to_string(fc.num_ref_l1), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "slice_data_start_bit", std::to_string(fc.slice_data_bit), -1, -1,
                         vide::SyntaxKind::Derived);
    }

    if (fc.nodes.empty()) {
        return tree;
    }

    // Real CU partition tree (parent links recorded by CABACReader::coding_tree).
    std::map<unsigned long long, uint32_t> pos_to_block;
    for (const vide::Block& block : grid.blocks) {
        const unsigned long long key =
            (static_cast<unsigned long long>(block.rect.x) << 48) |
            (static_cast<unsigned long long>(block.rect.y) << 32) |
            (static_cast<unsigned long long>(block.rect.w) << 16) |
            static_cast<unsigned long long>(block.rect.h);
        pos_to_block.emplace(key, block.id);
    }

    const uint32_t tree_root = builder.addRoot("coding_tree");
    std::vector<uint32_t> node_ids(fc.nodes.size(), vide::kInvalidId);
    for (std::size_t i = 0; i < fc.nodes.size(); ++i) {
        const vide_vvc::CodingNode& node = fc.nodes[i];
        const uint32_t parent =
            (node.parent >= 0 && static_cast<std::size_t>(node.parent) < i)
                ? node_ids[static_cast<std::size_t>(node.parent)]
                : tree_root;
        const uint32_t id = builder.addStructure(parent, "coding_unit");
        tree.nodes[id].au_index = 0;
        builder.addField(id, "x", std::to_string(node.x), -1, -1, vide::SyntaxKind::Derived);
        builder.addField(id, "y", std::to_string(node.y), -1, -1, vide::SyntaxKind::Derived);
        builder.addField(id, "width", std::to_string(node.w), -1, -1, vide::SyntaxKind::Derived);
        builder.addField(id, "height", std::to_string(node.h), -1, -1, vide::SyntaxKind::Derived);
        builder.addField(id, "depth", std::to_string(node.depth), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(id, "leaf", node.leaf ? "1" : "0", -1, -1, vide::SyntaxKind::Derived);
        if (node.leaf) {
            builder.addField(id, "pred_mode", node.pred_mode == 0 ? "INTRA" : "INTER", -1, -1,
                             vide::SyntaxKind::Derived);
            if (node.pred_mode == 0) {
                builder.addField(id, "intra_dir", std::to_string(node.intra_dir), -1, -1,
                                 vide::SyntaxKind::Derived);
            } else {
                builder.addField(id, "inter_dir", std::to_string(node.inter_dir), -1, -1,
                                 vide::SyntaxKind::Derived);
                builder.addField(id, "merge_flag", node.merge_flag ? "1" : "0", -1, -1,
                                 vide::SyntaxKind::Derived);
                builder.addField(id, "merge_index", std::to_string(node.merge_idx), -1, -1,
                                 vide::SyntaxKind::Derived);
            }
            builder.addField(id, "qp", std::to_string(node.qp), -1, -1, vide::SyntaxKind::Derived);
            builder.addField(id, "skip_flag", node.skip ? "1" : "0", -1, -1,
                             vide::SyntaxKind::Derived);
            builder.addField(id, "bits", std::to_string(node.bits),
                             node.bit_length > 0 ? static_cast<int64_t>(node.bit_offset) : -1,
                             node.bit_length > 0 ? static_cast<int64_t>(node.bit_length) : -1,
                             vide::SyntaxKind::Derived);
            const unsigned long long key =
                (static_cast<unsigned long long>(node.x) << 48) |
                (static_cast<unsigned long long>(node.y) << 32) |
                (static_cast<unsigned long long>(node.w) << 16) |
                static_cast<unsigned long long>(node.h);
            auto it = pos_to_block.find(key);
            if (it != pos_to_block.end()) {
                tree.nodes[id].block_id = it->second;
            }
        }
        node_ids[i] = id;
    }
    return tree;
}

// Converts a tightly packed YUV plane set (4:2:0) to RGBA; `is_pred` selects
// natural-image conversion, otherwise residual luma is shown as 128-biased grey.
vide::DecodedImage planesToRgba(const std::vector<Pel>& y, const std::vector<Pel>& u,
                                const std::vector<Pel>& v, int width, int height, int bit_depth,
                                bool is_pred) {
    vide::DecodedImage image;
    if (width <= 0 || height <= 0 || y.size() < static_cast<std::size_t>(width) * height) {
        return image;
    }
    const int shift = bit_depth > 8 ? (bit_depth - 8) : 0;
    const int cw = width / 2;
    const int ch = height / 2;
    const bool has_chroma = !u.empty() && !v.empty() &&
                            u.size() >= static_cast<std::size_t>(cw) * ch;
    image.width = width;
    image.height = height;
    image.format = vide::PixelFormat::RGBA8888;
    image.bit_depth = vide::BitDepth::B8;
    image.data.assign(static_cast<std::size_t>(width) * height * 4, 0);

    auto clamp8 = [](int val) {
        return static_cast<uint8_t>(val < 0 ? 0 : (val > 255 ? 255 : val));
    };
    for (int yy = 0; yy < height; ++yy) {
        for (int xx = 0; xx < width; ++xx) {
            const int Y = y[static_cast<std::size_t>(yy) * width + xx] >> shift;
            std::size_t idx = (static_cast<std::size_t>(yy) * width + xx) * 4;
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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stdout, "usage: %s <annexb.vvc> <out.vpk> [hash size mtime]\n", argv[0]);
        return 1;
    }

    std::ifstream file(argv[1], std::ios::in | std::ios::binary);
    if (!file) {
        vide::helperEmitError(std::string("cannot open ") + argv[1]);
        return 1;
    }

    vide::SourceFingerprint fp;
    if (argc >= 6) {
        fp.hash = std::strtoull(argv[3], nullptr, 10);
        fp.size = std::strtoull(argv[4], nullptr, 10);
        fp.mtime = std::strtoll(argv[5], nullptr, 10);
        fp.valid = true;
    }

    initROM();
    DecLib* decoder_ptr = new DecLib();
    DecLib& decoder = *decoder_ptr;
    decoder.create();
    decoder.init();

    vide::VpkWriter writer;
    bool writer_open = false;
    int width = 0;
    int height = 0;
    int bit_depth = 8;
    uint32_t frames_written = 0;

    InputByteStream bytestream(file);
    int poc = 0;
    int iPOCLastDisplay = -1;
    int iSkipFrame = 0;
    int targetOlsIdx = 0;
    PicList* pcListPic = nullptr;
    bool loopFiltered = false;
    bool decodedSliceInAU = false;
    int lastNaluLayerId = -1;

    // Drain frames captured by the VidE VTM hook and write them to the pack.
    auto drain = [&]() {
        vide_vvc::FrameCapture fc;
        while (vide_vvc::popCapture(fc)) {
            if (width == 0) {
                width = fc.width;
                height = fc.height;
                bit_depth = fc.bit_depth;
                vide::helperEmitHello(
                    vide::CodecId::VVC, width, height,
                    bit_depth == 10 ? vide::BitDepth::B10
                                    : (bit_depth == 12 ? vide::BitDepth::B12
                                                       : vide::BitDepth::B8));
                vide::Status s = writer.begin(argv[2], vide::CodecId::VVC, fp, width, height,
                                              bit_depth == 10 ? vide::BitDepth::B10
                                                              : vide::BitDepth::B8,
                                              0);
                if (!s.ok()) {
                    vide::helperEmitError(s.message());
                    std::exit(2);
                }
                writer_open = true;
            }

            vide::FrameMeta meta;
            meta.index = frames_written;
            meta.poc = fc.poc;
            meta.width = width;
            meta.height = height;
            meta.codec = vide::CodecId::VVC;
            meta.bit_depth = bit_depth == 10 ? vide::BitDepth::B10 : vide::BitDepth::B8;
            meta.is_keyframe = fc.is_keyframe;

            vide::BlockGrid grid = buildBlocks(fc);
            vide::SyntaxTree syntax = buildSyntax(fc, grid);
            vide::DecodedImage reco;
            if (!fc.reco_y.empty()) {
                reco = planesToRgba(fc.reco_y, fc.reco_u, fc.reco_v, fc.width, fc.height,
                                    fc.bit_depth, true);
            }
            vide::DecodedImage pred;
            if (fc.has_pred) {
                pred = planesToRgba(fc.pred_y, fc.pred_u, fc.pred_v, fc.width, fc.height,
                                    fc.bit_depth, true);
            }
            vide::DecodedImage resi;
            if (fc.has_resi) {
                resi = planesToRgba(fc.resi_y, fc.resi_u, fc.resi_v, fc.width, fc.height,
                                    fc.bit_depth, false);
            }

            if (writer_open) {
                writer.appendFrame(meta, &syntax, &grid, reco.valid() ? &reco : nullptr,
                                   pred.valid() ? &pred : nullptr,
                                   resi.valid() ? &resi : nullptr);
            }
            ++frames_written;
            vide::helperEmitReady(meta.index);
            if ((frames_written % 4) == 0) {
                vide::helperEmitProgress(frames_written);
            }
        }
    };

    while (file) {
        InputNALUnit nalu;
        nalu.m_nalUnitType = NAL_UNIT_INVALID;

        bool bNewPicture = decoder.isNewPicture(&file, &bytestream);
        bool bNewAccessUnit = bNewPicture && decodedSliceInAU &&
                              decoder.isNewAccessUnit(bNewPicture, &file, &bytestream);
        (void)bNewAccessUnit;

        if (!bNewPicture) {
            AnnexBStats stats = AnnexBStats();
            byteStreamNALUnit(bytestream, nalu.getBitstream().getFifo(), stats);
            if (!nalu.getBitstream().getFifo().empty()) {
                read(nalu);
                // Mirror DecApp: reset accumulated AU/APS NAL state when a slice
                // starts a new access unit.
                if (nalu.isSlice() && decodedSliceInAU &&
                    decoder.isSliceNaluFirstInAU(true, nalu)) {
                    decoder.resetAccessUnitNals();
                    decoder.resetAccessUnitApsNals();
                    decoder.resetAccessUnitPicInfo();
                }
                decoder.decode(nalu, iSkipFrame, iPOCLastDisplay, targetOlsIdx);
                if (nalu.m_nalUnitType == NAL_UNIT_VPS) {
                    const VPS* vps = decoder.getVPS();
                    if (vps != nullptr) {
                        targetOlsIdx = vps->m_targetOlsIdx;
                    }
                }
                if (nalu.isSlice()) {
                    decodedSliceInAU = true;
                }
            }
            lastNaluLayerId = nalu.m_nuhLayerId;
        } else {
            nalu.m_nuhLayerId = lastNaluLayerId;
        }

        if (bNewPicture || !file || nalu.m_nalUnitType == NAL_UNIT_EOS) {
            if (!decoder.getFirstSliceInSequence(nalu.m_nuhLayerId)) {
                if (!loopFiltered || file) {
                    decoder.executeLoopFilters();
                    decoder.finishPicture(poc, pcListPic, INFO, true);
                }
                loopFiltered = (nalu.m_nalUnitType == NAL_UNIT_EOS);
                if (nalu.m_nalUnitType == NAL_UNIT_EOS) {
                    decoder.setFirstSliceInSequence(true, nalu.m_nuhLayerId);
                }
            } else {
                decoder.setFirstSliceInPicture(true);
            }
        }

        // Frames finished by finishPicture() are queued by the VideCapture hook.
        drain();
    }

    decoder.deletePicBuffer();
    decoder.destroy();
    delete decoder_ptr;

    if (writer_open) {
        writer.finish();
    }
    vide::helperEmitProgress(frames_written);
    vide::helperEmitDone();
    return 0;
}
