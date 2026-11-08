// VidE AV1 capture hook implementation (see av1_capture.h).
#include "av1_capture.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "av1/common/blockd.h"
#include "av1/common/onyxc_int.h"
#include "av1/common/seg_common.h"
#include "aom_dsp/bitreader.h"
#include "aom_ports/mem.h"

#define VIDE_AV1_MAX_BLOCKS 262144
#define VIDE_AV1_MAX_QUEUE 4

static Av1BlockInfo g_blocks[VIDE_AV1_MAX_BLOCKS];
static int g_block_count = 0;

static Av1FrameCapture g_queue[VIDE_AV1_MAX_QUEUE];
static int g_head = 0;
static int g_count = 0;

// Per-block bit accounting (see av1_capture.h). aom_reader_tell() reports the
// cumulative bit position, so a decode_block()'s cost is the delta across it.
static uint32_t g_block_start_bits = 0;
static int g_block_start_index = 0;
static const uint8_t* g_frame_data = NULL;  // VidE: frame compressed-data base

void vide_av1_capture_set_frame_data(const void* data) {
    g_frame_data = (const uint8_t*)data;
}

static uint32_t av1_frame_relative_tell(const aom_reader* r) {
    if (r == NULL) {
        return 0;
    }
    const uint32_t tell = aom_reader_tell(r);
    if (g_frame_data != NULL && r->buffer_start >= g_frame_data) {
        return (uint32_t)((r->buffer_start - g_frame_data) * 8) + tell;
    }
    return tell;
}

void vide_av1_capture_block_begin(const void* reader) {
    const aom_reader* r = (const aom_reader*)reader;
    g_block_start_bits = av1_frame_relative_tell(r);
    g_block_start_index = g_block_count;
}

void vide_av1_capture_block_end(const void* reader) {
    const aom_reader* r = (const aom_reader*)reader;
    if (r == NULL || g_block_count <= g_block_start_index) {
        return;  // this invocation did not record a block
    }
    const uint32_t end = av1_frame_relative_tell(r);
    g_blocks[g_block_start_index].bits =
        (end >= g_block_start_bits) ? (end - g_block_start_bits) : 0;
    if (end >= g_block_start_bits) {
        g_blocks[g_block_start_index].bit_offset = g_block_start_bits;
        g_blocks[g_block_start_index].bit_length = end - g_block_start_bits;
    }
}

// Real partition tree (see av1_capture.h).
#define VIDE_AV1_MAX_DEPTH 64
static Av1Node* g_nodes = NULL;
static int g_node_count = 0;
static int g_node_cap = 0;
static int g_node_stack[VIDE_AV1_MAX_DEPTH];
static int g_node_depth = 0;

static int node_reserve(int need) {
    if (need <= g_node_cap) {
        return 1;
    }
    int cap = g_node_cap > 0 ? g_node_cap : 4096;
    while (cap < need) {
        cap *= 2;
    }
    Av1Node* grown = (Av1Node*)realloc(g_nodes, (size_t)cap * sizeof(Av1Node));
    if (grown == NULL) {
        return 0;
    }
    g_nodes = grown;
    g_node_cap = cap;
    return 1;
}

void vide_av1_capture_partition_begin(int mi_row, int mi_col, int size) {
    const int parent = (g_node_depth > 0) ? g_node_stack[g_node_depth - 1] : -1;
    int my_index = parent;
    if (node_reserve(g_node_count + 1)) {
        Av1Node* node = &g_nodes[g_node_count];
        node->parent = parent;
        node->x = mi_col * 4;  // MI_SIZE
        node->y = mi_row * 4;
        node->w = size;
        node->h = size;
        node->depth = g_node_depth;
        node->block_index = -1;
        my_index = g_node_count;
        ++g_node_count;
    }
    g_node_stack[g_node_depth < VIDE_AV1_MAX_DEPTH ? g_node_depth : VIDE_AV1_MAX_DEPTH - 1] =
        my_index;
    if (g_node_depth < VIDE_AV1_MAX_DEPTH) {
        ++g_node_depth;
    }
}

