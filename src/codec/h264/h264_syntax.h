// Lightweight H.264 (AVC) parameter-set syntax parser.
//
// Produces a SyntaxTree for SPS/PPS NAL units with bit offsets. Full slice /
// macroblock syntax comes from the JM adapter (M5); this covers the sequence
// and picture headers that are available from container extradata.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/syntax.h"
#include "common/syntax_builder.h"

namespace vide {

struct H264ParameterSets {
    std::vector<std::vector<uint8_t>> sps;
    std::vector<std::vector<uint8_t>> pps;
    // First coded slice NAL (type 1 or 5), when present in the scanned stream.
    std::vector<uint8_t> first_slice;
    int first_slice_nal_type = 0;

    bool empty() const { return sps.empty() && pps.empty(); }
};

// Minimal SPS/PPS context needed to parse a slice header (H.264 7.3.3).
struct H264SpsContext {
    bool valid = false;
    int profile_idc = 0;
    int chroma_format_idc = 1;
    int log2_max_frame_num = 0;
    int pic_order_cnt_type = 0;
    int log2_max_pic_order_cnt_lsb = 0;
    bool delta_pic_order_always_zero = false;
    int frame_mbs_only_flag = 1;
    bool separate_colour_plane_flag = false;
};

struct H264PpsContext {
    bool valid = false;
    bool bottom_field_pic_order_in_frame_present_flag = false;
    bool redundant_pic_cnt_present_flag = false;
    bool entropy_coding_mode_flag = false;
};

// Extracts SPS/PPS from AVCC extradata, falling back to Annex-B scanning.
H264ParameterSets extractH264ParameterSets(const uint8_t* data, std::size_t size);

// Scans an Annex-B byte stream for SPS (type 7), PPS (type 8) and the first
// coded slice (type 1/5).
H264ParameterSets scanH264AnnexB(const uint8_t* data, std::size_t size);

// Parses one NAL unit (header byte included) and appends nodes under `parent`.
void parseH264Sps(const uint8_t* nal, std::size_t size, SyntaxBuilder& builder, uint32_t parent,
                  H264SpsContext* ctx = nullptr);
void parseH264Pps(const uint8_t* nal, std::size_t size, SyntaxBuilder& builder, uint32_t parent,
                  H264PpsContext* ctx = nullptr);

// Parses a slice header (H.264 7.3.3) up to the POC / redundant-pic-cnt fields.
void parseH264SliceHeader(const uint8_t* nal, std::size_t size, const H264SpsContext& sps,
                          const H264PpsContext& pps, SyntaxBuilder& builder, uint32_t parent);

}  // namespace vide
