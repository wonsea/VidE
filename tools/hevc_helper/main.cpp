// VidE reference-decoder helper: HEVC via the HM libraries.
//
// Links HM's static libraries, decodes the Annex-B stream, and writes frames
// (blocks / reco) into a VidE Pack cache. Progress is reported to the parent
// over stderr (HM logs go to stdout). Runs as a separate process so it never
// collides with other reference decoders' global symbols.
//
// Usage: vide_hevc_helper <annexb.hevc> <out.vpk> [source_hash source_size source_mtime]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "TLibCommon/TComDataCU.h"
#include "TLibCommon/TComPic.h"
#include "TLibCommon/TComPicSym.h"
#include "TLibCommon/TComPicYuv.h"
#include "TLibCommon/TComSlice.h"
#include "TLibDecoder/AnnexBread.h"
#include "TLibDecoder/NALread.h"
#include "TLibDecoder/TDecTop.h"
#include "TLibDecoder/VideCapture.h"

#include "common/helper_common.h"
#include "common/syntax_builder.h"
#include "common/vpk.h"
#include "core/cache_store.h"

namespace {

const char* partSizeName(PartSize ps) {
    switch (ps) {
        case SIZE_2Nx2N: return "2Nx2N";
        case SIZE_2NxN: return "2NxN";
        case SIZE_Nx2N: return "Nx2N";
        case SIZE_NxN: return "NxN";
        case SIZE_2NxnU: return "2NxnU";
        case SIZE_2NxnD: return "2NxnD";
        case SIZE_nLx2N: return "nLx2N";
        case SIZE_nRx2N: return "nRx2N";
        default: return "?";
    }
}

// Single recursion that fills the BlockGrid with leaf CUs and appends a real
// CU/PU/TU syntax subtree (values read straight from the decoder structures).
// Leaf CUs link their syntax node to the block via block_id (R1/S-3).
void harvestCu(const TComDataCU* cu, const TComSPS& sps, UInt absPartIdx, UInt depth,
               int picture_width, int picture_height, vide::SyntaxTree& tree,
               vide::SyntaxBuilder& builder, uint32_t parent, vide::BlockGrid& grid) {
    const UInt maxParts = cu->getPic()->getNumPartitionsInCtu();
    const UInt curParts = maxParts >> (depth << 1);

    const UInt lx = cu->getCUPelX() + g_auiRasterToPelX[g_auiZscanToRaster[absPartIdx]];
    const UInt ly = cu->getCUPelY() + g_auiRasterToPelY[g_auiZscanToRaster[absPartIdx]];
    const UInt cw = sps.getMaxCUWidth() >> depth;
    const UInt ch = sps.getMaxCUHeight() >> depth;

    const Bool split = (depth < cu->getDepth(absPartIdx)) &&
                       (depth < sps.getLog2DiffMaxMinCodingBlockSize());

    const uint32_t cuNode = builder.addStructure(parent, "coding_unit");
    tree.nodes[cuNode].au_index = 0;
    {
        unsigned bo = 0, bl = 0;
        if (vide_hm::cuBitRange(cu, absPartIdx, bo, bl)) {
            tree.nodes[cuNode].bit_offset = static_cast<int64_t>(bo);
            tree.nodes[cuNode].bit_length = static_cast<int64_t>(bl);
        }
    }
    auto cu_field = [&](const char* name, const std::string& value) {
        builder.addField(cuNode, name, value, -1, -1, vide::SyntaxKind::Derived);
    };
    cu_field("x", std::to_string(lx));
    cu_field("y", std::to_string(ly));
    cu_field("width", std::to_string(cw));
    cu_field("height", std::to_string(ch));
    cu_field("depth", std::to_string(depth));
    cu_field("pred_mode", cu->isIntra(absPartIdx) ? "INTRA" : "INTER");
    cu_field("part_mode", partSizeName(cu->getPartitionSize(absPartIdx)));
    cu_field("qp", std::to_string(cu->getQP(absPartIdx)));
    cu_field("skip_flag", cu->isSkipped(absPartIdx) ? "1" : "0");
    cu_field("split_cu_flag", split ? "1" : "0");

    if (split) {
        const UInt qParts = curParts >> 2;
        UInt idx = absPartIdx;
        for (UInt part = 0; part < 4; ++part) {
            harvestCu(cu, sps, idx, depth + 1, picture_width, picture_height, tree, builder,
                      cuNode, grid);
            idx += qParts;
        }
        return;
    }

    // --- Prediction unit ---------------------------------------------------
    const uint32_t puNode = builder.addStructure(cuNode, "prediction_unit");
    tree.nodes[puNode].au_index = 0;
    auto pu_field = [&](const char* name, const std::string& value) {
        builder.addField(puNode, name, value, -1, -1, vide::SyntaxKind::Derived);
    };
    if (cu->isIntra(absPartIdx)) {
        pu_field("intra_luma_mode", std::to_string(cu->getIntraDir(CHANNEL_TYPE_LUMA, absPartIdx)));
        pu_field("intra_chroma_mode",
                 std::to_string(cu->getIntraDir(CHANNEL_TYPE_CHROMA, absPartIdx)));
    } else {
        pu_field("inter_dir", std::to_string(cu->getInterDir(absPartIdx)));
        pu_field("merge_flag", cu->getMergeFlag(absPartIdx) ? "1" : "0");
        pu_field("merge_index", std::to_string(cu->getMergeIndex(absPartIdx)));
        for (int list = 0; list < 2; ++list) {
            const RefPicList eList = (list == 0) ? REF_PIC_LIST_0 : REF_PIC_LIST_1;
            const TComCUMvField* mv_field = cu->getCUMvField(eList);
            if (mv_field == nullptr) {
                continue;
            }
            const TComMv& mv = mv_field->getMv(static_cast<Int>(absPartIdx));
            pu_field((std::string("mv_l") + std::to_string(list)).c_str(),
                     std::to_string(mv.getHor()) + "," + std::to_string(mv.getVer()));
            pu_field((std::string("ref_idx_l") + std::to_string(list)).c_str(),
                     std::to_string(static_cast<int>(mv_field->getRefIdx(static_cast<Int>(absPartIdx)))));
        }
    }

    // --- Transform unit (summary: quadtree depth, size, coded-block flags) --
    const UInt trDepth = cu->getTransformIdx(absPartIdx);
    const UInt tuLog2 = sps.getQuadtreeTULog2MaxSize();
    const UInt tuSize = (tuLog2 > trDepth) ? (1u << (tuLog2 - trDepth)) : 1u;
    const uint32_t tuNode = builder.addStructure(cuNode, "transform_unit");
    tree.nodes[tuNode].au_index = 0;
    builder.addField(tuNode, "depth", std::to_string(trDepth), -1, -1, vide::SyntaxKind::Derived);
    builder.addField(tuNode, "size", std::to_string(tuSize), -1, -1, vide::SyntaxKind::Derived);
    builder.addField(tuNode, "cbf_luma", std::to_string(cu->getCbf(absPartIdx, COMPONENT_Y, trDepth)),
                     -1, -1, vide::SyntaxKind::Derived);
    builder.addField(tuNode, "cbf_chroma",
                     std::to_string(cu->getCbf(absPartIdx, COMPONENT_Cb, trDepth)), -1, -1,
                     vide::SyntaxKind::Derived);

    // --- Coding block (leaf, clipped to the picture) -----------------------
    int rx = static_cast<int>(lx);
    int ry = static_cast<int>(ly);
    int rw = static_cast<int>(cw);
    int rh = static_cast<int>(ch);
    if (rx >= picture_width || ry >= picture_height) {
        return;
    }
    rw = std::min(rw, picture_width - rx);
    rh = std::min(rh, picture_height - ry);

    vide::Block block;
    block.id = static_cast<uint32_t>(grid.blocks.size());
    block.rect = vide::Rect{rx, ry, rw, rh};
    block.depth = static_cast<int>(depth);
    block.qp = cu->getQP(absPartIdx);
    if (cu->isIntra(absPartIdx)) {
        block.type = vide::BlockType::Intra;
    } else if (cu->isSkipped(absPartIdx)) {
        block.type = vide::BlockType::Skip;
    } else if (cu->getPredictionMode(absPartIdx) == MODE_INTER) {
        block.type = vide::BlockType::Inter;
    } else {
        block.type = vide::BlockType::Unknown;
    }
    // QP provenance (req 7): an explicit cu_qp_delta or a skip CU without one.
    block.qp_source = block.qp < 0
                          ? vide::QpSource::Na
                          : (vide_hm::cuQpCoded(cu, absPartIdx) ? vide::QpSource::Coded
                                                                : vide::QpSource::Inherited);
    block.bits = vide_hm::cuBits(cu, absPartIdx);  // CABAC bits for this CU
    {
        unsigned bit_off = 0, bit_len = 0;
        if (vide_hm::cuBitRange(cu, absPartIdx, bit_off, bit_len)) {
            block.bit_offset = bit_off;
            block.bit_length = bit_len;
        }
    }

    if (block.type == vide::BlockType::Inter || block.type == vide::BlockType::Skip) {
        for (int list = 0; list < 2; ++list) {
            const RefPicList eList = (list == 0) ? REF_PIC_LIST_0 : REF_PIC_LIST_1;
            const TComCUMvField* mv_field = cu->getCUMvField(eList);
            if (mv_field == nullptr) {
                continue;
            }
            const TComMv& mv = mv_field->getMv(static_cast<Int>(absPartIdx));
            block.mv[list][0] = static_cast<int16_t>(mv.getHor());
            block.mv[list][1] = static_cast<int16_t>(mv.getVer());
            block.ref_idx[list] =
                static_cast<int8_t>(mv_field->getRefIdx(static_cast<Int>(absPartIdx)));
        }
    }
    grid.blocks.push_back(block);
    tree.nodes[cuNode].block_id = block.id;  // link syntax node to the picture block
}

// Builds the BlockGrid and appends a `coding_tree` subtree to `tree`.
void appendCodingTree(TComPic* pic, vide::SyntaxTree& tree, vide::BlockGrid& grid) {
    const TComSPS& sps = pic->getPicSym()->getSPS();
    grid.width = static_cast<int>(sps.getPicWidthInLumaSamples());
    grid.height = static_cast<int>(sps.getPicHeightInLumaSamples());
    grid.ctu_size = static_cast<int>(sps.getMaxCUWidth());

    vide::SyntaxBuilder builder(tree);
    const uint32_t root = builder.addRoot("coding_tree");
    tree.nodes[root].au_index = 0;
    const UInt numCtus = pic->getPicSym()->getNumberOfCtusInFrame();
    for (UInt ctu = 0; ctu < numCtus; ++ctu) {
        const TComDataCU* cu = pic->getCtu(ctu);
        if (cu == nullptr) {
            continue;
        }
        const uint32_t ctuNode = builder.addStructure(root, "coding_tree_unit");
        tree.nodes[ctuNode].au_index = 0;
        builder.addField(ctuNode, "address", std::to_string(ctu), -1, -1,
                         vide::SyntaxKind::Derived);
        harvestCu(cu, sps, 0, 0, grid.width, grid.height, tree, builder, ctuNode, grid);
    }
}

// Derives a display syntax tree for one picture from the decoded HM structures
// (SPS/PPS/slice header). Bit offsets are unavailable, so nodes are "derived".
vide::SyntaxTree harvestSyntax(TComPic* pic) {
    vide::SyntaxTree tree;
    vide::SyntaxBuilder builder(tree);

    const uint32_t root = builder.addRoot("HEVC picture");
    builder.addField(root, "poc", std::to_string(pic->getPOC()), -1, -1,
                     vide::SyntaxKind::Derived);

    // Note: the spec-faithful VPS/SPS/PPS are parsed in the main process from
    // the elementary stream (codec/hevc/hevc_syntax) and merged with this tree.
    // Emit every slice of the picture (H.265 pictures may have multiple slices).
    const UInt num_slices = pic->getNumAllocatedSlice();
    for (UInt i = 0; i < num_slices; ++i) {
        const TComSlice* slice = pic->getSlice(i);
        if (slice == nullptr) {
            continue;
        }
        const uint32_t sh = builder.addStructure(root, "slice_segment_header");
        builder.addField(sh, "index", std::to_string(i), -1, -1, vide::SyntaxKind::Derived);
        builder.addField(sh, "slice_segment_address",
                         std::to_string(slice->getSliceSegmentCurStartCtuTsAddr()), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "slice_type", std::to_string(static_cast<int>(slice->getSliceType())),
                         -1, -1, vide::SyntaxKind::Derived);
        builder.addField(sh, "poc", std::to_string(slice->getPOC()), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "slice_qp", std::to_string(slice->getSliceQp()), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "slice_qp_delta", std::to_string(slice->getSliceQpDelta()), -1, -1,
                         vide::SyntaxKind::Derived);
        const int n0 = slice->getNumRefIdx(REF_PIC_LIST_0);
        const int n1 = slice->getNumRefIdx(REF_PIC_LIST_1);
        builder.addField(sh, "num_ref_idx_l0_active", std::to_string(n0), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "num_ref_idx_l1_active", std::to_string(n1), -1, -1,
                         vide::SyntaxKind::Derived);
        for (int j = 0; j < n0; ++j) {
            const TComPic* rp = slice->getRefPic(REF_PIC_LIST_0, j);
            builder.addField(sh, ("ref_l0_poc[" + std::to_string(j) + "]").c_str(),
                             rp != nullptr ? std::to_string(rp->getPOC()) : std::string("-"),
                             -1, -1, vide::SyntaxKind::Derived);
        }
        for (int j = 0; j < n1; ++j) {
            const TComPic* rp = slice->getRefPic(REF_PIC_LIST_1, j);
            builder.addField(sh, ("ref_l1_poc[" + std::to_string(j) + "]").c_str(),
                             rp != nullptr ? std::to_string(rp->getPOC()) : std::string("-"),
                             -1, -1, vide::SyntaxKind::Derived);
        }
        builder.addField(sh, "temporal_id", std::to_string(slice->getTLayer()), -1, -1,
                         vide::SyntaxKind::Derived);
        builder.addField(sh, "is_irap", slice->isIRAP() ? "1" : "0", -1, -1,
                         vide::SyntaxKind::Derived);
        if (i == 0) {
            builder.addField(sh, "slice_data_start_bit",
                             std::to_string(vide_hm::sliceDataStart(pic)), -1, -1,
                             vide::SyntaxKind::Derived);
        }
    }
    return tree;
}

// Converts HM's reconstruction buffer (8-bit) to tightly packed RGBA.
vide::DecodedImage harvestReco(TComPic* pic, int bit_depth) {
    vide::DecodedImage image;
    TComPicYuv* yuv = pic->getPicYuvRec();
    if (yuv == nullptr) {
        return image;
    }
    const int width = yuv->getWidth(COMPONENT_Y);
    const int height = yuv->getHeight(COMPONENT_Y);
    const int cw = yuv->getWidth(COMPONENT_Cb);
    const int ch = yuv->getHeight(COMPONENT_Cb);
    const Pel* yp = yuv->getAddr(COMPONENT_Y);
    const Pel* up = yuv->getAddr(COMPONENT_Cb);
    const Pel* vp = yuv->getAddr(COMPONENT_Cr);
    const int ys = yuv->getStride(COMPONENT_Y);
    const int cs = yuv->getStride(COMPONENT_Cb);
    const int shift = bit_depth > 8 ? (bit_depth - 8) : 0;

    image.width = width;
    image.height = height;
    image.format = vide::PixelFormat::RGBA8888;
    image.bit_depth = vide::BitDepth::B8;
    image.data.resize(static_cast<std::size_t>(width) * height * 4);

    auto clamp8 = [](int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int Y = yp[y * ys + x] >> shift;
            const int U = up[(y / 2) * cs + (x / 2)] >> shift;
            const int V = vp[(y / 2) * cs + (x / 2)] >> shift;
            const int c = Y - 16;
            const int d = U - 128;
            const int e = V - 128;
            const int scaled = c < 0 ? 0 : c;
            std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            image.data[idx + 0] = clamp8((298 * scaled + 409 * e + 128) >> 8);
            image.data[idx + 1] = clamp8((298 * scaled - 100 * d - 208 * e + 128) >> 8);
            image.data[idx + 2] = clamp8((298 * scaled + 516 * d + 128) >> 8);
            image.data[idx + 3] = 255;
            (void)ch;
        }
    }
    return image;
}

