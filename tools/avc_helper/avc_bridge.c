// C bridge implementation over JM's decoder (see avc_bridge.h).
#include "avc_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "global.h"
#include "mbuffer.h"
#include "h264decoder.h"
#include "VideCapture.h"  // VidE: picture capture hook

int avc_open(const char* path) {
    // Drive the JM decoder purely through its library entry point (constraint
    // C2): hand-fill InputParameters with the same defaults JM's own parameter
    // table applies (configfile.h Map[]) plus the few values its CLI would set.
    // No config file, no ParseCommand(), no command line, no log parsing.
    InputParameters inp;
    memset(&inp, 0, sizeof(inp));
    strncpy(inp.infile, path, FILE_NAME_SIZE - 1);
    inp.infile[FILE_NAME_SIZE - 1] = '\0';
    // Empty output name => OpenDecoder leaves p_out = -1 and write_out_picture
    // returns without writing (output.c), so no YUV dump file is produced.
    inp.outfile[0] = '\0';
    inp.reffile[0] = '\0';
    inp.FileFormat = PAR_OF_ANNEXB;
    inp.write_uv = 1;
    inp.ref_offset = 0;
    inp.poc_scale = 1;  // must stay >= 1: calculate_frame_no() divides by it
    inp.silent = 1;     // suppress the decoder's own stdout progress logs
    inp.conceal_mode = 0;
    inp.ref_poc_gap = 2;
    inp.poc_gap = 2;
    inp.intra_profile_deblocking = 1;
    inp.iDecFrmNum = 0;
    inp.bDisplayDecParams = 0;
    inp.dpb_plus[0] = 1;
    inp.dpb_plus[1] = 0;
#if (MVC_EXTENSION_ENABLE)
    inp.DecodeAllLayers = 0;
#endif

    const int r = OpenDecoder(&inp);
    if (getenv("VIDE_AVC_DBG") != NULL) {
        FILE* dbg = fopen("avc_bridge.log", "a");
        if (dbg != NULL) {
            fprintf(dbg, "avc_open r=%d infile='%s' p_out=%d\n", r, inp.infile,
                    p_Dec ? p_Dec->p_Vid->p_out : -99);
            fflush(dbg);
            fclose(dbg);
        }
    }
    return (r == DEC_OPEN_NOERR) ? 0 : -1;
}

