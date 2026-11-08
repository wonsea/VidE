// VidE capture hook for the VVC reference decoder (VTM).
//
// DecLib::finishPicture tears down per-picture coding structures (temp buffers,
// coeffs, intermediate data) right after a picture is reconstructed. Reading
// those structures from the helper afterwards races with VTM's DPB recycling and
// crashes. This hook runs inside finishPicture (before teardown) and copies
// everything VidE needs into a global FIFO of self-contained frame captures.
//
// The helper drains the FIFO after each decode iteration. Captures are keyed by
// a monotonically increasing sequence number, never by Picture*.
//
// Bit-range convention (v0.36+): per-CU bit ranges are snapshotted from the
// CABAC reader at CU entry/exit and exposed as `bit_offset`/`bit_length`
// relative to the slice NAL RBSP (substream bases folded in). See
// external/patches/README.md.
#pragma once

#include <cstdint>
#include <vector>

#include "CommonLib/CommonDef.h"

class Picture;
class CodingUnit;

namespace vide_vvc {

// A single reconstructed coding unit (picture coordinates).
struct CuInfo {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int depth = 0;
    int qp = 0;
    int qp_coded = 0;    // 1 when this CU explicitly coded cu_qp_delta
    int pred_mode = 0;   // 0 intra, 1 inter, 2 skip, 3 unknown
    int mv0x = 0, mv0y = 0, mv1x = 0, mv1y = 0;
    int ref0 = -1, ref1 = -1;
    uint32_t bits = 0;
    // Coded bit range within the slice-data substream (0/0 = unknown).
    uint32_t bit_offset = 0;
    uint32_t bit_length = 0;
};

// A node of the real CU partition tree, captured while CABACReader::coding_tree
// descends (so parent/child links come straight from the decoder recursion,
// not from post-hoc geometry). `parent` indexes FrameCapture::nodes (-1 at a
// CTU root); leaf nodes carry the decoded CU fields.
struct CodingNode {
    int parent = -1;
    int x = 0, y = 0, w = 0, h = 0;
    int depth = 0;
    int leaf = 0;
    int pred_mode = 0;   // 0 intra, 1 inter
    int qp = 0;
    int qp_coded = 0;
    int skip = 0;
    int intra_dir = -1;
    int inter_dir = 0;
    int merge_flag = 0;
    int merge_idx = 0;
    uint32_t bits = 0;
    uint32_t bit_offset = 0;
    uint32_t bit_length = 0;
};

// A captured picture (all planes tightly packed, luma resolution).
struct FrameCapture {
    int poc = 0;
    int width = 0;
    int height = 0;
    int bit_depth = 8;
    bool is_keyframe = false;
    bool has_pred = false;
    bool has_resi = false;

    std::vector<Pel> pred_y, pred_u, pred_v;
    std::vector<Pel> resi_y, resi_u, resi_v;
    std::vector<Pel> reco_y, reco_u, reco_v;
    std::vector<CuInfo> cus;
    std::vector<CodingNode> nodes;
    // Reference POCs per list (from the first slice of the picture).
    std::vector<int> ref_l0_poc;
    std::vector<int> ref_l1_poc;
    // First slice / picture header fields.
    int slice_type = 0;
    int slice_qp = 0;
    int slice_qp_delta = 0;
    int poc_lsb = 0;
    int temporal_id = 0;
    bool is_irap = false;
    int num_ref_l0 = 0;
    int num_ref_l1 = 0;
    // Bit offset of the slice-data substream start within the slice NAL RBSP
    // (i.e. slice_header + alignment length). Maps per-CU bit ranges to the NAL.
    uint32_t slice_data_bit = 0;
};

// Called from DecLib::finishPicture; copies the picture and queues a capture.
void capturePicture(::Picture* pic);

// Pops the oldest queued capture into `out`. Returns false when empty.
bool popCapture(FrameCapture& out);

// ---- Real CU partition-tree recording (called from CABACReader::coding_tree) --
// `cs`/`cu` are opaque CodingStructure*/CodingUnit*. `active` is false for
// chroma-tree calls, which push nothing.
struct CodingNodeScope {
    CodingNodeScope(const void* cs, int x, int y, int w, int h, bool active);
    ~CodingNodeScope();
    const void* cs_ = nullptr;
    bool active_ = false;
};
void attachCodingNode(const void* cs, const void* cu);

// Records that a CU explicitly coded cu_qp_delta. Called from CABACReader right
// at the cu_qp_delta() call (before cuCtx.isDQPCoded is set true). `cu` is a
// CodingUnit* passed as void* to keep VTM headers out of this interface.
void recordCuQpCoded(const void* cu);

// ---- CABAC bin accounting (per-CU bit counts) ------------------------------
// BinDecoderBase calls addBins() for every bin read; DecCu marks each CU start
// and records its bin delta, which the capture attaches to the CU. `bitPos` is
// CABACReader::get_num_bits_read() (bits consumed in the slice-data substream).
void addBins(unsigned count);
unsigned binCount();
void markCuStart(const ::CodingUnit* cu, unsigned bitPos);
void recordCuBits(const ::CodingUnit* cu, unsigned bitPos);

// Records the slice-data substream start bit (relative to the slice NAL RBSP)
// for a picture. Called from DecSlice::decompressSlice; the first slice wins.
void recordSliceDataStart(const ::Picture* pic, unsigned bitPos);

// Per-substream (tiles / wavefront entry points) base bit within the slice NAL
// RBSP. `substream` is the InputBitstream* handed to the CABAC reader.
void recordSubstreamBase(const void* substream, unsigned base);
// Marks the substream the CABAC reader is currently using (CABACReader::initBitstream).
void setActiveSubstream(const void* substream);

}  // namespace vide_vvc
