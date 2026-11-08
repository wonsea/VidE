// VidE capture hook implementation (see VideCapture.h).
#include "VideCapture.h"

#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "mbuffer.h"

#define VIDE_JM_MAX_QUEUE 8
#define VIDE_JM_MAX_MBS (1 << 18)  // 262144 macroblocks (>= 8K frame)

unsigned int vide_jm_bins = 0;                       // CABAC bins (see VideCapture.h)
static unsigned int g_mb_bins[VIDE_JM_MAX_MBS];      // per-MB bin deltas
static unsigned char g_mb_qp_coded[VIDE_JM_MAX_MBS]; // per-MB mb_qp_delta presence
static unsigned int g_mb_bit_off[VIDE_JM_MAX_MBS];   // per-MB NAL-RBSP bit offset
static unsigned int g_mb_bit_len[VIDE_JM_MAX_MBS];   // per-MB bit length
static int g_mb_slice[VIDE_JM_MAX_MBS];              // per-MB global VCL slice ordinal
static unsigned int g_cabac_base = 0;                // NAL-RBSP bit of CABAC data start
static int g_global_slice = 0;                       // running VCL slice ordinal
static int g_current_slice = 0;                      // ordinal of the slice being decoded

void vide_jm_capture_mb_bits(int mb_index, unsigned int bins) {
    if (mb_index >= 0 && mb_index < VIDE_JM_MAX_MBS) {
        g_mb_bins[mb_index] = bins;
    }
}

void vide_jm_set_cabac_base(int byte_start_pos) {
    // streamBuffer skips the 1-byte NAL header, so NAL-RBSP byte = byte_start_pos + 1.
    g_cabac_base = (unsigned int)(byte_start_pos + 1) * 8u;
}

unsigned int vide_jm_cabac_base(void) {
    return g_cabac_base;
}

void vide_jm_capture_mb_bit_range(int mb_index, unsigned int bit_offset, unsigned int bit_length) {
    if (mb_index >= 0 && mb_index < VIDE_JM_MAX_MBS) {
        g_mb_bit_off[mb_index] = bit_offset;
        g_mb_bit_len[mb_index] = bit_length;
    }
}

int vide_jm_current_slice_index(void) {
    return g_current_slice;
}

void vide_jm_capture_mb_slice(int mb_index, int slice_index) {
    if (mb_index >= 0 && mb_index < VIDE_JM_MAX_MBS) {
        g_mb_slice[mb_index] = slice_index;
    }
}

void vide_jm_capture_mb_qp_coded(int mb_index, int coded) {
    if (mb_index >= 0 && mb_index < VIDE_JM_MAX_MBS) {
        g_mb_qp_coded[mb_index] = coded ? 1 : 0;
    }
}

int vide_jm_mb_qp_coded(int mb_index) {
    if (mb_index >= 0 && mb_index < VIDE_JM_MAX_MBS) {
        return g_mb_qp_coded[mb_index];
    }
    return 0;
}

static JmFrameCapture g_queue[VIDE_JM_MAX_QUEUE];
static int g_head = 0;
static int g_count = 0;

// All slice headers of the current picture. The first slice also fills the
// legacy scalar fields (attached to the next capture).
static JmFrameCapture g_slice;  // only the slices[] scalar mirror is used
static JmSliceInfo* g_slices = NULL;
static int g_slices_count = 0;
static int g_slices_cap = 0;
static int g_last_slice_nr = -1;