int avc_decode_next(int* out_width, int* out_height, int* out_bit_depth, int* out_poc,
                    uint8_t** out_reco_y, uint8_t** out_reco_u, uint8_t** out_reco_v,
                    int* out_chroma_width, int* out_chroma_height,
                    AvcMbInfo* mb_out, int mb_capacity, int* out_mb_count,
                    AvcSliceInfo* slices_out, int slice_capacity, int* out_slice_count,
                    int16_t** out_pred_y, int16_t** out_pred_u, int16_t** out_pred_v,
                    int16_t** out_resi_y, int16_t** out_resi_u, int16_t** out_resi_v,
                    int* out_has_pred) {
    // Frames are snapshotted by the VidE JM hook (VideCapture, called from
    // exit_picture). Decode until one becomes available or the stream ends.
    for (;;) {
        JmFrameCapture cap;
        if (vide_jm_pop_capture(&cap)) {
            *out_width = cap.width;
            *out_height = cap.height;
            *out_bit_depth = cap.bit_depth;
            *out_poc = cap.poc;
            *out_reco_y = cap.y;
            *out_reco_u = cap.u;
            *out_reco_v = cap.v;
            *out_chroma_width = cap.chroma_width;
            *out_chroma_height = cap.chroma_height;
            if (out_pred_y != NULL) {
                *out_pred_y = cap.pred_y;
                *out_pred_u = cap.pred_u;
                *out_pred_v = cap.pred_v;
                *out_resi_y = cap.resi_y;
                *out_resi_u = cap.resi_u;
                *out_resi_v = cap.resi_v;
                *out_has_pred = cap.has_pred;
            }
            if (out_slice_count != NULL) {
                const int ns = cap.slice_count < slice_capacity ? cap.slice_count : slice_capacity;
                for (int i = 0; i < ns; ++i) {
                    const JmSliceInfo* s = &cap.slices[i];
                    AvcSliceInfo* o = &slices_out[i];
                    memset(o, 0, sizeof(*o));
                    o->slice_type = s->slice_type;
                    o->nal_ref_idc = s->nal_ref_idc;
                    o->idr_flag = s->idr_flag;
                    o->idr_pic_id = s->idr_pic_id;
                    o->pic_parameter_set_id = s->pic_parameter_set_id;
                    o->frame_num = s->frame_num;
                    o->field_pic_flag = s->field_pic_flag;
                    o->bottom_field_flag = s->bottom_field_flag;
                    o->pic_order_cnt_lsb = s->pic_order_cnt_lsb;
                    o->delta_pic_order_cnt_bottom = s->delta_pic_order_cnt_bottom;
                    o->delta_pic_order_cnt0 = s->delta_pic_order_cnt0;
                    o->delta_pic_order_cnt1 = s->delta_pic_order_cnt1;
                    o->redundant_pic_cnt = s->redundant_pic_cnt;
                    o->num_ref_idx_l0_active = s->num_ref_idx_l0_active;
                    o->num_ref_idx_l1_active = s->num_ref_idx_l1_active;
                    o->slice_qp_delta = s->slice_qp_delta;
                    o->slice_qp = s->slice_qp;
    o->ref_l0_count = s->ref_l0_count;
    o->ref_l1_count = s->ref_l1_count;
    o->slice_index = s->slice_index;
                    for (int j = 0; j < s->ref_l0_count && j < 16; ++j) {
                        o->ref_l0_poc[j] = s->ref_l0_poc[j];
                    }
                    for (int j = 0; j < s->ref_l1_count && j < 16; ++j) {
                        o->ref_l1_poc[j] = s->ref_l1_poc[j];
                    }
                }
                *out_slice_count = ns;
            }
            free(cap.slices);
            const int n = cap.mb_count < mb_capacity ? cap.mb_count : mb_capacity;
            for (int i = 0; i < n; ++i) {
                const JmMbInfo* m = &cap.mbs[i];
                AvcMbInfo* info = &mb_out[i];
                memset(info, 0, sizeof(*info));
                info->x = m->x;
                info->y = m->y;
                info->w = m->w;
                info->h = m->h;
                info->qp = m->qp;
                info->block_type = m->block_type;
                info->qp_coded = m->qp_coded;
                info->ref0 = m->ref0;
                info->ref1 = m->ref1;
                info->mv0x = m->mv0x;
                info->mv0y = m->mv0y;
                info->mv1x = m->mv1x;
                info->mv1y = m->mv1y;
                info->bits = m->bits;
    info->bit_offset = m->bit_offset;
    info->bit_length = m->bit_length;
    info->slice_index = m->slice_index;
                for (int k = 0; k < 4; ++k) {
                    info->b8mode[k] = m->b8mode[k];
                    info->b8pdir[k] = m->b8pdir[k];
                }
            }
            *out_mb_count = n;
            free(cap.mbs);  // planes are handed to the caller
            return 0;
        }

        DecodedPicList* pic = NULL;
        const int ret = DecodeOneFrame(&pic);
        if (ret != DEC_SUCCEED) {
            return 1;  // end of stream (frames were captured during decode)
        }
    }
}

void avc_close(void) {
    // Decoding is already finished; JM's flush teardown (FinitDecoder /
    // CloseDecoder) exits the process, so the helper simply drops the decoder.
    // No temporary files are produced any more (see avc_open).
}

int avc_sps_info(int* profile_idc, int* level_idc, int* chroma_format_idc,
                 int* bit_depth_luma_minus8, int* width_mbs_minus1,
                 int* height_map_units_minus1) {
    if (p_Dec == NULL || p_Dec->p_Vid == NULL || p_Dec->p_Vid->active_sps == NULL) {
        return -1;
    }
    const seq_parameter_set_rbsp_t* sps = p_Dec->p_Vid->active_sps;
    *profile_idc = sps->profile_idc;
    *level_idc = sps->level_idc;
    *chroma_format_idc = sps->chroma_format_idc;
    *bit_depth_luma_minus8 = sps->bit_depth_luma_minus8;
    *width_mbs_minus1 = sps->pic_width_in_mbs_minus1;
    *height_map_units_minus1 = sps->pic_height_in_map_units_minus1;
    return 0;
}