void vide_av1_capture_partition_end(void) {
    if (g_node_depth > 0) {
        --g_node_depth;
    }
}

static void block_dims(BLOCK_SIZE bsize, int* w, int* h) {
    *w = block_size_wide[bsize];
    *h = block_size_high[bsize];
    if (*w <= 0) *w = 4;
    if (*h <= 0) *h = 4;
}

// Prediction / residual capture state (8-bit only).
static const YV12_BUFFER_CONFIG* g_frame_yv12 = NULL;

void vide_av1_capture_block(AV1_COMMON* cm, MACROBLOCKD* xd, int mi_row, int mi_col) {
    {
        FILE* d = getenv("VIDE_AV1_DBG") ? fopen("av1_capture.log", "a") : NULL;
        if (d) { fprintf(d, "block cm=%p xd=%p mi=%p\n", (void*)cm, (void*)xd,
                         xd ? (void*)xd->mi : NULL); fflush(d); fclose(d); }
    }
    if (xd == NULL || xd->mi == NULL || xd->mi[0] == NULL ||
        g_block_count >= VIDE_AV1_MAX_BLOCKS) {
        return;
    }
    g_frame_yv12 = get_frame_new_buffer(cm);
    const MB_MODE_INFO* mbmi = &xd->mi[0]->mbmi;

    int w = 0, h = 0;
    block_dims(mbmi->sb_type, &w, &h);

    Av1BlockInfo* b = &g_blocks[g_block_count++];
    memset(b, 0, sizeof(*b));
    b->x = mi_col * MI_SIZE;
    b->y = mi_row * MI_SIZE;
    b->w = w;
    b->h = h;
    b->qp = av1_get_qindex(&cm->seg, mbmi->segment_id, cm->base_qindex);
    // AV1 codes a per-block QP only via (a) delta_q, signalled at the top-left MI
    // of each superblock when delta_q_present_flag is set, or (b) a segment with
    // an active alt-Q. Otherwise the QP is inherited from base/previous.
    {
        const int delta_q_here = cm->delta_q_present_flag &&
                                 (mi_col & MAX_MIB_MASK) == 0 && (mi_row & MAX_MIB_MASK) == 0 &&
                                 !(mbmi->skip && mbmi->sb_type == BLOCK_LARGEST);
        const int seg_alt_q = segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_ALT_Q);
        if (cm->delta_q_present_flag) {
            b->qp = xd->current_qindex;  // reflect the running delta-Q
        }
        b->qp_coded = (delta_q_here || seg_alt_q) ? 1 : 0;
    }
    b->block_type = mbmi->skip ? 2 : (is_inter_block(mbmi) ? 1 : 0);
    b->ref0 = mbmi->ref_frame[0];
    b->ref1 = mbmi->ref_frame[1];
    // mv stored in 1/8-pel; convert to quarter-pel.
    b->mv0x = mbmi->mv[0].as_mv.col / 8;
    b->mv0y = mbmi->mv[0].as_mv.row / 8;
    b->mv1x = mbmi->mv[1].as_mv.col / 8;
    b->mv1y = mbmi->mv[1].as_mv.row / 8;

    // Record this leaf block as a child of the current partition region.
    if (node_reserve(g_node_count + 1)) {
        Av1Node* node = &g_nodes[g_node_count];
        node->parent = (g_node_depth > 0) ? g_node_stack[g_node_depth - 1] : -1;
        node->x = b->x;
        node->y = b->y;
        node->w = w;
        node->h = h;
        node->depth = g_node_depth;
        node->block_index = g_block_count - 1;
        ++g_node_count;
    }
}

