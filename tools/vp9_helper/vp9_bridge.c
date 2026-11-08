// C bridge implementation over libvpx's VP9 decoder (see vp9_bridge.h).
#include "vp9_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"

// VidE capture hook compiled into libvpx.
#include "vp9/decoder/vp9_capture.h"

static vpx_codec_ctx_t g_codec;
static FILE* g_file = NULL;
static int g_opened = 0;

int vp9_open(const char* path) {
    g_file = fopen(path, "rb");
    if (g_file == NULL) {
        return -1;
    }
    // Detect IVF: read the 32-byte IVF header if present.
    char magic[4] = {0};
    if (fread(magic, 1, 4, g_file) == 4 && memcmp(magic, "DKIF", 4) == 0) {
        unsigned char header[28];
        if (fread(header, 1, 28, g_file) != 28) {
            fclose(g_file);
            g_file = NULL;
            return -1;
        }
    } else {
        rewind(g_file);  // raw IVF-less stream: decode from the start
    }

    vpx_codec_dec_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.threads = 1;
    if (vpx_codec_dec_init(&g_codec, vpx_codec_vp9_dx(), &cfg, 0) != VPX_CODEC_OK) {
        fclose(g_file);
        g_file = NULL;
        return -1;
    }
    g_opened = 1;
    return 0;
}