// Converts captured prediction or residual planes (4:2:0) to tightly packed
// RGBA. Residual values are centred around 0; add 128 for display.
vide::DecodedImage harvestCaptureRgba(const vide_hm::Planes& planes, bool is_pred, int bit_depth) {
    vide::DecodedImage image;
    const int width = planes.width;
    const int height = planes.height;
    if (width <= 0 || height <= 0) {
        return image;
    }
    const int cw = width / 2;
    const int shift = bit_depth > 8 ? (bit_depth - 8) : 0;
    image.width = width;
    image.height = height;
    image.format = vide::PixelFormat::RGBA8888;
    image.bit_depth = vide::BitDepth::B8;
    image.data.resize(static_cast<std::size_t>(width) * height * 4);

    auto clamp8 = [](int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
    const bool has_chroma = !planes.u.empty();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int Y = planes.y[static_cast<std::size_t>(y) * width + x] >> shift;
            std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            if (!is_pred) {
                // Residual: render signed luma as a 128-biased grey image.
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
                const std::size_t ci = static_cast<std::size_t>(y / 2) * cw + (x / 2);
                U = planes.u[ci] >> shift;
                V = planes.v[ci] >> shift;
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
        std::fprintf(stderr, "usage: %s <annexb.hevc> <out.vpk> [hash size mtime]\n", argv[0]);
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

    TDecTop decoder;
    decoder.create();
    decoder.init();

    vide::VpkWriter writer;
    bool writer_open = false;
    std::set<int> harvested;
    int width = 0;
    int height = 0;
    int bit_depth = 8;
    uint32_t frames_written = 0;

    InputByteStream bytestream(file);
    Int poc = 0;
    Int iPOCLastDisplay = -1;
    Int iSkipFrame = 0;
    TComList<TComPic*>* pcListPic = nullptr;
    Bool loopFiltered = false;

    while (file) {
        const std::streampos location = file.tellg();

        InputNALUnit nalu;
        AnnexBStats stats = AnnexBStats();
        byteStreamNALUnit(bytestream, nalu.getBitstream().getFifo(), stats);

        Bool bNewPicture = false;
        if (!nalu.getBitstream().getFifo().empty()) {
            read(nalu);
            bNewPicture = decoder.decode(nalu, iSkipFrame, iPOCLastDisplay);
            if (bNewPicture) {
                file.clear();
                file.seekg(location - std::streamoff(3));
                bytestream.reset();
            }
        }

        if ((bNewPicture || !file || nalu.m_nalUnitType == NAL_UNIT_EOS) &&
            !decoder.getFirstSliceInSequence()) {
            if (!loopFiltered || file) {
                decoder.executeLoopFilters(poc, pcListPic);
            }
            loopFiltered = (nalu.m_nalUnitType == NAL_UNIT_EOS);
            if (nalu.m_nalUnitType == NAL_UNIT_EOS) {
                decoder.setFirstSliceInSequence(true);
            }
        } else if ((bNewPicture || !file || nalu.m_nalUnitType == NAL_UNIT_EOS) &&
                   decoder.getFirstSliceInSequence()) {
            decoder.setFirstSliceInPicture(true);
        }

        if (pcListPic != nullptr && !pcListPic->empty()) {
            for (auto it = pcListPic->begin(); it != pcListPic->end(); ++it) {
                TComPic* pic = *it;
                if (pic == nullptr || !pic->getOutputMark()) {
                    continue;
                }
                const Int pic_poc = pic->getPOC();
                if (harvested.count(pic_poc) != 0) {
                    continue;
                }
                harvested.insert(pic_poc);

                const TComSPS& sps = pic->getPicSym()->getSPS();
                if (width == 0) {
                    width = static_cast<int>(sps.getPicWidthInLumaSamples());
                    height = static_cast<int>(sps.getPicHeightInLumaSamples());
                    bit_depth = sps.getBitDepth(CHANNEL_TYPE_LUMA);
                    vide::helperEmitHello(vide::CodecId::HEVC, width, height,
                                          bit_depth == 10 ? vide::BitDepth::B10
                                                          : (bit_depth == 12 ? vide::BitDepth::B12
                                                                             : vide::BitDepth::B8));
                    if (!writer_open) {
                        vide::Status s = writer.begin(argv[2], vide::CodecId::HEVC, fp, width,
                                                      height,
                                                      bit_depth == 10 ? vide::BitDepth::B10
                                                                      : vide::BitDepth::B8,
                                                      0);
                        if (!s.ok()) {
                            vide::helperEmitError(s.message());
                            return 2;
                        }
                        writer_open = true;
                    }
                }

                vide::FrameMeta meta;
                meta.index = frames_written;
                meta.poc = pic_poc;
                meta.width = width;
                meta.height = height;
                meta.codec = vide::CodecId::HEVC;
                meta.bit_depth = bit_depth == 10 ? vide::BitDepth::B10 : vide::BitDepth::B8;
                meta.is_keyframe = pic->getSlice(0)->isIRAP();

                vide::SyntaxTree syntax = harvestSyntax(pic);
                vide::BlockGrid grid;
                appendCodingTree(pic, syntax, grid);
                vide::DecodedImage reco = harvestReco(pic, bit_depth);
                vide::DecodedImage pred;
                vide::DecodedImage resi;
                const vide_hm::Capture* capture = vide_hm::getCapture(pic);
                if (capture != nullptr && capture->has_pred) {
                    pred = harvestCaptureRgba(capture->pred, true, bit_depth);
                }
                if (capture != nullptr && capture->has_resi) {
                    resi = harvestCaptureRgba(capture->resi, false, bit_depth);
                }

                if (writer_open) {
                    writer.appendFrame(meta, &syntax, &grid, &reco,
                                       pred.valid() ? &pred : nullptr,
                                       resi.valid() ? &resi : nullptr);
                }
                ++frames_written;
                vide::helperEmitReady(meta.index);
                if ((frames_written % 8) == 0) {
                    vide::helperEmitProgress(frames_written);
                }
            }
        }
    }

    decoder.deletePicBuffer();
    decoder.destroy();

    if (writer_open) {
        writer.finish();
    }
    vide::helperEmitProgress(frames_written);
    vide::helperEmitDone();
    return 0;
}