static void fill_one_slice(JmSliceInfo* s, const struct slice* slice) {
    s->slice_type = slice->slice_type;
    s->nal_ref_idc = slice->nal_reference_idc;
    s->idr_flag = slice->idr_flag;
    s->idr_pic_id = slice->idr_pic_id;
    s->pic_parameter_set_id = slice->pic_parameter_set_id;
    s->frame_num = slice->frame_num;
    s->field_pic_flag = (int)slice->field_pic_flag;
    s->bottom_field_flag = slice->bottom_field_flag;
    s->pic_order_cnt_lsb = slice->pic_order_cnt_lsb;
    s->delta_pic_order_cnt_bottom = slice->delta_pic_order_cnt_bottom;
    s->delta_pic_order_cnt0 = slice->delta_pic_order_cnt[0];
    s->delta_pic_order_cnt1 = slice->delta_pic_order_cnt[1];
    s->redundant_pic_cnt = slice->redundant_pic_cnt;
    s->num_ref_idx_l0_active = slice->num_ref_idx_active[LIST_0];
    s->num_ref_idx_l1_active = slice->num_ref_idx_active[LIST_1];
    s->slice_qp_delta = slice->slice_qp_delta;
    s->slice_qp = slice->qp;
    s->ref_l0_count = 0;
    s->ref_l1_count = 0;
    if (slice->listX != NULL) {
        const int n0 = (int)slice->listXsize[LIST_0];
        const int n1 = (int)slice->listXsize[LIST_1];
        for (int i = 0; i < n0 && i < 16; ++i) {
            const struct storable_picture* rp = slice->listX[LIST_0][i];
            s->ref_l0_poc[i] = rp != NULL ? rp->frame_poc : -9999;
            s->ref_l0_count = i + 1;
        }
        for (int i = 0; i < n1 && i < 16; ++i) {
            const struct storable_picture* rp = slice->listX[LIST_1][i];
            s->ref_l1_poc[i] = rp != NULL ? rp->frame_poc : -9999;
            s->ref_l1_count = i + 1;
        }
    }
}

void vide_jm_capture_slice_info(const struct slice* slice) {
    if (slice == NULL) {
        return;
    }
    // A new slice starts whenever its index within the picture changes.
    if ((int)slice->current_slice_nr == g_last_slice_nr) {
        return;
    }
    g_last_slice_nr = (int)slice->current_slice_nr;
    g_current_slice = g_global_slice++;  // VidE: global VCL slice ordinal
    if (g_slices_count == g_slices_cap) {
        const int cap = g_slices_cap > 0 ? g_slices_cap * 2 : 8;
        JmSliceInfo* grown = (JmSliceInfo*)realloc(g_slices, (size_t)cap * sizeof(JmSliceInfo));
        if (grown == NULL) {
            return;
        }
        g_slices = grown;
        g_slices_cap = cap;
    }
    fill_one_slice(&g_slices[g_slices_count], slice);
    g_slices[g_slices_count].slice_index = g_current_slice;  // VidE
    if (g_slices_count == 0) {
        // Mirror the first slice into the legacy scalar fields.
        g_slice.slice_type = g_slices[0].slice_type;
        g_slice.nal_ref_idc = g_slices[0].nal_ref_idc;
        g_slice.idr_flag = g_slices[0].idr_flag;
        g_slice.idr_pic_id = g_slices[0].idr_pic_id;
        g_slice.pic_parameter_set_id = g_slices[0].pic_parameter_set_id;
        g_slice.frame_num = g_slices[0].frame_num;
        g_slice.field_pic_flag = g_slices[0].field_pic_flag;
        g_slice.bottom_field_flag = g_slices[0].bottom_field_flag;
        g_slice.pic_order_cnt_lsb = g_slices[0].pic_order_cnt_lsb;
        g_slice.delta_pic_order_cnt_bottom = g_slices[0].delta_pic_order_cnt_bottom;
        g_slice.delta_pic_order_cnt0 = g_slices[0].delta_pic_order_cnt0;
        g_slice.delta_pic_order_cnt1 = g_slices[0].delta_pic_order_cnt1;
        g_slice.redundant_pic_cnt = g_slices[0].redundant_pic_cnt;
        g_slice.num_ref_idx_l0_active = g_slices[0].num_ref_idx_l0_active;
        g_slice.num_ref_idx_l1_active = g_slices[0].num_ref_idx_l1_active;
        g_slice.slice_qp_delta = g_slices[0].slice_qp_delta;
        g_slice.slice_qp = g_slices[0].slice_qp;
    }
    ++g_slices_count;
}

// Per-frame prediction/residual accumulators (coded size; cropped at capture).
static int16_t* g_pred_y = NULL;
static int16_t* g_pred_u = NULL;
static int16_t* g_pred_v = NULL;
static int16_t* g_resi_y = NULL;
static int16_t* g_resi_u = NULL;
static int16_t* g_resi_v = NULL;
static int g_pr_w = 0, g_pr_h = 0, g_pr_cw = 0, g_pr_ch = 0;
static int g_pr_has = 0;

