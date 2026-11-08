// VidE capture hook for the VP9 decoder (libvpx).
//
// libvpx's public decoder API exposes only pixels; VidE needs block-level info
// (mode / skip / MV / block size) plus the reconstruction. This hook records one
// entry per decoded block and, at frame end, snapshots the reconstruction into
// a FIFO that the VidE VP9 helper drains.
//
// Zero effect on decoding: it only reads existing structures and stores copies.
//
// Bit-range convention (v0.39+): each block records `bit_offset`/`bit_length`
// (relative to the frame's compressed data; no EPB in VP9), used by the IVF
// bitmap view. See external/patches/README.md.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Vp9BlockInfo {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t qp;
    int32_t qp_coded;    // 1 when the QP came from an explicit segment alt-Q
    int32_t block_type;  // 0 intra, 1 inter, 2 skip, 3 unknown
    int32_t ref0;
    int32_t ref1;
    int32_t mv0x;
    int32_t mv0y;
    int32_t mv1x;
    int32_t mv1y;
    uint32_t bits;  // binary symbols spent on this block
  uint32_t bit_offset;  // VidE: bit range within the frame's compressed data (v0.39)
  uint32_t bit_length;
} Vp9BlockInfo;

// A node of the real VP9 partition tree, recorded while decode_partition
// descends. `parent` indexes Vp9FrameCapture::nodes (-1 at a superblock root);
// `block_index` >= 0 marks a leaf block (index into ::blocks).
typedef struct Vp9Node {
    int32_t parent;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t depth;
    int32_t block_index;
} Vp9Node;

typedef struct Vp9FrameCapture {
    int32_t width;
    int32_t height;
    int32_t bit_depth;
    int32_t is_keyframe;
    uint8_t* y;  // tightly packed
    uint8_t* u;
    uint8_t* v;
    int32_t chroma_width;
    int32_t chroma_height;
    Vp9BlockInfo* blocks;
    int32_t block_count;
    Vp9Node* nodes;  // real partition tree (parent links from decode_partition)
    int32_t node_count;
    // Per-frame prediction and residual (luma+chroma, frame coords). Signed
    // 16-bit; residual can be negative.
    int16_t* pred_y;
    int16_t* pred_u;
    int16_t* pred_v;
    int16_t* resi_y;
    int16_t* resi_u;
    int16_t* resi_v;
    int32_t has_pred;
    int32_t has_resi;
} Vp9FrameCapture;

typedef struct VP9Common VP9_COMMON;
struct MODE_INFO;

// Called per decoded block (leaf) from decode_block().
void vide_vp9_capture_block(VP9_COMMON* cm, const struct MODE_INFO* mi, int mi_row,
                            int mi_col, int bsize);

// Per-block bit accounting: begin() before a leaf block's symbols are read,
// end() after. `reader` is an opaque vpx_reader*; the delta is attached to the
// block recorded by vide_vp9_capture_block().
void vide_vp9_capture_block_begin(const void* reader);
void vide_vp9_capture_block_end(const void* reader);
// VidE: frame compressed-data base pointer (vp9_decode_frame), so block bit
// offsets become frame-relative rather than tile-relative.
void vide_vp9_capture_set_frame_data(const void* data);

// Real partition tree: begin() when decode_partition/process_partition enters a
// region, end() when it leaves. Leaf blocks attach under the current region.
void vide_vp9_capture_partition_begin(int mi_row, int mi_col, int size);
void vide_vp9_capture_partition_end(void);

// Prediction/residual capture around the inverse transform: begin() before the
// transform (dst holds prediction), end() after (dst holds reconstruction).
void vide_vp9_capture_residual_begin(int plane, const void* dst, int stride, int w, int h);
void vide_vp9_capture_residual_end(int plane, const void* dst, int stride, int w, int h);

// Copy the pre-loop-filter reconstruction for macroblock rows [start, stop).
void vide_vp9_capture_prefilter_rows(const void* yv12, int mi_row_start, int mi_row_stop);

// Called once per frame after decoding to attach the reconstruction and queue it.
void vide_vp9_capture_frame(VP9_COMMON* cm, const void* yv12, int is_keyframe);

// Pops the oldest queued frame. Returns 1 and fills `out`; caller frees buffers.
int vide_vp9_pop_capture(Vp9FrameCapture* out);

#ifdef __cplusplus
}
#endif
