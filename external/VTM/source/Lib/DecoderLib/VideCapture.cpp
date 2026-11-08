#include "DecoderLib/VideCapture.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>

#include "CommonLib/ChromaFormat.h"
#include "CommonLib/Picture.h"
#include "CommonLib/Slice.h"
#include "CommonLib/Unit.h"

namespace vide_vvc {
namespace {

std::deque<FrameCapture>& queue() {
    static std::deque<FrameCapture> q;
    return q;
}

unsigned& binCounter() {
    static unsigned counter = 0;
    return counter;
}
std::map<const CodingUnit*, unsigned>& cuStartMap() {
    static std::map<const CodingUnit*, unsigned> m;
    return m;
}
std::map<const CodingUnit*, unsigned>& cuBitsMap() {
    static std::map<const CodingUnit*, unsigned> m;
    return m;
}
std::map<const CodingUnit*, unsigned>& cuStartBitMap() {
    static std::map<const CodingUnit*, unsigned> m;
    return m;
}
std::map<const CodingUnit*, unsigned>& cuBitLengthMap() {
    static std::map<const CodingUnit*, unsigned> m;
    return m;
}
std::map<const CodingUnit*, bool>& cuQpCodedMap() {
    static std::map<const CodingUnit*, bool> m;
    return m;
}
std::map<const Picture*, unsigned>& sliceDataBitMap() {
    static std::map<const Picture*, unsigned> m;
    return m;
}
std::map<const void*, unsigned>& substreamBaseMap() {
    static std::map<const void*, unsigned> m;
    return m;
}
const void*& activeSubstream() {
    static const void* p = nullptr;
    return p;
}
unsigned activeSubstreamBase() {
    auto it = substreamBaseMap().find(activeSubstream());
    return it != substreamBaseMap().end() ? it->second : 0;
}

void copyPlane(std::vector<Pel>& dst, int width, int height, const CPelBuf& src) {
    dst.assign(static_cast<std::size_t>(width) * height, 0);
    if (src.buf == nullptr || src.width <= 0 || src.height <= 0) {
        return;
    }
    const int w = std::min(width, static_cast<int>(src.width));
    const int h = std::min(height, static_cast<int>(src.height));
    const int pitch = src.stride > 0 ? static_cast<int>(src.stride) : src.width;
    const int xmax = std::min(w, pitch);
    for (int y = 0; y < h; ++y) {
        std::memcpy(dst.data() + static_cast<std::size_t>(y) * width,
                    src.buf + static_cast<std::ptrdiff_t>(y) * pitch,
                    static_cast<std::size_t>(xmax) * sizeof(Pel));
    }
}

int predModeToInt(PredMode mode, bool skip) {
    if (skip) {
        return 2;
    }
    switch (mode) {
        case MODE_INTRA: return 0;
        case MODE_INTER: return 1;
        default: return 3;
    }
}

}  // namespace

namespace {

std::map<const void*, std::vector<CodingNode>>& nodeMap() {
    static std::map<const void*, std::vector<CodingNode>> m;
    return m;
}
std::map<const void*, std::vector<int>>& nodeStack() {
    static std::map<const void*, std::vector<int>> m;
    return m;
}

}  // namespace

CodingNodeScope::CodingNodeScope(const void* cs, int x, int y, int w, int h, bool active)
    : cs_(cs), active_(active) {
    if (!active_) {
        return;
    }
    std::vector<CodingNode>& nodes = nodeMap()[cs];
    std::vector<int>& stack = nodeStack()[cs];
    CodingNode node;
    node.parent = stack.empty() ? -1 : stack.back();
    node.x = x;
    node.y = y;
    node.w = w;
    node.h = h;
    node.depth = static_cast<int>(stack.size());
    nodes.push_back(node);
    stack.push_back(static_cast<int>(nodes.size()) - 1);
}

CodingNodeScope::~CodingNodeScope() {
    if (!active_) {
        return;
    }
    std::vector<int>& stack = nodeStack()[cs_];
    if (!stack.empty()) {
        stack.pop_back();
    }
}

void attachCodingNode(const void* cs, const void* cu_ptr) {    auto it = nodeMap().find(cs);
    if (it == nodeMap().end() || it->second.empty()) {
        return;
    }
    const CodingUnit* cu = static_cast<const CodingUnit*>(cu_ptr);
    CodingNode& node = it->second.back();
    node.leaf = 1;
    node.pred_mode = (cu->predMode == MODE_INTRA) ? 0 : 1;
    node.qp = cu->qp;
    auto qpIt = cuQpCodedMap().find(cu);
    node.qp_coded = (qpIt != cuQpCodedMap().end() && qpIt->second) ? 1 : 0;
    node.skip = cu->skip ? 1 : 0;
    if (cu->firstPU != nullptr) {
        node.intra_dir = static_cast<int>(cu->firstPU->intraDir[ChannelType::LUMA]);
        node.inter_dir = cu->firstPU->interDir;
        node.merge_flag = cu->firstPU->mergeFlag ? 1 : 0;
        node.merge_idx = cu->firstPU->mergeIdx;
    }
    auto bitsIt = cuBitsMap().find(cu);
    if (bitsIt != cuBitsMap().end()) {
        node.bits = bitsIt->second;
    }
    auto startBitIt = cuStartBitMap().find(cu);
    auto lenIt = cuBitLengthMap().find(cu);
    if (startBitIt != cuStartBitMap().end() && lenIt != cuBitLengthMap().end()) {
        node.bit_offset = startBitIt->second;
        node.bit_length = lenIt->second;
    }
}

void recordCuQpCoded(const void* cu_ptr) {
    const CodingUnit* cu = static_cast<const CodingUnit*>(cu_ptr);
    cuQpCodedMap()[cu] = true;
}

void recordSliceDataStart(const ::Picture* pic, unsigned bitPos) {
    if (pic == nullptr) {
        return;
    }
    // Keep the first slice's value (picture-level mapping).
    sliceDataBitMap().emplace(pic, bitPos);
}

void recordSubstreamBase(const void* substream, unsigned base) {
    if (substream != nullptr) {
        substreamBaseMap()[substream] = base;
    }
}

void setActiveSubstream(const void* substream) {
    activeSubstream() = substream;
}

void capturePicture(::Picture* pic) {
    if (pic == nullptr || pic->cs == nullptr) {
        return;
    }
    const UnitArea area = pic->cs->area;
    const int width = static_cast<int>(area.lwidth());
    const int height = static_cast<int>(area.lheight());
    if (width <= 0 || height <= 0) {
        return;
    }
    const ChromaFormat fmt = area.chromaFormat;
    const int cw = (fmt == CHROMA_400) ? 0 : (width >> ::getComponentScaleX(COMPONENT_Cb, fmt));
    const int ch = (fmt == CHROMA_400) ? 0 : (height >> ::getComponentScaleY(COMPONENT_Cb, fmt));

    FrameCapture fc;
    fc.poc = pic->getPOC();
    fc.width = width;
    fc.height = height;
    fc.bit_depth = pic->cs->sps != nullptr ? pic->cs->sps->getBitDepth(ChannelType::LUMA) : 8;
    fc.is_keyframe = pic->slices.empty() ? false
                                         : (pic->slices.front() != nullptr &&
                                            pic->slices.front()->isIRAP());
    // Reference POCs (from the first slice), for the frame-level DPB view.
    if (!pic->slices.empty() && pic->slices.front() != nullptr) {
        const Slice* sl = pic->slices.front();
        for (int i = 0; i < sl->getNumRefIdx(REF_PIC_LIST_0); ++i) {
            fc.ref_l0_poc.push_back(sl->getRefPOC(REF_PIC_LIST_0, i));
        }
        for (int i = 0; i < sl->getNumRefIdx(REF_PIC_LIST_1); ++i) {
            fc.ref_l1_poc.push_back(sl->getRefPOC(REF_PIC_LIST_1, i));
        }
        fc.slice_type = static_cast<int>(sl->getSliceType());
        fc.slice_qp = sl->getSliceQp();
        fc.slice_qp_delta = sl->getSliceQpDelta();
        fc.poc_lsb = pic->getPOC();
        fc.temporal_id = static_cast<int>(sl->getTLayer());
        fc.is_irap = sl->isIRAP();
        fc.num_ref_l0 = sl->getNumRefIdx(REF_PIC_LIST_0);
        fc.num_ref_l1 = sl->getNumRefIdx(REF_PIC_LIST_1);
    }
    {
        auto sdIt = sliceDataBitMap().find(pic);
        if (sdIt != sliceDataBitMap().end()) {
            fc.slice_data_bit = sdIt->second;
        }
    }

    // Prediction / residual: read the CodingStructure's bound buffers (these
    // are rebound per picture) rather than Picture::getPredBuf(area), which
    // can hand back an unbound buffer for recycled pictures.
    const CPelUnitBuf pred = pic->cs->getPredBuf();
    const CPelUnitBuf resi = pic->cs->getResiBuf();
    if (pred.get(COMPONENT_Y).buf != nullptr) {
        copyPlane(fc.pred_y, width, height, pred.get(COMPONENT_Y));
        if (cw > 0) {
            copyPlane(fc.pred_u, cw, ch, pred.get(COMPONENT_Cb));
            copyPlane(fc.pred_v, cw, ch, pred.get(COMPONENT_Cr));
        }
        fc.has_pred = true;
    }
    if (resi.get(COMPONENT_Y).buf != nullptr) {
        copyPlane(fc.resi_y, width, height, resi.get(COMPONENT_Y));
        if (cw > 0) {
            copyPlane(fc.resi_u, cw, ch, resi.get(COMPONENT_Cb));
            copyPlane(fc.resi_v, cw, ch, resi.get(COMPONENT_Cr));
        }
        fc.has_resi = true;
    }

    // Reconstruction.
    const CPelUnitBuf reco = pic->getRecoBuf();
    copyPlane(fc.reco_y, width, height, reco.get(COMPONENT_Y));
    if (cw > 0) {
        copyPlane(fc.reco_u, cw, ch, reco.get(COMPONENT_Cb));
        copyPlane(fc.reco_v, cw, ch, reco.get(COMPONENT_Cr));
    }

    // Coding units.
    if (pic->cs->cus.capacity() > 0) {
        for (const CodingUnit* cu : pic->cs->cus) {
            if (cu == nullptr) {
                continue;
            }
            // Luma CUs only: dual-tree chroma CUs share luma positions and would
            // duplicate/overlap the block grid (and have no matching luma node).
            if (cu->chType != ChannelType::LUMA) {
                continue;
            }
            const Area& luma = cu->Y();
            CuInfo info;
            info.x = static_cast<int>(luma.x);
            info.y = static_cast<int>(luma.y);
            info.w = static_cast<int>(luma.width);
            info.h = static_cast<int>(luma.height);
            info.depth = cu->depth;
            info.qp = cu->qp;
            {
                auto qpIt = cuQpCodedMap().find(cu);
                info.qp_coded = (qpIt != cuQpCodedMap().end() && qpIt->second) ? 1 : 0;
            }
            info.pred_mode = predModeToInt(cu->predMode, cu->skip);
            auto bitsIt = cuBitsMap().find(cu);
            info.bits = bitsIt != cuBitsMap().end() ? bitsIt->second : 0;
            auto startBitIt = cuStartBitMap().find(cu);
            auto lenIt = cuBitLengthMap().find(cu);
            if (startBitIt != cuStartBitMap().end() && lenIt != cuBitLengthMap().end()) {
                info.bit_offset = startBitIt->second;
                info.bit_length = lenIt->second;
            }
            if (cu->firstPU != nullptr) {
                for (int list = 0; list < 2; ++list) {
                    const RefPicList e = (list == 0) ? REF_PIC_LIST_0 : REF_PIC_LIST_1;
                    const int mvx = cu->firstPU->mv[e].getHor() >> 2;  // 1/16 -> 1/4 pel
                    const int mvy = cu->firstPU->mv[e].getVer() >> 2;
                    const int ref = cu->firstPU->refIdx[e];
                    if (list == 0) {
                        info.mv0x = mvx;
                        info.mv0y = mvy;
                        info.ref0 = ref;
                    } else {
                        info.mv1x = mvx;
                        info.mv1y = mvy;
                        info.ref1 = ref;
                    }
                }
            }
            fc.cus.push_back(info);
        }
    }

    // Real CU partition tree recorded during CABACReader::coding_tree.
    auto nodeIt = nodeMap().find(pic->cs);
    if (nodeIt != nodeMap().end()) {
        fc.nodes = std::move(nodeIt->second);
        nodeMap().erase(nodeIt);
        nodeStack().erase(pic->cs);
    }

    // CUs are recycled from a pool across pictures: drop the per-CU QP map so a
    // reused CU pointer never carries a stale "coded" flag.
    cuQpCodedMap().clear();
    sliceDataBitMap().clear();
    substreamBaseMap().clear();
    activeSubstream() = nullptr;

    queue().push_back(std::move(fc));
}

bool popCapture(FrameCapture& out) {
    if (queue().empty()) {
        return false;
    }
    out = std::move(queue().front());
    queue().pop_front();
    return true;
}

// ---- CABAC bin accounting --------------------------------------------------

void addBins(unsigned count) { binCounter() += count; }
unsigned binCount() { return binCounter(); }

void markCuStart(const CodingUnit* cu, unsigned bitPos) {
    cuStartMap()[cu] = binCounter();
    cuStartBitMap()[cu] = bitPos + activeSubstreamBase();
}

void recordCuBits(const CodingUnit* cu, unsigned bitPos) {
    auto it = cuStartMap().find(cu);
    if (it != cuStartMap().end()) {
        cuBitsMap()[cu] = binCounter() - it->second;
        cuStartMap().erase(it);
    }
    const unsigned abs = bitPos + activeSubstreamBase();
    auto bitIt = cuStartBitMap().find(cu);
    if (bitIt != cuStartBitMap().end()) {
        cuBitLengthMap()[cu] = abs >= bitIt->second ? abs - bitIt->second : 0;
    }
}

}  // namespace vide_vvc