static void pr_free(void) {
    free(g_pred_y); free(g_pred_u); free(g_pred_v);
    free(g_resi_y); free(g_resi_u); free(g_resi_v);
    g_pred_y = g_pred_u = g_pred_v = NULL;
    g_resi_y = g_resi_u = g_resi_v = NULL;
    g_pr_w = g_pr_h = g_pr_cw = g_pr_ch = 0;
}

static void pr_alloc(int w, int h, int cw, int ch) {
    if (g_pred_y != NULL && w == g_pr_w && h == g_pr_h && cw == g_pr_cw && ch == g_pr_ch) {
        return;  // reuse (caller zeroes)
    }
    pr_free();
    g_pred_y = (int16_t*)calloc((size_t)w * h, sizeof(int16_t));
    g_resi_y = (int16_t*)calloc((size_t)w * h, sizeof(int16_t));
    if (cw > 0 && ch > 0) {
        g_pred_u = (int16_t*)calloc((size_t)cw * ch, sizeof(int16_t));
        g_pred_v = (int16_t*)calloc((size_t)cw * ch, sizeof(int16_t));
        g_resi_u = (int16_t*)calloc((size_t)cw * ch, sizeof(int16_t));
        g_resi_v = (int16_t*)calloc((size_t)cw * ch, sizeof(int16_t));
    }
    g_pr_w = w; g_pr_h = h; g_pr_cw = cw; g_pr_ch = ch;
}

void vide_jm_capture_mb_pred_resi(const struct video_par* p_Vid, const struct slice* slice,
                                  const struct macroblock_dec* mb) {
    if (p_Vid == NULL || slice == NULL || mb == NULL || slice->mb_pred == NULL) {
        return;
    }
    const int w = p_Vid->width;
    const int h = p_Vid->height;
    const int cw = p_Vid->width_cr;
    const int ch = p_Vid->height_cr;
    if (w <= 0 || h <= 0) {
        return;
    }
    if (mb->mbAddrX == 0) {
        pr_alloc(w, h, cw, ch);
        if (g_pred_y != NULL) {
            memset(g_pred_y, 0, (size_t)w * h * sizeof(int16_t));
            memset(g_resi_y, 0, (size_t)w * h * sizeof(int16_t));
        }
        if (g_pred_u != NULL) {
            memset(g_pred_u, 0, (size_t)cw * ch * sizeof(int16_t));
            memset(g_pred_v, 0, (size_t)cw * ch * sizeof(int16_t));
            memset(g_resi_u, 0, (size_t)cw * ch * sizeof(int16_t));
            memset(g_resi_v, 0, (size_t)cw * ch * sizeof(int16_t));
        }
        g_pr_has = 0;
    }
    if (g_pred_y == NULL) {
        return;
    }
    const int bx = mb->pix_x;
    const int by = mb->pix_y;
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            const int x = bx + c, y = by + r;
            if (x >= 0 && x < w && y >= 0 && y < h) {
                g_pred_y[(size_t)y * w + x] = (int16_t)slice->mb_pred[0][r][c];
                g_resi_y[(size_t)y * w + x] = (int16_t)slice->mb_rres[0][r][c];
            }
        }
    }
    if (g_pred_u != NULL && slice->mb_pred[1] != NULL && slice->mb_pred[2] != NULL) {
        const int cbx = bx / 2;
        const int cby = by / 2;
        const int mcw = p_Vid->mb_cr_size_x;
        const int mch = p_Vid->mb_cr_size_y;
        for (int r = 0; r < mch; ++r) {
            for (int c = 0; c < mcw; ++c) {
                const int x = cbx + c, y = cby + r;
                if (x >= 0 && x < cw && y >= 0 && y < ch) {
                    g_pred_u[(size_t)y * cw + x] = (int16_t)slice->mb_pred[1][r][c];
                    g_pred_v[(size_t)y * cw + x] = (int16_t)slice->mb_pred[2][r][c];
                    g_resi_u[(size_t)y * cw + x] = (int16_t)slice->mb_rres[1][r][c];
                    g_resi_v[(size_t)y * cw + x] = (int16_t)slice->mb_rres[2][r][c];
                }
            }
        }
    }
    g_pr_has = 1;
}

