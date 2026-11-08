// VidE capture hook for the AV1 decoder (libaom).
//
// libaom's public decoder API exposes only pixels; VidE needs block-level info
// (mode / skip / MV / block size) plus the reconstruction. This hook records one
// entry per decoded block and, at frame end, snapshots the reconstruction into
// a FIFO that the VidE AV1 helper drains.
//
// Zero effect on decoding: it only reads existing structures and stores copies.
//
// Bit-range convention (v0.39+): each block records `bit_offset`/`bit_length`
// (relative to the frame's tile data via aom_reader_tell), used by the IVF
// bitmap view. See external/patches/README.md.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Av1BlockInfo {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t qp;
    int32_t qp_coded;    // 1 when the QP came from an explicit delta_q / segment
    int32_t block_type;  // 0 intra, 1 inter, 2 skip, 3 unknown
    int32_t ref0;
    int32_t ref1;
    int32_t mv0x;
    int32_t mv0y;
    int32_t mv1x;
    int32_t mv1y;
    uint32_t bits;  // entropy bits spent on this block (aom_reader_tell delta)
  uint32_t bit_offset;  // VidE: bit range within the frame's tile data (v0.39)
  uint32_t bit_length;
} Av1BlockInfo;

// A node of the real AV1 partition tree, recorded while decode_partition
// descends. `parent` indexes Av1FrameCapture::nodes (-1 at a superblock root);
// `block_index` >= 0 marks a leaf block (index into ::blocks).
typedef struct Av1Node {
    int32_t parent;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t depth;
    int32_t block_index;
} Av1Node;

typedef struct Av1FrameCapture {
    int32_t width;
    int32_t height;
    int32_t bit_depth;
    int32_t is_keyframe;
    int32_t chroma_width;
    int32_t chroma_height;
    uint8_t* y;
    uint8_t* u;
    uint8_t* v;
    Av1BlockInfo* blocks;
    int32_t block_count;
    Av1Node* nodes;
    int32_t node_count;
    int16_t* pred_y;  // prediction (8-bit only), tightly packed
    int16_t* pred_u;
    int16_t* pred_v;
    int16_t* resi_y;  // residual, tightly packed (128-biased on display)
    int16_t* resi_u;
    int16_t* resi_v;
    int32_t has_pred;
    int32_t has_resi;
} Av1FrameCapture;

struct AV1Common;
struct macroblockd;

// Called per decoded leaf block from decode_block().
void vide_av1_capture_block(struct AV1Common* cm, struct macroblockd* xd, int mi_row, int mi_col);

// Per-block bit accounting: begin() at the start of decode_block(), end() after
// the block's tokens are read. The delta is attached to the block recorded by
// vide_av1_capture_block(). `reader` is an opaque aom_reader*.
void vide_av1_capture_block_begin(const void* reader);
void vide_av1_capture_block_end(const void* reader);
// VidE: frame compressed-data base pointer (av1_decode_frame), so block bit
// offsets become frame-relative rather than tile-relative.
void vide_av1_capture_set_frame_data(const void* data);

// Real partition tree: begin() when decode_partition enters a region, end() when
// it leaves. Leaf blocks attach under the current region.
void vide_av1_capture_partition_begin(int mi_row, int mi_col, int size);
void vide_av1_capture_partition_end(void);

// Prediction / residual capture (8-bit only). Called around the residual add in
// inverse_transform_block(); the pre-loop-filter snapshot is taken once per
// frame. The helper derives pred = prefilter - residual.
void vide_av1_capture_residual_begin(int plane, const void* dst, int stride, int w, int h);
void vide_av1_capture_residual_end(int plane, const void* dst, int stride, int w, int h);
void vide_av1_capture_prefilter(const void* frame_yv12);

// Called once per frame after decoding to attach the reconstruction and queue it.
void vide_av1_capture_frame(struct AV1Common* cm, const void* yv12, int is_keyframe);

// Pops the oldest queued frame. Returns 1 and fills `out`; caller frees buffers.
int vide_av1_pop_capture(Av1FrameCapture* out);

#ifdef __cplusplus
}
#endif
