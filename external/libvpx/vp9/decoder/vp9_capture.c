// VidE VP9 capture hook implementation (see vp9_capture.h).
#include "vp9_capture.h"

#include <stdlib.h>
#include <string.h>

#include "vp9/common/vp9_blockd.h"
#include "vp9/common/vp9_onyxc_int.h"
#include "vp9/common/vp9_seg_common.h"
#include "vpx_dsp/bitreader.h"

#define VIDE_VP9_MAX_BLOCKS 65536
#define VIDE_VP9_MAX_QUEUE 4

static Vp9BlockInfo g_blocks[VIDE_VP9_MAX_BLOCKS];
static int g_block_count = 0;

static Vp9FrameCapture g_queue[VIDE_VP9_MAX_QUEUE];
static int g_head = 0;
static int g_count = 0;

// Per-block bit accounting (see vp9_capture.h).
static unsigned int g_block_start_bins = 0;
static int g_block_start_index = 0;
static int g_block_start_bits = 0;  // VidE: reader bit position at block start
static const unsigned char* g_frame_data = 0;  // VidE: frame compressed-data base

void vide_vp9_capture_set_frame_data(const void* data) {
    g_frame_data = (const unsigned char*)data;
}

static int vp9_frame_relative_tell(const vpx_reader* r) {
    if (r == 0) {
        return 0;
    }
    const int tell = vpx_reader_tell(r);
    if (g_frame_data != 0 && r->buffer_start >= g_frame_data) {
        return (int)((r->buffer_start - g_frame_data) * 8) + tell;
    }
    return tell;
}

void vide_vp9_capture_block_begin(const void* reader) {
    const vpx_reader* r = (const vpx_reader*)reader;
    g_block_start_bins = (r != NULL) ? vpx_reader_bins(r) : 0;
    g_block_start_bits = vp9_frame_relative_tell(r);
    g_block_start_index = g_block_count;
}

void vide_vp9_capture_block_end(const void* reader) {
    const vpx_reader* r = (const vpx_reader*)reader;
    if (r == NULL || g_block_count <= g_block_start_index) {
        return;  // this invocation did not record a block
    }
    const unsigned int end = vpx_reader_bins(r);
    g_blocks[g_block_start_index].bits =
        (end >= g_block_start_bins) ? (end - g_block_start_bins) : 0;
    const int end_bits = vp9_frame_relative_tell(r);
    if (end_bits >= g_block_start_bits) {
        g_blocks[g_block_start_index].bit_offset = (uint32_t)g_block_start_bits;
        g_blocks[g_block_start_index].bit_length = (uint32_t)(end_bits - g_block_start_bits);
    }
}

// Real partition tree (see vp9_capture.h).
#define VIDE_VP9_MAX_DEPTH 64
static Vp9Node* g_nodes = NULL;
static int g_node_count = 0;
static int g_node_cap = 0;
static int g_node_stack[VIDE_VP9_MAX_DEPTH];
static int g_node_depth = 0;

static int node_reserve(int need) {
    if (need <= g_node_cap) {
        return 1;
    }
    int cap = g_node_cap > 0 ? g_node_cap : 4096;
    while (cap < need) {
        cap *= 2;
    }
    Vp9Node* grown = (Vp9Node*)realloc(g_nodes, (size_t)cap * sizeof(Vp9Node));
    if (grown == NULL) {
        return 0;
    }
    g_nodes = grown;
    g_node_cap = cap;
    return 1;
}

void vide_vp9_capture_partition_begin(int mi_row, int mi_col, int size) {
    const int parent = (g_node_depth > 0) ? g_node_stack[g_node_depth - 1] : -1;
    int my_index = parent;
    if (node_reserve(g_node_count + 1)) {
        Vp9Node* node = &g_nodes[g_node_count];
        node->parent = parent;
        node->x = mi_col * 8;
        node->y = mi_row * 8;
        node->w = size;
        node->h = size;
        node->depth = g_node_depth;
        node->block_index = -1;
        my_index = g_node_count;
        ++g_node_count;
    }
    g_node_stack[g_node_depth < VIDE_VP9_MAX_DEPTH ? g_node_depth : VIDE_VP9_MAX_DEPTH - 1] =
        my_index;
    if (g_node_depth < VIDE_VP9_MAX_DEPTH) {
        ++g_node_depth;
    }
}