int vp9_decode_next(int* out_width, int* out_height, int* out_bit_depth, int* out_keyframe,
                    uint8_t** out_y, uint8_t** out_u, uint8_t** out_v, int* out_cw, int* out_ch,
                    Vp9BlockInfoC* blocks, int block_capacity, int* out_block_count,
                    Vp9NodeC* nodes, int node_capacity, int* out_node_count,
                    int16_t** out_pred_y, int16_t** out_pred_u, int16_t** out_pred_v,
                    int16_t** out_resi_y, int16_t** out_resi_u, int16_t** out_resi_v,
                    int* out_has_pred) {
    if (!g_opened) {
        return -1;
    }
    // Drain any captured frame first (the hook snapshots during decode).
    Vp9FrameCapture cap;
    if (vide_vp9_pop_capture(&cap)) {
        *out_width = cap.width;
        *out_height = cap.height;
        *out_bit_depth = cap.bit_depth;
        *out_keyframe = cap.is_keyframe;
        *out_y = cap.y;
        *out_u = cap.u;
        *out_v = cap.v;
        *out_cw = cap.chroma_width;
        *out_ch = cap.chroma_height;
        if (out_pred_y != NULL) {
            *out_pred_y = cap.pred_y;
            *out_pred_u = cap.pred_u;
            *out_pred_v = cap.pred_v;
            *out_resi_y = cap.resi_y;
            *out_resi_u = cap.resi_u;
            *out_resi_v = cap.resi_v;
            *out_has_pred = cap.has_pred;
        }
        const int n = cap.block_count < block_capacity ? cap.block_count : block_capacity;
        for (int i = 0; i < n; ++i) {
            Vp9BlockInfoC* o = &blocks[i];
            o->x = cap.blocks[i].x;
            o->y = cap.blocks[i].y;
            o->w = cap.blocks[i].w;
            o->h = cap.blocks[i].h;
            o->qp = cap.blocks[i].qp;
            o->qp_coded = cap.blocks[i].qp_coded;
            o->block_type = cap.blocks[i].block_type;
            o->ref0 = cap.blocks[i].ref0;
            o->ref1 = cap.blocks[i].ref1;
            o->mv0x = cap.blocks[i].mv0x;
            o->mv0y = cap.blocks[i].mv0y;
            o->mv1x = cap.blocks[i].mv1x;
            o->mv1y = cap.blocks[i].mv1y;
            o->bits = cap.blocks[i].bits;
            o->bit_offset = cap.blocks[i].bit_offset;
            o->bit_length = cap.blocks[i].bit_length;
        }
        *out_block_count = n;
        const int nn = cap.node_count < node_capacity ? cap.node_count : node_capacity;
        for (int i = 0; i < nn; ++i) {
            Vp9NodeC* o = &nodes[i];
            o->parent = cap.nodes[i].parent;
            o->x = cap.nodes[i].x;
            o->y = cap.nodes[i].y;
            o->w = cap.nodes[i].w;
            o->h = cap.nodes[i].h;
            o->depth = cap.nodes[i].depth;
            o->block_index = cap.nodes[i].block_index;
        }
        *out_node_count = nn;
        free(cap.nodes);
        free(cap.blocks);
        return 0;
    }

    // Read the next IVF frame (12-byte header: size(4) + timestamp(8)).
    unsigned char frame_header[12];
    if (fread(frame_header, 1, 12, g_file) != 12) {
        return 1;  // end of stream
    }
    const uint32_t frame_size = (uint32_t)frame_header[0] | ((uint32_t)frame_header[1] << 8) |
                                ((uint32_t)frame_header[2] << 16) |
                                ((uint32_t)frame_header[3] << 24);
    if (frame_size == 0 || frame_size > (64u << 20)) {
        return 1;
    }
    unsigned char* buf = (unsigned char*)malloc(frame_size);
    if (buf == NULL) {
        return -2;
    }
    if (fread(buf, 1, frame_size, g_file) != frame_size) {
        free(buf);
        return 1;
    }
    const vpx_codec_err_t err = vpx_codec_decode(&g_codec, buf, frame_size, NULL, 0);
    free(buf);
    if (err != VPX_CODEC_OK) {
        return -2;
    }

    // The capture hook fires inside vpx_codec_decode; pop it now.
    if (vide_vp9_pop_capture(&cap)) {
        *out_width = cap.width;
        *out_height = cap.height;
        *out_bit_depth = cap.bit_depth;
        *out_keyframe = cap.is_keyframe;
        *out_y = cap.y;
        *out_u = cap.u;
        *out_v = cap.v;
        *out_cw = cap.chroma_width;
        *out_ch = cap.chroma_height;
        if (out_pred_y != NULL) {
            *out_pred_y = cap.pred_y;
            *out_pred_u = cap.pred_u;
            *out_pred_v = cap.pred_v;
            *out_resi_y = cap.resi_y;
            *out_resi_u = cap.resi_u;
            *out_resi_v = cap.resi_v;
            *out_has_pred = cap.has_pred;
        }
        const int n = cap.block_count < block_capacity ? cap.block_count : block_capacity;
        for (int i = 0; i < n; ++i) {
            Vp9BlockInfoC* o = &blocks[i];
            o->x = cap.blocks[i].x;
            o->y = cap.blocks[i].y;
            o->w = cap.blocks[i].w;
            o->h = cap.blocks[i].h;
            o->qp = cap.blocks[i].qp;
            o->qp_coded = cap.blocks[i].qp_coded;
            o->block_type = cap.blocks[i].block_type;
            o->ref0 = cap.blocks[i].ref0;
            o->ref1 = cap.blocks[i].ref1;
            o->mv0x = cap.blocks[i].mv0x;
            o->mv0y = cap.blocks[i].mv0y;
            o->mv1x = cap.blocks[i].mv1x;
            o->mv1y = cap.blocks[i].mv1y;
            o->bits = cap.blocks[i].bits;
            o->bit_offset = cap.blocks[i].bit_offset;
            o->bit_length = cap.blocks[i].bit_length;
        }
        *out_block_count = n;
        const int nn = cap.node_count < node_capacity ? cap.node_count : node_capacity;
        for (int i = 0; i < nn; ++i) {
            Vp9NodeC* o = &nodes[i];
            o->parent = cap.nodes[i].parent;
            o->x = cap.nodes[i].x;
            o->y = cap.nodes[i].y;
            o->w = cap.nodes[i].w;
            o->h = cap.nodes[i].h;
            o->depth = cap.nodes[i].depth;
            o->block_index = cap.nodes[i].block_index;
        }
        *out_node_count = nn;
        free(cap.nodes);
        free(cap.blocks);
        return 0;
    }
    return 2;  // frame decoded but not captured (retry)
}

void vp9_close(void) {
    if (g_opened) {
        vpx_codec_destroy(&g_codec);
        g_opened = 0;
    }
    if (g_file != NULL) {
        fclose(g_file);
        g_file = NULL;
    }
}