// ---- prediction / residual capture (8-bit) --------------------------------
static int16_t* g_resi_y = NULL;
static int16_t* g_resi_u = NULL;
static int16_t* g_resi_v = NULL;
static int16_t* g_reco_y = NULL;
static int16_t* g_reco_u = NULL;
static int16_t* g_reco_v = NULL;
static int g_pw = 0, g_ph = 0, g_pcw = 0, g_pch = 0, g_bd = 8;
static int16_t g_pred_block[64 * 64];
static int g_blk_plane = -1, g_blk_x = 0, g_blk_y = 0, g_blk_w = 0, g_blk_h = 0;
static int g_blk_bytes = 1;

static const uint8_t* yv12_plane(const YV12_BUFFER_CONFIG* yv12, int plane) {
    return plane == 0 ? yv12->y_buffer
                      : (plane == 1 ? yv12->u_buffer : yv12->v_buffer);
}
static void yv12_dims(const YV12_BUFFER_CONFIG* yv12, int plane, int* w, int* h) {
    if (plane == 0) {
        *w = yv12->y_crop_width;
        *h = yv12->y_crop_height;
    } else {
        *w = yv12->uv_crop_width;
        *h = yv12->uv_crop_height;
    }
}
static int16_t* resi_plane(int plane) {
    return plane == 0 ? g_resi_y : (plane == 1 ? g_resi_u : g_resi_v);
}
static int16_t* reco_plane(int plane) {
    return plane == 0 ? g_reco_y : (plane == 1 ? g_reco_u : g_reco_v);
}

static int ensure_pred_buffers(const YV12_BUFFER_CONFIG* yv12) {
    g_bd = (yv12->flags & YV12_FLAG_HIGHBITDEPTH) ? 2 : 1;  // bytes per sample
    if (g_resi_y != NULL && g_pw == yv12->y_crop_width &&
        g_ph == yv12->y_crop_height) {
        return 1;
    }
    free(g_resi_y); free(g_resi_u); free(g_resi_v);
    free(g_reco_y); free(g_reco_u); free(g_reco_v);
    g_pw = yv12->y_crop_width;
    g_ph = yv12->y_crop_height;
    g_pcw = yv12->uv_crop_width;
    g_pch = yv12->uv_crop_height;
    g_resi_y = (int16_t*)calloc((size_t)g_pw * g_ph, sizeof(int16_t));
    g_resi_u = (int16_t*)calloc((size_t)g_pcw * g_pch, sizeof(int16_t));
    g_resi_v = (int16_t*)calloc((size_t)g_pcw * g_pch, sizeof(int16_t));
    g_reco_y = (int16_t*)malloc((size_t)g_pw * g_ph * sizeof(int16_t));
    g_reco_u = (int16_t*)malloc((size_t)g_pcw * g_pch * sizeof(int16_t));
    g_reco_v = (int16_t*)malloc((size_t)g_pcw * g_pch * sizeof(int16_t));
    return g_resi_y && g_resi_u && g_resi_v && g_reco_y && g_reco_u && g_reco_v;
}

// Reads sample (r,c) from a (possibly 16-bit) plane, using libaom's convention
// that the buffer pointer strides in samples.
static int sample_at(const uint8_t* buf, int stride, int r, int c, int bytes) {
    if (bytes == 2) {
        return (int)CONVERT_TO_SHORTPTR(buf)[(size_t)r * stride + c];
    }
    return (int)buf[(size_t)r * stride + c];
}