static void copy_slice_fields(JmFrameCapture* cap) {
    cap->slice_type = g_slice.slice_type;
    cap->nal_ref_idc = g_slice.nal_ref_idc;
    cap->idr_flag = g_slice.idr_flag;
    cap->idr_pic_id = g_slice.idr_pic_id;
    cap->pic_parameter_set_id = g_slice.pic_parameter_set_id;
    cap->frame_num = g_slice.frame_num;
    cap->field_pic_flag = g_slice.field_pic_flag;
    cap->bottom_field_flag = g_slice.bottom_field_flag;
    cap->pic_order_cnt_lsb = g_slice.pic_order_cnt_lsb;
    cap->delta_pic_order_cnt_bottom = g_slice.delta_pic_order_cnt_bottom;
    cap->delta_pic_order_cnt0 = g_slice.delta_pic_order_cnt0;
    cap->delta_pic_order_cnt1 = g_slice.delta_pic_order_cnt1;
    cap->redundant_pic_cnt = g_slice.redundant_pic_cnt;
    cap->num_ref_idx_l0_active = g_slice.num_ref_idx_l0_active;
    cap->num_ref_idx_l1_active = g_slice.num_ref_idx_l1_active;
    cap->slice_qp_delta = g_slice.slice_qp_delta;
    cap->slice_qp = g_slice.slice_qp;

    // All slice headers of this picture.
    if (g_slices_count > 0) {
        cap->slices = (JmSliceInfo*)malloc((size_t)g_slices_count * sizeof(JmSliceInfo));
        if (cap->slices != NULL) {
            memcpy(cap->slices, g_slices, (size_t)g_slices_count * sizeof(JmSliceInfo));
            cap->slice_count = g_slices_count;
        }
    }
    g_slices_count = 0;
    g_last_slice_nr = -1;

    memset(&g_slice, 0, sizeof(g_slice));  // ready for the next picture
}

static int map_block_type(short mb_type) {
    if (mb_type < 0) {
        return 2;  // skip
    }
    if (is_intra_mb(mb_type)) {
        return 0;  // intra
    }
    return 1;      // inter
}