void vide_vp9_capture_partition_end(void) {
    if (g_node_depth > 0) {
        --g_node_depth;
    }
}

// ---- prediction / residual accumulation (see vp9_capture.h) ----------------
static const YV12_BUFFER_CONFIG* g_pr_yv12 = NULL;
static int16_t* g_reco_y = NULL;
static int16_t* g_reco_u = NULL;
static int16_t* g_reco_v = NULL;
static int16_t* g_resi_y = NULL;
static int16_t* g_resi_u = NULL;
static int16_t* g_resi_v = NULL;
static int g_pr_w = 0, g_pr_h = 0, g_pr_cw = 0, g_pr_ch = 0;
static int g_pr_has = 0;
static int16_t g_pred_blk[64 * 64];  // scratch: prediction of one tx block

static void pr_free(void) {
    free(g_reco_y); free(g_reco_u); free(g_reco_v);
    free(g_resi_y); free(g_resi_u); free(g_resi_v);
    g_reco_y = g_reco_u = g_reco_v = NULL;
    g_resi_y = g_resi_u = g_resi_v = NULL;
    g_pr_w = g_pr_h = g_pr_cw = g_pr_ch = 0;
}

static void pr_ensure(VP9_COMMON* cm) {
    const YV12_BUFFER_CONFIG* yv = get_frame_new_buffer(cm);
    if (yv == NULL) {
        return;
    }
    g_pr_yv12 = yv;
    const int w = yv->y_crop_width;
    const int h = yv->y_crop_height;
    const int cw = yv->uv_crop_width;
    const int ch = yv->uv_crop_height;
    if (g_reco_y != NULL && w == g_pr_w && h == g_pr_h && cw == g_pr_cw && ch == g_pr_ch) {
        return;
    }
    pr_free();
    g_reco_y = (int16_t*)calloc((size_t)w * h, sizeof(int16_t));
    g_resi_y = (int16_t*)calloc((size_t)w * h, sizeof(int16_t));
    g_reco_u = (int16_t*)calloc((size_t)cw * ch, sizeof(int16_t));
    g_reco_v = (int16_t*)calloc((size_t)cw * ch, sizeof(int16_t));
    g_resi_u = (int16_t*)calloc((size_t)cw * ch, sizeof(int16_t));
    g_resi_v = (int16_t*)calloc((size_t)cw * ch, sizeof(int16_t));
    g_pr_w = w; g_pr_h = h; g_pr_cw = cw; g_pr_ch = ch;
}

// Zero the accumulators at the first block of each frame.
void vide_vp9_capture_frame_reset(VP9_COMMON* cm) {
    pr_ensure(cm);
    if (g_reco_y != NULL) {
        memset(g_reco_y, 0, (size_t)g_pr_w * g_pr_h * sizeof(int16_t));
        memset(g_resi_y, 0, (size_t)g_pr_w * g_pr_h * sizeof(int16_t));
    }
    if (g_reco_u != NULL) {
        memset(g_reco_u, 0, (size_t)g_pr_cw * g_pr_ch * sizeof(int16_t));
        memset(g_reco_v, 0, (size_t)g_pr_cw * g_pr_ch * sizeof(int16_t));
        memset(g_resi_u, 0, (size_t)g_pr_cw * g_pr_ch * sizeof(int16_t));
        memset(g_resi_v, 0, (size_t)g_pr_cw * g_pr_ch * sizeof(int16_t));
    }
    g_pr_has = 0;
}

static void plane_base_stride(int plane, const uint8_t** base, int* stride) {
    if (g_pr_yv12 == NULL) {
        *base = NULL; *stride = 0; return;
    }
    if (plane == 0) {
        *base = g_pr_yv12->y_buffer;
        *stride = g_pr_yv12->y_stride;
    } else if (plane == 1) {
        *base = g_pr_yv12->u_buffer;
        *stride = g_pr_yv12->uv_stride;
    } else {
        *base = g_pr_yv12->v_buffer;
        *stride = g_pr_yv12->uv_stride;
    }
}