void vide_av1_capture_residual_begin(int plane, const void* dst_ptr, int stride,
                                     int w, int h) {
    g_blk_plane = -1;
    if (g_frame_yv12 == NULL || plane < 0 || plane > 2) return;
    if (!ensure_pred_buffers(g_frame_yv12)) return;
    const int bytes = g_bd;
    const uint8_t* base = yv12_plane(g_frame_yv12, plane);
    const uint8_t* dst = (const uint8_t*)dst_ptr;
    if (base == NULL || dst == NULL) return;
    long off;
    if (bytes == 2) {
        off = (long)(CONVERT_TO_SHORTPTR(dst) - CONVERT_TO_SHORTPTR(base));
    } else {
        off = (long)(dst - base);
    }
    if (off < 0) return;
    int pw = 0, ph = 0;
    yv12_dims(g_frame_yv12, plane, &pw, &ph);
    int x = (int)(off % stride);
    int y = (int)(off / stride);
    if (x < 0 || y < 0 || x >= pw || y >= ph) return;
    if (x + w > pw) w = pw - x;
    if (y + h > ph) h = ph - y;
    if (w <= 0 || h <= 0 || w > 64 || h > 64) return;
    g_blk_plane = plane;
    g_blk_x = x;
    g_blk_y = y;
    g_blk_w = w;
    g_blk_h = h;
    g_blk_bytes = bytes;
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            g_pred_block[(size_t)r * w + c] = (int16_t)sample_at(dst, stride, r, c, bytes);
        }
    }
}

void vide_av1_capture_residual_end(int plane, const void* dst_ptr, int stride,
                                   int w, int h) {
    (void)w;
    (void)h;
    if (g_blk_plane != plane || g_blk_plane < 0 || g_frame_yv12 == NULL) return;
    int pw = 0, ph = 0;
    yv12_dims(g_frame_yv12, plane, &pw, &ph);
    const uint8_t* dst = (const uint8_t*)dst_ptr;
    int16_t* resi = resi_plane(plane);
    if (dst == NULL || resi == NULL) return;
    const int bw = g_blk_w;
    const int bh = g_blk_h;
    const int bytes = g_blk_bytes;
    for (int r = 0; r < bh; ++r) {
        for (int c = 0; c < bw; ++c) {
            const int pred = g_pred_block[(size_t)r * bw + c];
            const int reco = sample_at(dst, stride, r, c, bytes);
            resi[(size_t)(g_blk_y + r) * pw + (g_blk_x + c)] =
                (int16_t)(reco - pred);
        }
    }
    g_blk_plane = -1;
}

void vide_av1_capture_prefilter(const void* frame_yv12_ptr) {
    if (frame_yv12_ptr == NULL) return;
    const YV12_BUFFER_CONFIG* yv12 = (const YV12_BUFFER_CONFIG*)frame_yv12_ptr;
    if (!ensure_pred_buffers(yv12)) return;
    const int bytes = g_bd;
    for (int r = 0; r < g_ph; ++r) {
        for (int c = 0; c < g_pw; ++c) {
            g_reco_y[(size_t)r * g_pw + c] =
                (int16_t)sample_at(yv12->y_buffer, yv12->y_stride, r, c, bytes);
        }
    }
    for (int r = 0; r < g_pch; ++r) {
        for (int c = 0; c < g_pcw; ++c) {
            g_reco_u[(size_t)r * g_pcw + c] =
                (int16_t)sample_at(yv12->u_buffer, yv12->uv_stride, r, c, bytes);
            g_reco_v[(size_t)r * g_pcw + c] =
                (int16_t)sample_at(yv12->v_buffer, yv12->uv_stride, r, c, bytes);
        }
    }
}

