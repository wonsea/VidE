// C bridge over libaom's AV1 decoder for the VidE AV1 helper.
//
// libaom's headers are C and clash with C++ standard headers, so all libaom
// access goes through this C translation unit. Block/reco data comes from the
// VidE capture hook compiled into libaom (av1_capture.c).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Av1BlockInfoC {
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
} Av1BlockInfoC;

typedef struct Av1NodeC {
    int32_t parent;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t depth;
    int32_t block_index;
} Av1NodeC;

// Opens an AV1 stream (low-overhead OBU / IVF). Returns 0 on success.
int av1_open(const char* path);

// Prediction / residual planes of the most recently decoded frame (8-bit input
// only). Ownership is transferred to the caller via av1_take_planes(); the
// caller must free the pointers.
typedef struct Av1PlanesC {
    int32_t width;
    int32_t height;
    int32_t cw;
    int32_t ch;
    int16_t* pred_y;
    int16_t* pred_u;
    int16_t* pred_v;
    int16_t* resi_y;
    int16_t* resi_u;
    int16_t* resi_v;
    int32_t has_pred;
    int32_t has_resi;
} Av1PlanesC;
void av1_take_planes(Av1PlanesC* out);

// Decodes the next frame. Returns 0 and fills the outputs (caller frees y/u/v),
// 1 at end of stream, <0 on error.
int av1_decode_next(int* out_width, int* out_height, int* out_bit_depth, int* out_keyframe,
                    uint8_t** out_y, uint8_t** out_u, uint8_t** out_v, int* out_cw, int* out_ch,
                    Av1BlockInfoC* blocks, int block_capacity, int* out_block_count,
                    Av1NodeC* nodes, int node_capacity, int* out_node_count);

void av1_close(void);

#ifdef __cplusplus
}
#endif
