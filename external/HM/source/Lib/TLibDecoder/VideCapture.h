// VidE capture hook for the HEVC reference decoder (HM).
//
// HM's decoder reconstructs each CU into the picture but keeps the prediction
// and residual only in transient per-CU buffers; the picture-level prediction
// and residual buffers are left null. To let VidE visualise prediction and
// residual images, this hook copies each reconstructed CU's prediction and
// residual into picture-sized side buffers, keyed by TComPic*.
//
// Zero effect on decoding: the hook only reads existing buffers and stores
// copies. It is compiled into TLibDecoder (globs *.cpp) and consumed by
// vide_hevc_helper.
//
// Bit-range convention (v0.36+): per-CU bit ranges are snapshotted from the
// entropy reader at CU entry/exit and exposed as `bit_offset`/`bit_length`
// relative to the slice NAL RBSP (substream bases folded in). See
// external/patches/README.md.
#pragma once

#include <vector>

#include "TLibCommon/CommonDef.h"
#include "TLibCommon/TComPic.h"

namespace vide_hm {

struct Planes {
    int width = 0;
    int height = 0;
    std::vector<Pel> y;
    std::vector<Pel> u;
    std::vector<Pel> v;
};

struct Capture {
    int width = 0;
    int height = 0;
    int poc = -1;
    Planes pred;
    Planes resi;
    bool has_pred = false;
    bool has_resi = false;
};

// Copies the prediction/residual of an inter CU. `predYuv`/`resiYuv` hold the
// CU's samples at local coordinates; (cuX, cuY) is the CU position in the
// picture.
void captureInter(TComPic* pic, const TComYuv* predYuv, const TComYuv* resiYuv, int cuX, int cuY,
                  int cuW, int cuH);

// Copies one intra/TU block prediction/residual, positioned by picture coords.
void captureIntraBlock(TComPic* pic, ComponentID comp, int picX, int picY, int width, int height,
                       const Pel* pred, int predStride, const Pel* resi, int resiStride);

// Returns the captured planes for `pic`, or nullptr if none were recorded.
const Capture* getCapture(TComPic* pic);

// ---- CABAC bin accounting (per-CU bit counts) -----------------------------
// The entropy decoder calls addBins() for every bin read; xDecodeCU marks the
// start of each CU and xFinishDecodeCU records its bin delta.
void addBins(unsigned count);
unsigned binCount();
void markCuStart(const TComDataCU* cu, unsigned absPartIdx, unsigned bitPos);
void recordCuBits(const TComDataCU* cu, unsigned absPartIdx, unsigned bitPos);
unsigned cuBits(const TComDataCU* cu, unsigned absPartIdx);
// Bit range of a CU within the CABAC (slice-data) substream. Returns false when
// unknown. `bitPos` values come from TDecBinCABAC::getNumBitsRead().
bool cuBitRange(const TComDataCU* cu, unsigned absPartIdx, unsigned& offset, unsigned& length);
void resetCuBits();

// Slice-data substream start bit within the slice NAL RBSP (first slice wins).
void recordSliceDataStart(TComPic* pic, unsigned bitPos);
unsigned sliceDataStart(TComPic* pic);

// Per-substream (tiles / wavefront entry points) base bit within the slice NAL
// RBSP. `substream` is the TComInputBitstream* handed to the CABAC engine.
void recordSubstreamBase(const void* substream, unsigned base);
// Marks the substream the CABAC engine is currently reading (TDecBinCABAC::init).
void setActiveSubstream(const void* substream);

// Records whether a CU explicitly signalled cu_qp_delta (getdQPFlag() at
// xFinishDecodeCU). Lets the helper distinguish coded vs inherited QP.
void recordCuQpCoded(const TComDataCU* cu, unsigned absPartIdx, bool coded);
bool cuQpCoded(const TComDataCU* cu, unsigned absPartIdx);

}  // namespace vide_hm
