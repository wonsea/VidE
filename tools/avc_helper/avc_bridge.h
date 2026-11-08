// C bridge over JM's decoder.
//
// JM's headers are C and clash with C++ standard headers, so the decoder is
// driven from this C translation unit, which exposes a small POD-only API to
// the C++ helper.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AvcMbInfo {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t qp;
    int32_t block_type;   // 0 intra, 1 inter, 2 skip, 3 unknown
    int32_t qp_coded;     // 1 when this MB coded an explicit mb_qp_delta
    int32_t ref0;
    int32_t ref1;
    int32_t mv0x;
    int32_t mv0y;
    int32_t mv1x;
    int32_t mv1y;
    uint32_t bits;
    uint32_t bit_offset;  // NAL-RBSP-relative bit range (v0.39)
    uint32_t bit_length;
    int32_t slice_index;  // global VCL slice ordinal (v0.46)
    int32_t b8mode[4];
    int32_t b8pdir[4];
} AvcMbInfo;

typedef struct AvcSliceInfo {
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
    int32_t slice_index;  // global VCL slice ordinal (v0.46)
} AvcSliceInfo;

// Initialises the JM decoder for an Annex-B file. Returns 0 on success.
int avc_open(const char* path);

// Decodes the next frame. On success returns 0 and fills:
//   *out_width/*out_height, *out_bit_depth, *out_poc
//   *out_chroma_width/*out_chroma_height
//   *out_reco_*  (tightly packed 8-bit Y/Cb/Cr, cropped display resolution)
//   mb_out[0..mb_capacity) with the frame's macroblocks; *out_mb_count set.
//   *slice_out with the first slice header of the frame.
// Returns 1 at end of stream, <0 on error. Caller frees *out_reco_y/u/v.
int avc_decode_next(int* out_width, int* out_height, int* out_bit_depth, int* out_poc,
                    uint8_t** out_reco_y, uint8_t** out_reco_u, uint8_t** out_reco_v,
                    int* out_chroma_width, int* out_chroma_height,
                    AvcMbInfo* mb_out, int mb_capacity, int* out_mb_count,
                    AvcSliceInfo* slices_out, int slice_capacity, int* out_slice_count,
                    int16_t** out_pred_y, int16_t** out_pred_u, int16_t** out_pred_v,
                    int16_t** out_resi_y, int16_t** out_resi_u, int16_t** out_resi_v,
                    int* out_has_pred);

// Releases decoder state.
void avc_close(void);

// Sequence-parameter-set fields for the syntax view. Returns 0 on success.
int avc_sps_info(int* profile_idc, int* level_idc, int* chroma_format_idc,
                 int* bit_depth_luma_minus8, int* width_mbs_minus1,
                 int* height_map_units_minus1);

#ifdef __cplusplus
}
#endif
