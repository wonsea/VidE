// C bridge implementation over libaom's AV1 decoder (see av1_bridge.h).
#include "av1_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aom/aom_decoder.h"
#include "aom/aom_image.h"
#include "aom/aomdx.h"

// VidE capture hook compiled into libaom.
#include "av1/decoder/av1_capture.h"

static aom_codec_ctx_t g_codec;
static FILE* g_file = NULL;
static int g_opened = 0;
static int g_ivf = 0;

static Av1PlanesC g_planes;  // prediction/residual of the last decoded frame

static void free_planes(Av1PlanesC* p) {
    free(p->pred_y);
    free(p->pred_u);
    free(p->pred_v);
    free(p->resi_y);
    free(p->resi_u);
    free(p->resi_v);
    memset(p, 0, sizeof(*p));
}

void av1_take_planes(Av1PlanesC* out) {
    if (out == NULL) {
        return;
    }
    *out = g_planes;
    memset(&g_planes, 0, sizeof(g_planes));
}

int av1_open(const char* path) {
    g_file = fopen(path, "rb");
    if (g_file == NULL) {
        return -1;
    }
    char magic[4] = {0};
    if (fread(magic, 1, 4, g_file) == 4 && memcmp(magic, "DKIF", 4) == 0) {
        unsigned char header[28];
        if (fread(header, 1, 28, g_file) != 28) {
            fclose(g_file);
            g_file = NULL;
            return -1;
        }
        g_ivf = 1;
    } else {
        rewind(g_file);  // raw OBU stream
        g_ivf = 0;
    }

    aom_codec_dec_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.threads = 1;
    if (aom_codec_dec_init(&g_codec, aom_codec_av1_dx(), &cfg, 0) != AOM_CODEC_OK) {
        fclose(g_file);
        g_file = NULL;
        return -1;
    }
    g_opened = 1;
    return 0;
}