void vide_jm_capture_picture(VideoParameters* p_Vid, StorablePicture* pic) {
    if (p_Vid == NULL || pic == NULL || pic->imgY == NULL) {
        return;
    }
    if (g_count >= VIDE_JM_MAX_QUEUE) {
        return;  // consumer is behind; drop
    }

    const int coded_w = pic->size_x;
    const int coded_h = pic->size_y;
    if (coded_w <= 0 || coded_h <= 0) {
        return;
    }

    // Apply the cropping window (H.264 7.4.2.1.1) so the exported picture is the
    // display (cropped) image, matching the SPS the main process parsed. JM
    // keeps the coded (MB-aligned) planes with a stride; we copy the crop window.
    int crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;
    int sub_w = 1;
    int sub_h = 1;
    const int chroma_format_idc = pic->chroma_format_idc;
    const int frame2x = 2 - pic->frame_mbs_only_flag;
    if (chroma_format_idc == 1) {          // 4:2:0
        sub_w = 2;
        sub_h = 2 * frame2x;
    } else if (chroma_format_idc == 2) {   // 4:2:2
        sub_w = 2;
        sub_h = 1 * frame2x;
    } else if (chroma_format_idc == 3) {   // 4:4:4
        sub_w = 1;
        sub_h = 1 * frame2x;
    } else {                                // monochrome (no chroma crop used)
        sub_w = 1;
        sub_h = frame2x;
    }
    if (pic->frame_cropping_flag) {
        crop_left = sub_w * pic->frame_crop_left_offset;
        crop_right = sub_w * pic->frame_crop_right_offset;
        crop_top = sub_h * pic->frame_crop_top_offset;
        crop_bottom = sub_h * pic->frame_crop_bottom_offset;
    }
    const int out_w = coded_w - crop_left - crop_right;
    const int out_h = coded_h - crop_top - crop_bottom;

    const int cw = pic->size_x_cr;
    const int ch = pic->size_y_cr;
    // Chroma crop is expressed directly in chroma samples (the H.264 crop unit
    // for chroma equals sub_w/sub_h, so dividing the luma crop by it is exact).
    const int c_crop_left = sub_w > 0 ? crop_left / sub_w : crop_left;
    const int c_crop_right = sub_w > 0 ? crop_right / sub_w : crop_right;
    const int c_crop_top = sub_h > 0 ? crop_top / sub_h : crop_top;
    const int c_crop_bottom = sub_h > 0 ? crop_bottom / sub_h : crop_bottom;
    const int out_cw = cw - c_crop_left - c_crop_right;
    const int out_ch = ch - c_crop_top - c_crop_bottom;
    if (out_w <= 0 || out_h <= 0 || out_cw <= 0 || out_ch <= 0) {
        return;
    }

    JmFrameCapture* cap = &g_queue[(g_head + g_count) % VIDE_JM_MAX_QUEUE];
    memset(cap, 0, sizeof(*cap));
    cap->width = out_w;
    cap->height = out_h;
    cap->chroma_width = out_cw;
    cap->chroma_height = out_ch;
    cap->bit_depth = p_Vid->active_sps != NULL ? p_Vid->active_sps->bit_depth_luma_minus8 + 8 : 8;
    cap->poc = pic->frame_poc;
    cap->is_keyframe = pic->idr_flag != 0;
    copy_slice_fields(cap);  // first-slice header recorded this picture

    // JM stores samples as `imgpel` (uint16 for IMGTYPE==1 builds), so copy
    // sample-by-sample and normalise to 8 bit for VidE's 8-bit reco path.
    const int shift = cap->bit_depth > 8 ? (cap->bit_depth - 8) : 0;

    cap->y = (uint8_t*)malloc((size_t)out_w * out_h);
    cap->u = (uint8_t*)malloc((size_t)out_cw * out_ch);
    cap->v = (uint8_t*)malloc((size_t)out_cw * out_ch);
    if (cap->y == NULL || cap->u == NULL || cap->v == NULL) {
        free(cap->y); free(cap->u); free(cap->v);
        memset(cap, 0, sizeof(*cap));
        return;
    }
    for (int r = 0; r < out_h; ++r) {
        const imgpel* src = pic->imgY[r + crop_top] + crop_left;
        uint8_t* dst = cap->y + (size_t)r * out_w;
        for (int c = 0; c < out_w; ++c) {
            dst[c] = (uint8_t)(src[c] >> shift);
        }
    }
    for (int r = 0; r < out_ch; ++r) {
        const imgpel* su = pic->imgUV[0][r + c_crop_top] + c_crop_left;
        const imgpel* sv = pic->imgUV[1][r + c_crop_top] + c_crop_left;
        uint8_t* du = cap->u + (size_t)r * out_cw;
        uint8_t* dv = cap->v + (size_t)r * out_cw;
        for (int c = 0; c < out_cw; ++c) {
            du[c] = (uint8_t)(su[c] >> shift);
            dv[c] = (uint8_t)(sv[c] >> shift);
        }
    }

    const int mb_w = coded_w / 16;
    const int mb_h = coded_h / 16;
    const int mb_total = mb_w * mb_h;
    if (mb_total > 0) {
        cap->mbs = (JmMbInfo*)calloc((size_t)mb_total, sizeof(JmMbInfo));
        if (cap->mbs != NULL) {
            for (int i = 0; i < mb_total; ++i) {
                const Macroblock* mb = &p_Vid->mb_data[i];
                JmMbInfo* info = &cap->mbs[i];
                info->x = (i % mb_w) * 16;
                info->y = (i / mb_w) * 16;
                info->w = 16;
                info->h = 16;
                info->qp = mb->qp;
                info->block_type = map_block_type(mb->mb_type);
                info->qp_coded = vide_jm_mb_qp_coded(i);  // VidE: explicit mb_qp_delta?
                info->bits = g_mb_bins[i];  // VidE: CABAC bins for this MB
    info->bit_offset = g_mb_bit_off[i];  // VidE: NAL-RBSP bit range
    info->bit_length = g_mb_bit_len[i];
    info->slice_index = g_mb_slice[i];   // VidE: global VCL slice ordinal
                for (int k = 0; k < 4; ++k) {
                    info->b8mode[k] = mb->b8mode[k];
                    info->b8pdir[k] = mb->b8pdir[k];
                }
                info->ref0 = 0;
                info->ref1 = -1;
                info->mv0x = mb->mvd[LIST_0][0][0][0];
                info->mv0y = mb->mvd[LIST_0][0][0][1];
                info->mv1x = mb->mvd[LIST_1][0][0][0];
                info->mv1y = mb->mvd[LIST_1][0][0][1];
            }
            cap->mb_count = mb_total;
        }
    }

    // Attach the accumulated prediction/residual, cropped to display size.
    if (g_pr_has && g_pred_y != NULL) {
        const int lw = out_w, lh = out_h;
        cap->pred_y = (int16_t*)malloc((size_t)lw * lh * sizeof(int16_t));
        cap->resi_y = (int16_t*)malloc((size_t)lw * lh * sizeof(int16_t));
        for (int r = 0; r < lh; ++r) {
            for (int c = 0; c < lw; ++c) {
                const int sy = r + crop_top, sx = c + crop_left;
                const size_t di = (size_t)r * lw + c;
                if (sy < g_pr_h && sx < g_pr_w) {
                    cap->pred_y[di] = g_pred_y[(size_t)sy * g_pr_w + sx];
                    cap->resi_y[di] = g_resi_y[(size_t)sy * g_pr_w + sx];
                } else {
                    cap->pred_y[di] = 0;
                    cap->resi_y[di] = 0;
                }
            }
        }
        if (g_pred_u != NULL && out_cw > 0 && out_ch > 0) {
            cap->pred_u = (int16_t*)malloc((size_t)out_cw * out_ch * sizeof(int16_t));
            cap->pred_v = (int16_t*)malloc((size_t)out_cw * out_ch * sizeof(int16_t));
            cap->resi_u = (int16_t*)malloc((size_t)out_cw * out_ch * sizeof(int16_t));
            cap->resi_v = (int16_t*)malloc((size_t)out_cw * out_ch * sizeof(int16_t));
            for (int r = 0; r < out_ch; ++r) {
                for (int c = 0; c < out_cw; ++c) {
                    const int sy = r + c_crop_top, sx = c + c_crop_left;
                    const size_t di = (size_t)r * out_cw + c;
                    if (sy < g_pr_ch && sx < g_pr_cw) {
                        cap->pred_u[di] = g_pred_u[(size_t)sy * g_pr_cw + sx];
                        cap->pred_v[di] = g_pred_v[(size_t)sy * g_pr_cw + sx];
                        cap->resi_u[di] = g_resi_u[(size_t)sy * g_pr_cw + sx];
                        cap->resi_v[di] = g_resi_v[(size_t)sy * g_pr_cw + sx];
                    } else {
                        cap->pred_u[di] = 0; cap->pred_v[di] = 0;
                        cap->resi_u[di] = 0; cap->resi_v[di] = 0;
                    }
                }
            }
        }
        cap->has_pred = 1;
        cap->has_resi = 1;
    }

    // Reset the per-MB bin deltas / QP flags for the next picture.
    if (mb_total > 0 && mb_total <= VIDE_JM_MAX_MBS) {
        memset(g_mb_bins, 0, (size_t)mb_total * sizeof(unsigned int));
        memset(g_mb_qp_coded, 0, (size_t)mb_total * sizeof(unsigned char));
        memset(g_mb_bit_off, 0, (size_t)mb_total * sizeof(unsigned int));
        memset(g_mb_bit_len, 0, (size_t)mb_total * sizeof(unsigned int));
        memset(g_mb_slice, 0, (size_t)mb_total * sizeof(int));
    }

    ++g_count;
}

int vide_jm_pop_capture(JmFrameCapture* out) {
    if (g_count <= 0) {
        return 0;
    }
    *out = g_queue[g_head];
    memset(&g_queue[g_head], 0, sizeof(g_queue[g_head]));
    g_head = (g_head + 1) % VIDE_JM_MAX_QUEUE;
    --g_count;
    return 1;
}