// Records the block grid for one finished frame (reco is fetched by the bridge
// from libaom's public output, so this only carries block info + geometry).
void vide_av1_capture_frame(AV1_COMMON* cm, const void* yv12_ptr, int is_keyframe) {
    if (cm == NULL || yv12_ptr == NULL) {
        return;
    }
    // Hidden frames (show_frame == 0) are reference-only and never output; skip
    // them so the pack matches the displayed frame order, but reset the
    // per-frame accumulators so nothing leaks into the next frame.
    if (!cm->show_frame) {
        g_block_count = 0;
        g_node_count = 0;
        g_node_depth = 0;
        if (g_resi_y != NULL) {
            memset(g_resi_y, 0, (size_t)g_pw * g_ph * sizeof(int16_t));
            memset(g_resi_u, 0, (size_t)g_pcw * g_pch * sizeof(int16_t));
            memset(g_resi_v, 0, (size_t)g_pcw * g_pch * sizeof(int16_t));
        }
        return;
    }
    if (g_count >= VIDE_AV1_MAX_QUEUE) {
        return;
    }
    const YV12_BUFFER_CONFIG* yv12 = (const YV12_BUFFER_CONFIG*)yv12_ptr;
    Av1FrameCapture* cap = &g_queue[(g_head + g_count) % VIDE_AV1_MAX_QUEUE];
    memset(cap, 0, sizeof(*cap));
    cap->width = yv12->y_crop_width > 0 ? yv12->y_crop_width : yv12->y_width;
    cap->height = yv12->y_crop_height > 0 ? yv12->y_crop_height : yv12->y_height;
    cap->chroma_width = yv12->uv_crop_width;
    cap->chroma_height = yv12->uv_crop_height;
    cap->bit_depth = (int)yv12->bit_depth;
    cap->is_keyframe = is_keyframe;

    const int n = g_block_count < VIDE_AV1_MAX_BLOCKS ? g_block_count : VIDE_AV1_MAX_BLOCKS;
    if (n > 0) {
        cap->blocks = (Av1BlockInfo*)malloc((size_t)n * sizeof(Av1BlockInfo));
        if (cap->blocks != NULL) {
            memcpy(cap->blocks, g_blocks, (size_t)n * sizeof(Av1BlockInfo));
            cap->block_count = n;
        }
    }
    if (g_node_count > 0) {
        cap->nodes = (Av1Node*)malloc((size_t)g_node_count * sizeof(Av1Node));
        if (cap->nodes != NULL) {
            memcpy(cap->nodes, g_nodes, (size_t)g_node_count * sizeof(Av1Node));
            cap->node_count = g_node_count;
        }
    }
    // Prediction / residual: pred = pre-loop-filter reco - residual.
    if (g_reco_y != NULL && g_resi_y != NULL) {
        const size_t ly = (size_t)g_pw * g_ph;
        const size_t lc = (size_t)g_pcw * g_pch;
        cap->pred_y = (int16_t*)malloc(ly * sizeof(int16_t));
        cap->resi_y = (int16_t*)malloc(ly * sizeof(int16_t));
        cap->pred_u = (int16_t*)malloc(lc * sizeof(int16_t));
        cap->resi_u = (int16_t*)malloc(lc * sizeof(int16_t));
        cap->pred_v = (int16_t*)malloc(lc * sizeof(int16_t));
        cap->resi_v = (int16_t*)malloc(lc * sizeof(int16_t));
        if (cap->pred_y && cap->resi_y && cap->pred_u && cap->resi_u &&
            cap->pred_v && cap->resi_v) {
            for (size_t i = 0; i < ly; ++i) {
                cap->resi_y[i] = g_resi_y[i];
                cap->pred_y[i] = (int16_t)(g_reco_y[i] - g_resi_y[i]);
            }
            for (size_t i = 0; i < lc; ++i) {
                cap->resi_u[i] = g_resi_u[i];
                cap->pred_u[i] = (int16_t)(g_reco_u[i] - g_resi_u[i]);
                cap->resi_v[i] = g_resi_v[i];
                cap->pred_v[i] = (int16_t)(g_reco_v[i] - g_resi_v[i]);
            }
            cap->has_pred = 1;
            cap->has_resi = 1;
        }
        memset(g_resi_y, 0, ly * sizeof(int16_t));
        memset(g_resi_u, 0, lc * sizeof(int16_t));
        memset(g_resi_v, 0, lc * sizeof(int16_t));
    }
    g_block_count = 0;
    g_node_count = 0;
    g_node_depth = 0;
    ++g_count;
}

int vide_av1_pop_capture(Av1FrameCapture* out) {
    if (g_count <= 0) {
        return 0;
    }
    *out = g_queue[g_head];
    memset(&g_queue[g_head], 0, sizeof(g_queue[g_head]));
    g_head = (g_head + 1) % VIDE_AV1_MAX_QUEUE;
    --g_count;
    return 1;
}
