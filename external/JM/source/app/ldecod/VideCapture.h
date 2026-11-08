// VidE capture hook for the JM (H.264) reference decoder.
//
// JM finishes a picture in exit_picture(), then hands it to the DPB (which may
// recycle buffers). VidE's helper needs the reconstructed samples and the
// per-macroblock coding info, so this hook snapshots the finished picture into
// a FIFO right before it leaves exit_picture().
//
// Zero effect on decoding: it only reads existing structures and stores copies.
//
// Bit-range convention (v0.39+): each macroblock records `bit_offset`/
// `bit_length` (relative to its slice NAL RBSP) plus a global VCL slice ordinal
// (`slice_index`), so multi-slice frames map every block to its own NAL. See
// external/patches/README.md.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JmMbInfo {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t qp;
    int32_t block_type;  // 0 intra, 1 inter, 2 skip, 3 unknown
    int32_t qp_coded;    // 1 when this MB coded an explicit mb_qp_delta
    int32_t ref0;
    int32_t ref1;
    int32_t mv0x;
    int32_t mv0y;
    int32_t mv1x;
    int32_t mv1y;
    uint32_t bits;
    uint32_t bit_offset;  // VidE: NAL-RBSP-relative bit range (v0.39)
    uint32_t bit_length;
    int32_t slice_index;  // VidE: global VCL slice ordinal of this MB (v0.46)
    int32_t b8mode[4];  // per-8x8 partition mode
    int32_t b8pdir[4];  // per-8x8 prediction direction (0/1/2)
} JmMbInfo;

// One AVC slice header (H.264 7.3.3), captured per slice.
typedef struct JmSliceInfo {
    int32_t slice_type;
    int32_t nal_ref_idc;
    int32_t idr_flag;
    int32_t idr_pic_id;
    int32_t pic_parameter_set_id;
    uint32_t frame_num;
    int32_t field_pic_flag;
    int32_t bottom_field_flag;
    uint32_t pic_order_cnt_lsb;
    int32_t delta_pic_order_cnt_bottom;
    int32_t delta_pic_order_cnt0;
    int32_t delta_pic_order_cnt1;
    int32_t redundant_pic_cnt;
    int32_t num_ref_idx_l0_active;
    int32_t num_ref_idx_l1_active;
    int32_t slice_qp_delta;
    int32_t slice_qp;
    int32_t ref_l0_poc[16];
    int32_t ref_l1_poc[16];
    int32_t ref_l0_count;
    int32_t ref_l1_count;
    int32_t slice_index;  // VidE: global VCL slice ordinal (v0.46)
} JmSliceInfo;

typedef struct JmFrameCapture {
    int32_t width;       // cropped (display) luma width
    int32_t height;      // cropped (display) luma height
    int32_t bit_depth;   // source bit depth (8..16) of the coded stream
    int32_t poc;
    int32_t is_keyframe;
    int32_t chroma_width;   // cropped chroma width
    int32_t chroma_height;  // cropped chroma height
    uint8_t* y;  // tightly packed 8-bit (normalised), width x height
    uint8_t* u;  // tightly packed 8-bit (normalised), chroma_width x chroma_height
    uint8_t* v;
    JmMbInfo* mbs;
    int32_t mb_count;

    // First slice header of this picture (H.264 7.3.3).
    int32_t slice_type;
    int32_t nal_ref_idc;
    int32_t idr_flag;
    int32_t idr_pic_id;
    int32_t pic_parameter_set_id;
    uint32_t frame_num;
    int32_t field_pic_flag;
    int32_t bottom_field_flag;
    uint32_t pic_order_cnt_lsb;
    int32_t delta_pic_order_cnt_bottom;
    int32_t delta_pic_order_cnt0;
    int32_t delta_pic_order_cnt1;
    int32_t redundant_pic_cnt;
    int32_t num_ref_idx_l0_active;
    int32_t num_ref_idx_l1_active;
    int32_t slice_qp_delta;
    int32_t slice_qp;

    // All slice headers of this picture (H.264 pictures may have multiple slices).
    JmSliceInfo* slices;
    int32_t slice_count;

    // Per-frame prediction and residual (cropped display size), accumulated
    // per macroblock. Signed 16-bit so residual can be negative.
    int16_t* pred_y;
    int16_t* pred_u;
    int16_t* pred_v;
    int16_t* resi_y;
    int16_t* resi_u;
    int16_t* resi_v;
    int32_t has_pred;
    int32_t has_resi;
} JmFrameCapture;

// Called from exit_picture(); copies the finished picture and queues it.
struct storable_picture;
struct video_par;
struct slice;
struct macroblock_dec;
void vide_jm_capture_picture(struct video_par* p_Vid, struct storable_picture* pic);

// Records the prediction and residual of the macroblock just decoded (called
// from the MB loop after decode_one_macroblock). Accumulates into per-frame
// buffers attached to the next vide_jm_capture_picture().
void vide_jm_capture_mb_pred_resi(const struct video_par* p_Vid, const struct slice* slice,
                                  const struct macroblock_dec* mb);

// Records the first slice header of the current picture (called from the MB loop
// when the slice's first macroblock is decoded). Fields are attached to the next
// vide_jm_capture_picture().
void vide_jm_capture_slice_info(const struct slice* slice);

// Pops the oldest queued capture. Returns 1 and fills `out` when available;
// the caller owns the buffers and must free y/u/v/mbs.
int vide_jm_pop_capture(JmFrameCapture* out);

// VidE per-MB bin accounting. `vide_jm_bins` is incremented by the CABAC
// primitives; the per-macroblock decode loop records each MB's delta via
// vide_jm_capture_mb_bits(mb_index, ...).
extern unsigned int vide_jm_bins;
void vide_jm_capture_mb_bits(int mb_index, unsigned int bins);

// VidE per-MB bit range (NAL-RBSP-relative). The slice's CABAC data start byte
// (within the RBSP after the NAL header) is recorded once via
// vide_jm_set_cabac_base(); each MB records its [offset, offset+length) range.
void vide_jm_set_cabac_base(int byte_start_pos);
unsigned int vide_jm_cabac_base(void);
void vide_jm_capture_mb_bit_range(int mb_index, unsigned int bit_offset, unsigned int bit_length);

// VidE per-MB global VCL slice ordinal (AVC multi-slice pairing). The current
// slice's ordinal is exposed by vide_jm_current_slice_index(); the MB loop tags
// each macroblock with it.
int vide_jm_current_slice_index(void);
void vide_jm_capture_mb_slice(int mb_index, int slice_index);

// VidE per-MB QP-delta presence. read_delta_quant() flags the MB that coded an
// explicit mb_qp_delta; the capture reads it via vide_jm_mb_qp_coded().
void vide_jm_capture_mb_qp_coded(int mb_index, int coded);
int vide_jm_mb_qp_coded(int mb_index);

#ifdef __cplusplus
}
#endif
