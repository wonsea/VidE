// C bridge over libvpx's VP9 decoder for the VidE VP9 helper.
//
// libvpx's headers are C and clash with C++ standard headers, so all libvpx
// access goes through this C translation unit. Block/reco data comes from the
// VidE capture hook compiled into libvpx (vp9_capture.c).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Vp9BlockInfoC {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t qp;
    int32_t qp_coded;
    int32_t block_type;
    int32_t ref0;
    int32_t ref1;
    int32_t mv0x;
    int32_t mv0y;
    int32_t mv1x;
    int32_t mv1y;
    uint32_t bits;
    uint32_t bit_offset;
    uint32_t bit_length;
} Vp9BlockInfoC;

typedef struct Vp9NodeC {
    int32_t parent;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t depth;
    int32_t block_index;
} Vp9NodeC;

// Opens a VP9 stream (IVF or raw superframes). Returns 0 on success.
int vp9_open(const char* path);

// Decodes the next frame. Returns 0 and fills the outputs (caller frees y/u/v),
// 1 at end of stream, <0 on error.
int vp9_decode_next(int* out_width, int* out_height, int* out_bit_depth, int* out_keyframe,
                    uint8_t** out_y, uint8_t** out_u, uint8_t** out_v, int* out_cw, int* out_ch,
                    Vp9BlockInfoC* blocks, int block_capacity, int* out_block_count,
                    Vp9NodeC* nodes, int node_capacity, int* out_node_count,
                    int16_t** out_pred_y, int16_t** out_pred_u, int16_t** out_pred_v,
                    int16_t** out_resi_y, int16_t** out_resi_u, int16_t** out_resi_v,
                    int* out_has_pred);

void vp9_close(void);

#ifdef __cplusplus
}
#endif