void vide_vp9_capture_residual_begin(int plane, const void* dst_ptr, int stride, int w, int h) {
    (void)stride;
    if (dst_ptr == NULL || w <= 0 || h <= 0 || w > 64 || h > 64) {
        return;
    }
    const uint8_t* d = (const uint8_t*)dst_ptr;
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            g_pred_blk[(size_t)r * w + c] = (int16_t)d[(size_t)r * stride + c];
        }
    }
}

void vide_vp9_capture_residual_end(int plane, const void* dst_ptr, int stride, int w, int h) {
    if (dst_ptr == NULL || g_pr_yv12 == NULL || w <= 0 || h <= 0 || w > 64 || h > 64) {
        return;
    }
    const uint8_t* base = NULL;
    int pstride = 0;
    plane_base_stride(plane, &base, &pstride);
    if (base == NULL) {
        return;
    }
    const uint8_t* d = (const uint8_t*)dst_ptr;
    const long off = (long)(d - base);
    if (off < 0) {
        return;
    }
    const int bx = (int)(off % pstride);
    const int by = (int)(off / pstride);
    const int pw = (plane == 0) ? g_pr_w : g_pr_cw;
    const int ph = (plane == 0) ? g_pr_h : g_pr_ch;
    int16_t* resi = (plane == 0) ? g_resi_y : (plane == 1 ? g_resi_u : g_resi_v);
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            const int x = bx + c, y = by + r;
            if (x >= 0 && x < pw && y >= 0 && y < ph) {
                const int pred = g_pred_blk[(size_t)r * w + c];
                const int reco = d[(size_t)r * stride + c];
                resi[(size_t)y * pw + x] = (int16_t)(reco - pred);
            }
        }
    }
    g_pr_has = 1;
}

// Copy the pre-loop-filter reconstruction for rows [mi_row_start, mi_row_stop).
void vide_vp9_capture_prefilter_rows(const void* yv12_ptr, int mi_row_start, int mi_row_stop) {
    const YV12_BUFFER_CONFIG* yv = (const YV12_BUFFER_CONFIG*)yv12_ptr;
    if (yv == NULL || g_reco_y == NULL || g_pr_yv12 == NULL) {
        return;
    }
    int y0 = mi_row_start * 8;
    int y1 = mi_row_stop * 8;
    if (y0 < 0) y0 = 0;
    if (y1 > g_pr_h) y1 = g_pr_h;
    for (int r = y0; r < y1; ++r) {
        const uint8_t* src = yv->y_buffer + (size_t)r * yv->y_stride;
        for (int c = 0; c < g_pr_w; ++c) {
            g_reco_y[(size_t)r * g_pr_w + c] = (int16_t)src[c];
        }
    }
    const int cy0 = y0 / 2;
    const int cy1 = (y1 + 1) / 2;
    for (int r = cy0; r < cy1 && r < g_pr_ch; ++r) {
        const uint8_t* su = yv->u_buffer + (size_t)r * yv->uv_stride;
        const uint8_t* sv = yv->v_buffer + (size_t)r * yv->uv_stride;
        for (int c = 0; c < g_pr_cw; ++c) {
            g_reco_u[(size_t)r * g_pr_cw + c] = (int16_t)su[c];
            g_reco_v[(size_t)r * g_pr_cw + c] = (int16_t)sv[c];
        }
    }
}


static int block_dims(const MODE_INFO* mi, int* w, int* h) {
    switch (mi->sb_type) {
        case BLOCK_4X4: *w = 4; *h = 4; return 1;
        case BLOCK_4X8: *w = 4; *h = 8; return 1;
        case BLOCK_8X4: *w = 8; *h = 4; return 1;
        case BLOCK_8X8: *w = 8; *h = 8; return 1;
        case BLOCK_8X16: *w = 8; *h = 16; return 1;
        case BLOCK_16X8: *w = 16; *h = 8; return 1;
        case BLOCK_16X16: *w = 16; *h = 16; return 1;
        case BLOCK_16X32: *w = 16; *h = 32; return 1;
        case BLOCK_32X16: *w = 32; *h = 16; return 1;
        case BLOCK_32X32: *w = 32; *h = 32; return 1;
        case BLOCK_32X64: *w = 32; *h = 64; return 1;
        case BLOCK_64X32: *w = 64; *h = 32; return 1;
        case BLOCK_64X64: *w = 64; *h = 64; return 1;
        default: *w = 8; *h = 8; return 1;
    }
}

