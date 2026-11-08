#include "TLibDecoder/VideCapture.h"

#include <algorithm>
#include <cstring>
#include <map>

#include "TLibCommon/TComChromaFormat.h"
#include "TLibCommon/TComPicYuv.h"
#include "TLibCommon/TComYuv.h"

namespace vide_hm {
namespace {

std::map<TComPic*, Capture>& store() {
    static std::map<TComPic*, Capture> s;
    return s;
}

void allocPlanes(Planes& p, int width, int height, ChromaFormat fmt) {
    p.width = width;
    p.height = height;
    p.y.assign(static_cast<std::size_t>(width) * height, 0);
    p.u.clear();
    p.v.clear();
    if (fmt != CHROMA_400) {
        const int cw = width >> ::getComponentScaleX(COMPONENT_Cb, fmt);
        const int ch = height >> ::getComponentScaleY(COMPONENT_Cb, fmt);
        p.u.assign(static_cast<std::size_t>(cw) * ch, 0);
        p.v.assign(static_cast<std::size_t>(cw) * ch, 0);
    }
}

Capture& captureFor(TComPic* pic) {
    Capture& c = store()[pic];
    const TComPicYuv* rec = pic->getPicYuvRec();
    const int width = rec->getWidth(COMPONENT_Y);
    const int height = rec->getHeight(COMPONENT_Y);
    const ChromaFormat fmt = pic->getChromaFormat();
    if (c.width != width || c.height != height || c.poc != pic->getPOC()) {
        c.width = width;
        c.height = height;
        c.poc = pic->getPOC();
        c.pred = Planes{};
        c.resi = Planes{};
        c.has_pred = false;
        c.has_resi = false;
        allocPlanes(c.pred, width, height, fmt);
        allocPlanes(c.resi, width, height, fmt);
    }
    return c;
}

void copyRegion(std::vector<Pel>& dst, int dstW, int dstH, int dx, int dy, const Pel* src,
                int srcStride, int w, int h) {
    if (w <= 0 || h <= 0 || dst.empty()) {
        return;
    }
    for (int y = 0; y < h; ++y) {
        const int row = dy + y;
        if (row < 0 || row >= dstH) {
            continue;
        }
        const int x0 = std::max(0, dx);
        const int x1 = std::min(dstW, dx + w);
        if (x1 <= x0) {
            continue;
        }
        std::memcpy(dst.data() + static_cast<std::size_t>(row) * dstW + x0,
                    src + static_cast<std::size_t>(y) * srcStride + (x0 - dx),
                    static_cast<std::size_t>(x1 - x0) * sizeof(Pel));
    }
}

}  // namespace

void captureInter(TComPic* pic, const TComYuv* predYuv, const TComYuv* resiYuv, int cuX, int cuY,
                  int cuW, int cuH) {
    if (pic == nullptr || predYuv == nullptr) {
        return;
    }
    Capture& c = captureFor(pic);
    const ChromaFormat fmt = pic->getChromaFormat();
    const UInt numComp = ::getNumberValidComponents(fmt);
    for (UInt comp = 0; comp < numComp; ++comp) {
        const ComponentID id = ComponentID(comp);
        const int sx = static_cast<int>(::getComponentScaleX(id, fmt));
        const int sy = static_cast<int>(::getComponentScaleY(id, fmt));
        const int ax = cuX >> sx;
        const int ay = cuY >> sy;
        const int aw = cuW >> sx;
        const int ah = cuH >> sy;
        std::vector<Pel>* pdst = (comp == COMPONENT_Y) ? &c.pred.y
                                : (comp == COMPONENT_Cb) ? &c.pred.u
                                                         : &c.pred.v;
        std::vector<Pel>* rdst = (comp == COMPONENT_Y) ? &c.resi.y
                                : (comp == COMPONENT_Cb) ? &c.resi.u
                                                         : &c.resi.v;
        // Chroma planes are sub-sampled: bound against each plane's own size.
        const int pw = c.pred.width >> sx;
        const int ph = c.pred.height >> sy;
        copyRegion(*pdst, pw, ph, ax, ay, predYuv->getAddr(id), predYuv->getStride(id), aw, ah);
        if (resiYuv != nullptr) {
            copyRegion(*rdst, pw, ph, ax, ay, resiYuv->getAddr(id), resiYuv->getStride(id), aw,
                       ah);
        }
    }
    c.has_pred = true;
    if (resiYuv != nullptr) {
        c.has_resi = true;
    }
}

void captureIntraBlock(TComPic* pic, ComponentID comp, int picX, int picY, int width, int height,
                       const Pel* pred, int predStride, const Pel* resi, int resiStride) {
    if (pic == nullptr || pred == nullptr) {
        return;
    }
    Capture& c = captureFor(pic);
    std::vector<Pel>* pdst = (comp == COMPONENT_Y) ? &c.pred.y
                            : (comp == COMPONENT_Cb) ? &c.pred.u
                                                     : &c.pred.v;
    std::vector<Pel>* rdst = (comp == COMPONENT_Y) ? &c.resi.y
                            : (comp == COMPONENT_Cb) ? &c.resi.u
                                                     : &c.resi.v;
    const ChromaFormat fmt = pic->getChromaFormat();
    const int sx = static_cast<int>(::getComponentScaleX(comp, fmt));
    const int sy = static_cast<int>(::getComponentScaleY(comp, fmt));
    const int pw = c.pred.width >> sx;
    const int ph = c.pred.height >> sy;
    copyRegion(*pdst, pw, ph, picX, picY, pred, predStride, width, height);
    if (resi != nullptr) {
        copyRegion(*rdst, pw, ph, picX, picY, resi, resiStride, width, height);
    }
    c.has_pred = true;
    if (resi != nullptr) {
        c.has_resi = true;
    }
}

const Capture* getCapture(TComPic* pic) {
    auto it = store().find(pic);
    return it != store().end() ? &it->second : nullptr;
}

// ---- CABAC bin accounting --------------------------------------------------

namespace {

struct CuKey {
    const TComDataCU* cu;
    unsigned part;
    bool operator<(const CuKey& other) const {
        return cu < other.cu || (cu == other.cu && part < other.part);
    }
};

unsigned& binCounter() {
    static unsigned counter = 0;
    return counter;
}
std::map<CuKey, unsigned>& cuStartMap() {
    static std::map<CuKey, unsigned> m;
    return m;
}
std::map<CuKey, unsigned>& cuBitsMap() {
    static std::map<CuKey, unsigned> m;
    return m;
}
std::map<CuKey, bool>& cuQpCodedMap() {
    static std::map<CuKey, bool> m;
    return m;
}
std::map<CuKey, unsigned>& cuStartBitMap() {
    static std::map<CuKey, unsigned> m;
    return m;
}
std::map<CuKey, unsigned>& cuBitLenMap() {
    static std::map<CuKey, unsigned> m;
    return m;
}
std::map<TComPic*, unsigned>& sliceDataBitMap() {
    static std::map<TComPic*, unsigned> m;
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

}  // namespace

void addBins(unsigned count) { binCounter() += count; }
unsigned binCount() { return binCounter(); }

void markCuStart(const TComDataCU* cu, unsigned absPartIdx, unsigned bitPos) {
    cuStartMap()[CuKey{cu, absPartIdx}] = binCounter();
    cuStartBitMap()[CuKey{cu, absPartIdx}] = bitPos + activeSubstreamBase();
}

void recordCuBits(const TComDataCU* cu, unsigned absPartIdx, unsigned bitPos) {
    auto it = cuStartMap().find(CuKey{cu, absPartIdx});
    if (it != cuStartMap().end()) {
        cuBitsMap()[CuKey{cu, absPartIdx}] = binCounter() - it->second;
        cuStartMap().erase(it);
    }
    const unsigned abs = bitPos + activeSubstreamBase();
    auto bitIt = cuStartBitMap().find(CuKey{cu, absPartIdx});
    if (bitIt != cuStartBitMap().end()) {
        cuBitLenMap()[CuKey{cu, absPartIdx}] = abs >= bitIt->second ? abs - bitIt->second : 0;
    }
}

unsigned cuBits(const TComDataCU* cu, unsigned absPartIdx) {
    auto it = cuBitsMap().find(CuKey{cu, absPartIdx});
    return it != cuBitsMap().end() ? it->second : 0;
}

bool cuBitRange(const TComDataCU* cu, unsigned absPartIdx, unsigned& offset, unsigned& length) {
    const CuKey key{cu, absPartIdx};
    auto offIt = cuStartBitMap().find(key);
    auto lenIt = cuBitLenMap().find(key);
    if (offIt == cuStartBitMap().end() || lenIt == cuBitLenMap().end()) {
        return false;
    }
    offset = offIt->second;
    length = lenIt->second;
    return true;
}

void resetCuBits() {
    cuStartMap().clear();
    cuBitsMap().clear();
    cuQpCodedMap().clear();
    cuStartBitMap().clear();
    cuBitLenMap().clear();
    substreamBaseMap().clear();
    activeSubstream() = nullptr;
}

void recordSubstreamBase(const void* substream, unsigned base) {
    if (substream != nullptr) {
        substreamBaseMap()[substream] = base;
    }
}

void setActiveSubstream(const void* substream) {
    activeSubstream() = substream;
}

void recordSliceDataStart(TComPic* pic, unsigned bitPos) {
    if (pic == nullptr) {
        return;
    }
    sliceDataBitMap().emplace(pic, bitPos);
}

unsigned sliceDataStart(TComPic* pic) {
    auto it = sliceDataBitMap().find(pic);
    return it != sliceDataBitMap().end() ? it->second : 0;
}

void recordCuQpCoded(const TComDataCU* cu, unsigned absPartIdx, bool coded) {
    cuQpCodedMap()[CuKey{cu, absPartIdx}] = coded;
}

bool cuQpCoded(const TComDataCU* cu, unsigned absPartIdx) {
    auto it = cuQpCodedMap().find(CuKey{cu, absPartIdx});
    return it != cuQpCodedMap().end() ? it->second : false;
}

}  // namespace vide_hm