// Copies libaom's decoded aom_image_t into tightly packed 8/16-bit planes.
static int copy_image(aom_image_t* img, int* out_width, int* out_height, int* out_bit_depth,
                      uint8_t** out_y, uint8_t** out_u, uint8_t** out_v, int* out_cw, int* out_ch) {
    if (img == NULL) {
        return -1;
    }
    const int width = img->d_w;
    const int height = img->d_h;
    const int cw = (width + 1) / 2;
    const int ch = (height + 1) / 2;
    // libaom may allocate 16-bit frames even for 8-bit streams; the authoritative
    // signal is the AOM_IMG_FMT_HIGHBITDEPTH flag (img->bit_depth can read 8).
    const int bytes = (img->fmt & AOM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1;
    uint8_t* y = (uint8_t*)malloc((size_t)width * height * bytes);
    uint8_t* u = (uint8_t*)malloc((size_t)cw * ch * bytes);
    uint8_t* v = (uint8_t*)malloc((size_t)cw * ch * bytes);
    if (y == NULL || u == NULL || v == NULL) {
        free(y); free(u); free(v);
        return -1;
    }
    // aom_image uses separate planes for I420; use the plane pointers/strides.
    for (int r = 0; r < height; ++r) {
        memcpy(y + (size_t)r * width * bytes,
               img->planes[AOM_PLANE_Y] + (size_t)r * img->stride[AOM_PLANE_Y], (size_t)width * bytes);
    }
    const int cstride = img->stride[AOM_PLANE_U];
    for (int r = 0; r < ch; ++r) {
        memcpy(u + (size_t)r * cw * bytes,
               img->planes[AOM_PLANE_U] + (size_t)r * cstride, (size_t)cw * bytes);
        memcpy(v + (size_t)r * cw * bytes,
               img->planes[AOM_PLANE_V] + (size_t)r * cstride, (size_t)cw * bytes);
    }
    *out_width = width;
    *out_height = height;
    *out_bit_depth = img->bit_depth;
    *out_y = y;
    *out_u = u;
    *out_v = v;
    *out_cw = cw;
    *out_ch = ch;
    return 0;
}

int av1_decode_next(int* out_width, int* out_height, int* out_bit_depth, int* out_keyframe,
                    uint8_t** out_y, uint8_t** out_u, uint8_t** out_v, int* out_cw, int* out_ch,
                    Av1BlockInfoC* blocks, int block_capacity, int* out_block_count,
                    Av1NodeC* nodes, int node_capacity, int* out_node_count) {
    if (!g_opened) {
        return -1;
    }
    free_planes(&g_planes);  // drop any planes the caller did not take
    // Prefer an already-produced frame (reco via public API + block info via hook).
    aom_codec_iter_t iter = NULL;
    aom_image_t* img = aom_codec_get_frame(&g_codec, &iter);
    if (img != NULL) {
        Av1FrameCapture cap;
        memset(&cap, 0, sizeof(cap));
        cap.bit_depth = img->bit_depth;
        if (vide_av1_pop_capture(&cap)) {
            // block info from the hook queue
        }
        if (copy_image(img, out_width, out_height, out_bit_depth, out_y, out_u, out_v, out_cw,
                       out_ch) != 0) {
            return -1;
        }
        *out_keyframe = cap.is_keyframe;
        const int n = cap.block_count < block_capacity ? cap.block_count : block_capacity;
        for (int i = 0; i < n; ++i) {
            Av1BlockInfoC* o = &blocks[i];
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
            Av1NodeC* o = &nodes[i];
            o->parent = cap.nodes[i].parent;
            o->x = cap.nodes[i].x;
            o->y = cap.nodes[i].y;
            o->w = cap.nodes[i].w;
            o->h = cap.nodes[i].h;
            o->depth = cap.nodes[i].depth;
            o->block_index = cap.nodes[i].block_index;
        }
        g_planes.width = cap.width;
        g_planes.height = cap.height;
        g_planes.cw = cap.chroma_width;
        g_planes.ch = cap.chroma_height;
        g_planes.pred_y = cap.pred_y;
        g_planes.pred_u = cap.pred_u;
        g_planes.pred_v = cap.pred_v;
        g_planes.resi_y = cap.resi_y;
        g_planes.resi_u = cap.resi_u;
        g_planes.resi_v = cap.resi_v;
        g_planes.has_pred = cap.has_pred;
        g_planes.has_resi = cap.has_resi;
        *out_node_count = nn;
        free(cap.nodes);
        free(cap.blocks);
        return 0;
    }

    // Read the next frame: IVF frame header (12 bytes) or one whole raw OBU frame.
    unsigned char* buf = NULL;
    size_t frame_size = 0;
    if (g_ivf) {
        unsigned char fh[12];
        if (fread(fh, 1, 12, g_file) != 12) {
            return 1;
        }
        frame_size = (uint32_t)fh[0] | ((uint32_t)fh[1] << 8) | ((uint32_t)fh[2] << 16) |
                     ((uint32_t)fh[3] << 24);
        if (frame_size == 0 || frame_size > (64u << 20)) {
            return 1;
        }
        buf = (unsigned char*)malloc(frame_size);
        if (buf == NULL) {
            return -2;
        }
        if (fread(buf, 1, frame_size, g_file) != frame_size) {
            free(buf);
            return 1;
        }
    } else {
        // Raw OBU: read the remaining file in one go on the first call.
        fseek(g_file, 0, SEEK_END);
        long total = ftell(g_file);
        fseek(g_file, 0, SEEK_SET);
        if (total <= 0) {
            return 1;
        }
        buf = (unsigned char*)malloc((size_t)total);
        if (buf == NULL) {
            return -2;
        }
        frame_size = fread(buf, 1, (size_t)total, g_file);
        g_ivf = -1;  // sentinel: consumed
    }

    {
        FILE* d = getenv("VIDE_AV1_DBG") ? fopen("av1_capture.log", "a") : NULL;
        if (d) { fprintf(d, "decode size=%zu ivf=%d\n", frame_size, g_ivf); fflush(d); fclose(d); }
    }
    const aom_codec_err_t err = aom_codec_decode(&g_codec, buf, frame_size, NULL, 0);
    free(buf);
    {
        FILE* d = getenv("VIDE_AV1_DBG") ? fopen("av1_capture.log", "a") : NULL;
        if (d) {
            fprintf(d, "decode ret=%d (%s) detail=%s\n", (int)err, aom_codec_err_to_string(err),
                    aom_codec_error_detail(&g_codec) ? aom_codec_error_detail(&g_codec) : "(none)");
            fflush(d); fclose(d);
        }
    }
    if (err != AOM_CODEC_OK) {
        return -2;
    }

    // Fetch the freshly decoded frame from libaom's public output and pair it
    // with the block info the hook recorded for this frame.
    aom_codec_iter_t iter2 = NULL;
    aom_image_t* img2 = aom_codec_get_frame(&g_codec, &iter2);
    if (img2 != NULL) {
        Av1FrameCapture cap;
        memset(&cap, 0, sizeof(cap));
        vide_av1_pop_capture(&cap);
        if (copy_image(img2, out_width, out_height, out_bit_depth, out_y, out_u, out_v, out_cw,
                       out_ch) != 0) {
            return -1;
        }
        *out_keyframe = cap.is_keyframe;
        const int n = cap.block_count < block_capacity ? cap.block_count : block_capacity;
        for (int i = 0; i < n; ++i) {
            Av1BlockInfoC* o = &blocks[i];
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
            Av1NodeC* o = &nodes[i];
            o->parent = cap.nodes[i].parent;
            o->x = cap.nodes[i].x;
            o->y = cap.nodes[i].y;
            o->w = cap.nodes[i].w;
            o->h = cap.nodes[i].h;
            o->depth = cap.nodes[i].depth;
            o->block_index = cap.nodes[i].block_index;
        }
        g_planes.width = cap.width;
        g_planes.height = cap.height;
        g_planes.cw = cap.chroma_width;
        g_planes.ch = cap.chroma_height;
        g_planes.pred_y = cap.pred_y;
        g_planes.pred_u = cap.pred_u;
        g_planes.pred_v = cap.pred_v;
        g_planes.resi_y = cap.resi_y;
        g_planes.resi_u = cap.resi_u;
        g_planes.resi_v = cap.resi_v;
        g_planes.has_pred = cap.has_pred;
        g_planes.has_resi = cap.has_resi;
        *out_node_count = nn;
        free(cap.nodes);
        free(cap.blocks);
        return 0;
    }
    if (g_ivf == -1) {
        return 1;  // raw OBU fully consumed
    }
    return 2;  // decoded but no frame yielded yet (retry)
}

void av1_close(void) {
    if (g_opened) {
        aom_codec_destroy(&g_codec);
        g_opened = 0;
    }
    if (g_file != NULL) {
        fclose(g_file);
        g_file = NULL;
    }
}