static int block_qp(const VP9_COMMON* cm, const MODE_INFO* mi) {
    const int seg = mi->segment_id;
    return vp9_get_qindex(&cm->seg, seg, cm->base_qindex);
}

static int block_type(const MODE_INFO* mi) {
    // `skip` only means "no residual coded"; intra blocks set it too, so the
    // intra/inter decision must come first or skipped intra blocks are
    // misreported as Skip (notably in I frames).
    if (!is_inter_block(mi)) {
        return 0;  // Intra
    }
    if (mi->skip) {
        return 2;  // Inter block with no residual
    }
    return 1;  // Inter
}

void vide_vp9_capture_block(VP9_COMMON* cm, const MODE_INFO* mi, int mi_row, int mi_col,
                            int bsize) {
    (void)bsize;
    if (mi == NULL || g_block_count >= VIDE_VP9_MAX_BLOCKS) {
        return;
    }
    if (g_block_count == 0) {
        vide_vp9_capture_frame_reset(cm);  // first block of a new frame
    }
    int w = 0, h = 0;
    block_dims(mi, &w, &h);
    Vp9BlockInfo* b = &g_blocks[g_block_count++];
    memset(b, 0, sizeof(*b));
    b->x = mi_col * 8;
    b->y = mi_row * 8;
    b->w = w;
    b->h = h;
    b->qp = block_qp(cm, mi);
    // A VP9 block has no per-block QP delta: its QP is either the frame base QP
    // (inherited) or an explicit per-segment alt-Q value (coded for this block).
    b->qp_coded = segfeature_active(&cm->seg, mi->segment_id, SEG_LVL_ALT_Q) ? 1 : 0;
    b->block_type = block_type(mi);
    b->ref0 = mi->ref_frame[0];
    b->ref1 = mi->ref_frame[1];
    // mv[] is stored in 1/8-pel by libvpx; convert to quarter-pel (>>1).
    b->mv0x = mi->mv[0].as_mv.col / 8;
    b->mv0y = mi->mv[0].as_mv.row / 8;
    b->mv1x = mi->mv[1].as_mv.col / 8;
    b->mv1y = mi->mv[1].as_mv.row / 8;

    // Record this leaf block as a child of the current partition region.
    if (node_reserve(g_node_count + 1)) {
        Vp9Node* node = &g_nodes[g_node_count];
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

void vide_vp9_capture_frame(VP9_COMMON* cm, const void* yv12_ptr, int is_keyframe) {
    if (cm == NULL || yv12_ptr == NULL) {
        return;
    }
    // Hidden frames (e.g. ALTREF, show_frame == 0) are reference-only and never
    // output; skip them so the pack matches the displayed frame order. Still
    // reset the per-frame accumulators so no state leaks into the next frame.
    if (!cm->show_frame) {
        g_block_count = 0;
        g_node_count = 0;
        g_node_depth = 0;
        g_pr_has = 0;
        return;
    }
    if (g_count >= VIDE_VP9_MAX_QUEUE) {
        return;
    }
    const YV12_BUFFER_CONFIG* yv12 = (const YV12_BUFFER_CONFIG*)yv12_ptr;
    const int width = yv12->y_crop_width;
    const int height = yv12->y_crop_height;
    const int cw = yv12->uv_crop_width;
    const int ch = yv12->uv_crop_height;
    if (width <= 0 || height <= 0) {
        return;
    }

    Vp9FrameCapture* cap = &g_queue[(g_head + g_count) % VIDE_VP9_MAX_QUEUE];
    memset(cap, 0, sizeof(*cap));
    cap->width = width;
    cap->height = height;
    cap->bit_depth = cm->bit_depth;
    cap->is_keyframe = is_keyframe;
    cap->chroma_width = cw;
    cap->chroma_height = ch;

    const int bytes = cm->bit_depth > 8 ? 2 : 1;
    cap->y = (uint8_t*)malloc((size_t)width * height * bytes);
    cap->u = (uint8_t*)malloc((size_t)cw * ch * bytes);
    cap->v = (uint8_t*)malloc((size_t)cw * ch * bytes);
    if (cap->y == NULL || cap->u == NULL || cap->v == NULL) {
        free(cap->y); free(cap->u); free(cap->v);
        memset(cap, 0, sizeof(*cap));
        return;
    }
    for (int r = 0; r < height; ++r) {
        memcpy(cap->y + (size_t)r * width * bytes, yv12->y_buffer + (size_t)r * yv12->y_stride * bytes,
               (size_t)width * bytes);
    }
    for (int r = 0; r < ch; ++r) {
        memcpy(cap->u + (size_t)r * cw * bytes, yv12->u_buffer + (size_t)r * yv12->uv_stride * bytes,
               (size_t)cw * bytes);
        memcpy(cap->v + (size_t)r * cw * bytes, yv12->v_buffer + (size_t)r * yv12->uv_stride * bytes,
               (size_t)cw * bytes);
    }

    const int n = g_block_count < VIDE_VP9_MAX_BLOCKS ? g_block_count : VIDE_VP9_MAX_BLOCKS;
    if (n > 0) {
        cap->blocks = (Vp9BlockInfo*)malloc((size_t)n * sizeof(Vp9BlockInfo));
        if (cap->blocks != NULL) {
            memcpy(cap->blocks, g_blocks, (size_t)n * sizeof(Vp9BlockInfo));
            cap->block_count = n;
        }
    }
    if (g_node_count > 0) {
        cap->nodes = (Vp9Node*)malloc((size_t)g_node_count * sizeof(Vp9Node));
        if (cap->nodes != NULL) {
            memcpy(cap->nodes, g_nodes, (size_t)g_node_count * sizeof(Vp9Node));
            cap->node_count = g_node_count;
        }
    }

    // Prediction / residual: pred = pre-loop-filter reco - residual.
    if (g_pr_has && g_reco_y != NULL && width == g_pr_w && height == g_pr_h) {
        cap->pred_y = (int16_t*)malloc((size_t)width * height * sizeof(int16_t));
        cap->resi_y = (int16_t*)malloc((size_t)width * height * sizeof(int16_t));
        for (size_t i = 0; i < (size_t)width * height; ++i) {
            cap->resi_y[i] = g_resi_y[i];
            cap->pred_y[i] = (int16_t)(g_reco_y[i] - g_resi_y[i]);
        }
        if (cw > 0 && ch > 0 && g_reco_u != NULL) {
            cap->pred_u = (int16_t*)malloc((size_t)cw * ch * sizeof(int16_t));
            cap->pred_v = (int16_t*)malloc((size_t)cw * ch * sizeof(int16_t));
            cap->resi_u = (int16_t*)malloc((size_t)cw * ch * sizeof(int16_t));
            cap->resi_v = (int16_t*)malloc((size_t)cw * ch * sizeof(int16_t));
            for (size_t i = 0; i < (size_t)cw * ch; ++i) {
                cap->resi_u[i] = g_resi_u[i];
                cap->resi_v[i] = g_resi_v[i];
                cap->pred_u[i] = (int16_t)(g_reco_u[i] - g_resi_u[i]);
                cap->pred_v[i] = (int16_t)(g_reco_v[i] - g_resi_v[i]);
            }
        }
        cap->has_pred = 1;
        cap->has_resi = 1;
    }

    g_block_count = 0;  // reset for the next frame
    g_node_count = 0;
    g_node_depth = 0;
    g_pr_has = 0;
    ++g_count;
}

int vide_vp9_pop_capture(Vp9FrameCapture* out) {
    if (g_count <= 0) {
        return 0;
    }
    *out = g_queue[g_head];
    memset(&g_queue[g_head], 0, sizeof(g_queue[g_head]));
    g_head = (g_head + 1) % VIDE_VP9_MAX_QUEUE;
    --g_count;
    return 1;
}
