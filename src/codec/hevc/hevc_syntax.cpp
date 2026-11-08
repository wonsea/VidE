#include "codec/hevc/hevc_syntax.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "common/bitreader.h"
#include "common/syntax_builder.h"

namespace vide {
namespace {

std::string dec(uint32_t v) { return std::to_string(v); }
std::string dec(int v) { return std::to_string(v); }

void removeEmulationPrevention(const uint8_t* data, std::size_t size,
                               std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(size);
    int zeros = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const uint8_t byte = data[i];
        if (zeros >= 2 && byte == 0x03) {
            zeros = 0;
            continue;
        }
        out.push_back(byte);
        zeros = (byte == 0) ? zeros + 1 : 0;
    }
}

// Records a parsed syntax element with its exact bit range.
uint32_t elem(SyntaxBuilder& b, uint32_t parent, const char* name, const std::string& value,
              std::size_t start, std::size_t end) {
    return b.addField(parent, name, value, static_cast<int64_t>(start),
                      static_cast<int64_t>(end - start), SyntaxKind::Field);
}

uint32_t readU(BitReader& r, SyntaxBuilder& b, uint32_t parent, const char* name, int bits) {
    const std::size_t start = r.bitPosition();
    const uint32_t v = r.readBits(bits);
    elem(b, parent, name, dec(v), start, r.bitPosition());
    return v;
}

uint32_t readUE(BitReader& r, SyntaxBuilder& b, uint32_t parent, const char* name) {
    const std::size_t start = r.bitPosition();
    const uint32_t v = r.readUE();
    elem(b, parent, name, dec(v), start, r.bitPosition());
    return v;
}

int readSE(BitReader& r, SyntaxBuilder& b, uint32_t parent, const char* name) {
    const std::size_t start = r.bitPosition();
    const int v = r.readSE();
    elem(b, parent, name, dec(v), start, r.bitPosition());
    return v;
}

void profileTierLevel(BitReader& r, SyntaxBuilder& b, uint32_t parent, bool profile_present,
                      uint32_t max_sub_layers_minus1) {
    const uint32_t ptl = b.addStructure(parent, "profile_tier_level");
    if (profile_present) {
        readU(r, b, ptl, "general_profile_space", 2);
        readU(r, b, ptl, "general_tier_flag", 1);
        readU(r, b, ptl, "general_profile_idc", 5);
        const std::size_t compat_start = r.bitPosition();
        for (int j = 0; j < 32; ++j) {
            readU(r, b, ptl, ("general_profile_compatibility_flag[" + dec(j) + "]").c_str(), 1);
        }
        (void)compat_start;
        readU(r, b, ptl, "general_progressive_source_flag", 1);
        readU(r, b, ptl, "general_interlaced_source_flag", 1);
        readU(r, b, ptl, "general_non_packed_constraint_flag", 1);
        readU(r, b, ptl, "general_frame_only_constraint_flag", 1);
        {
            const std::size_t s = r.bitPosition();
            (void)r.readBits(43);
            elem(b, ptl, "general_reserved_zero_43bits", "0", s, r.bitPosition());
        }
        {
            const std::size_t s = r.bitPosition();
            (void)r.readBits(1);
            elem(b, ptl, "general_inbld_flag_or_reserved_zero_bit", "0", s, r.bitPosition());
        }
    }
    readU(r, b, ptl, "general_level_idc", 8);

    std::vector<uint32_t> sub_profile(max_sub_layers_minus1, 0);
    std::vector<uint32_t> sub_level(max_sub_layers_minus1, 0);
    for (uint32_t i = 0; i < max_sub_layers_minus1; ++i) {
        sub_profile[i] = readU(r, b, ptl, ("sub_layer_profile_present_flag[" + dec(i) + "]").c_str(), 1);
        sub_level[i] = readU(r, b, ptl, ("sub_layer_level_present_flag[" + dec(i) + "]").c_str(), 1);
    }
    if (max_sub_layers_minus1 > 0) {
        for (uint32_t i = max_sub_layers_minus1; i < 8; ++i) {
            readU(r, b, ptl, ("reserved_zero_2bits[" + dec(i) + "]").c_str(), 2);
        }
    }
    for (uint32_t i = 0; i < max_sub_layers_minus1; ++i) {
        if (sub_profile[i]) {
            readU(r, b, ptl, ("sub_layer_profile_space[" + dec(i) + "]").c_str(), 2);
            readU(r, b, ptl, ("sub_layer_tier_flag[" + dec(i) + "]").c_str(), 1);
            readU(r, b, ptl, ("sub_layer_profile_idc[" + dec(i) + "]").c_str(), 5);
            for (int j = 0; j < 32; ++j) {
                readU(r, b, ptl,
                      ("sub_layer_profile_compatibility_flag[" + dec(i) + "][" + dec(j) + "]").c_str(), 1);
            }
            readU(r, b, ptl, ("sub_layer_progressive_source_flag[" + dec(i) + "]").c_str(), 1);
            readU(r, b, ptl, ("sub_layer_interlaced_source_flag[" + dec(i) + "]").c_str(), 1);
            readU(r, b, ptl, ("sub_layer_non_packed_constraint_flag[" + dec(i) + "]").c_str(), 1);
            readU(r, b, ptl, ("sub_layer_frame_only_constraint_flag[" + dec(i) + "]").c_str(), 1);
            {
                const std::size_t s = r.bitPosition();
                (void)r.readBits(43);
                elem(b, ptl, ("sub_layer_reserved_zero_43bits[" + dec(i) + "]").c_str(), "0", s,
                     r.bitPosition());
            }
            {
                const std::size_t s = r.bitPosition();
                (void)r.readBits(1);
                elem(b, ptl, ("sub_layer_inbld_flag_or_reserved_zero_bit[" + dec(i) + "]").c_str(),
                     "0", s, r.bitPosition());
            }
        }
        if (sub_level[i]) {
            readU(r, b, ptl, ("sub_layer_level_idc[" + dec(i) + "]").c_str(), 8);
        }
    }
}

void scalingListData(BitReader& r, SyntaxBuilder& b, uint32_t parent) {
    const uint32_t sl = b.addStructure(parent, "scaling_list_data");
    for (int size_id = 0; size_id < 4; ++size_id) {
        const int step = (size_id == 3) ? 3 : 1;
        for (int matrix_id = 0; matrix_id < 6; matrix_id += step) {
            const std::string prefix =
                "[" + dec(size_id) + "][" + dec(matrix_id) + "]";
            const uint32_t pred = readU(
                r, b, sl, ("scaling_list_pred_mode_flag" + prefix).c_str(), 1);
            if (pred == 0) {
                readUE(r, b, sl, ("scaling_list_pred_matrix_id_delta" + prefix).c_str());
            } else {
                const int coef_num = std::min(64, 1 << (4 + (size_id << 1)));
                if (size_id > 1) {
                    readSE(r, b, sl, ("scaling_list_dc_coef_minus8" + prefix).c_str());
                }
                for (int i = 0; i < coef_num; ++i) {
                    readSE(r, b, sl, ("scaling_list_delta_coef" + prefix + "[" + dec(i) + "]").c_str());
                }
            }
            if (!r.good()) {
                return;
            }
        }
    }
}

// Returns the number of delta POCs of the parsed set (for the next set's bound).
int stRefPicSet(BitReader& r, SyntaxBuilder& b, uint32_t parent, uint32_t idx, uint32_t num_sets,
                const std::vector<int>& num_delta_pocs) {
    const uint32_t node = b.addStructure(parent, "st_ref_pic_set(" + dec(idx) + ")");
    uint32_t inter_pred = 0;
    if (idx != 0) {
        inter_pred = readU(r, b, node, "inter_ref_pic_set_prediction_flag", 1);
    } else {
        elem(b, node, "inter_ref_pic_set_prediction_flag", "0", r.bitPosition(), r.bitPosition());
    }
    if (inter_pred != 0) {
        if (idx == num_sets) {
            readUE(r, b, node, "delta_idx_minus1");
        } else {
            elem(b, node, "delta_idx_minus1", "0", r.bitPosition(), r.bitPosition());
        }
        readU(r, b, node, "delta_rps_sign", 1);
        readUE(r, b, node, "abs_delta_rps_minus1");
        // Reference set index for the loop bound.
        int ref = -1;
        if (idx == num_sets && idx >= 1) {
            ref = static_cast<int>(idx) - 1;  // delta_idx_minus1 == 0 (common case)
        } else if (idx >= 1) {
            ref = static_cast<int>(idx) - 1;
        }
        int bound = (ref >= 0 && ref < static_cast<int>(num_delta_pocs.size()))
                        ? num_delta_pocs[ref]
                        : 0;
        int count = 0;
        for (int j = 0; j <= bound; ++j) {
            const uint32_t used = readU(
                r, b, node, ("used_by_curr_pic_flag[" + dec(j) + "]").c_str(), 1);
            uint32_t use_delta = 1;
            if (used == 0) {
                use_delta = readU(r, b, node, ("use_delta_flag[" + dec(j) + "]").c_str(), 1);
            }
            if (used || use_delta) {
                ++count;
            }
            if (!r.good()) {
                break;
            }
        }
        return count;
    }
    const uint32_t num_negative = readUE(r, b, node, "num_negative_pics");
    const uint32_t num_positive = readUE(r, b, node, "num_positive_pics");
    for (uint32_t i = 0; i < num_negative; ++i) {
        readUE(r, b, node, ("delta_poc_s0_minus1[" + dec(i) + "]").c_str());
        readU(r, b, node, ("used_by_curr_pic_s0_flag[" + dec(i) + "]").c_str(), 1);
    }
    for (uint32_t i = 0; i < num_positive; ++i) {
        readUE(r, b, node, ("delta_poc_s1_minus1[" + dec(i) + "]").c_str());
        readU(r, b, node, ("used_by_curr_pic_s1_flag[" + dec(i) + "]").c_str(), 1);
    }
    return static_cast<int>(num_negative + num_positive);
}

uint32_t parseVps(const std::vector<uint8_t>& rbsp, SyntaxTree& tree) {
    BitReader r(rbsp.data(), rbsp.size());
    r.readBits(16);  // nal_unit_header
    SyntaxBuilder b(tree);
    const uint32_t vps = b.addRoot("video_parameter_set_rbsp", SyntaxKind::Structure);

    readU(r, b, vps, "vps_video_parameter_set_id", 4);
    readU(r, b, vps, "vps_base_layer_internal_flag", 1);
    readU(r, b, vps, "vps_base_layer_available_flag", 1);
    readU(r, b, vps, "vps_max_layers_minus1", 6);
    const uint32_t max_sub = readU(r, b, vps, "vps_max_sub_layers_minus1", 3);
    readU(r, b, vps, "vps_temporal_id_nesting_flag", 1);
    readU(r, b, vps, "vps_reserved_0xffff_16bits", 16);
    profileTierLevel(r, b, vps, true, max_sub);
    const uint32_t ordering = readU(r, b, vps, "vps_sub_layer_ordering_info_present_flag", 1);
    for (uint32_t i = (ordering ? 0 : max_sub); i <= max_sub && r.good(); ++i) {
        readUE(r, b, vps, ("vps_max_dec_pic_buffering_minus1[" + dec(i) + "]").c_str());
        readUE(r, b, vps, ("vps_max_num_reorder_pics[" + dec(i) + "]").c_str());
        readUE(r, b, vps, ("vps_max_latency_increase_plus1[" + dec(i) + "]").c_str());
    }
    const uint32_t max_layer_id = readU(r, b, vps, "vps_max_layer_id", 6);
    const uint32_t num_layer_sets = readUE(r, b, vps, "vps_num_layer_sets_minus1");
    for (uint32_t i = 1; i <= num_layer_sets && r.good(); ++i) {
        for (uint32_t j = 0; j <= max_layer_id && r.good(); ++j) {
            readU(r, b, vps,
                  ("layer_id_included_flag[" + dec(i) + "][" + dec(j) + "]").c_str(), 1);
        }
    }
    const uint32_t timing = readU(r, b, vps, "vps_timing_info_present_flag", 1);
    if (timing != 0) {
        readU(r, b, vps, "vps_num_units_in_tick", 32);
        readU(r, b, vps, "vps_time_scale", 32);
        const uint32_t poc_prop = readU(r, b, vps, "vps_poc_proportional_to_timing_flag", 1);
        if (poc_prop) {
            readUE(r, b, vps, "vps_num_ticks_poc_diff_one_minus1");
        }
        const uint32_t num_hrd = readUE(r, b, vps, "vps_num_hrd_parameters");
        for (uint32_t i = 0; i < num_hrd && r.good(); ++i) {
            readUE(r, b, vps, ("hrd_layer_set_idx[" + dec(i) + "]").c_str());
            if (i > 0) {
                readU(r, b, vps, ("cprms_present_flag[" + dec(i) + "]").c_str(), 1);
            }
        }
    }
    readU(r, b, vps, "vps_extension_flag", 1);
    return 1;
}

uint32_t parseSps(const std::vector<uint8_t>& rbsp, SyntaxTree& tree) {
    BitReader r(rbsp.data(), rbsp.size());
    r.readBits(16);
    SyntaxBuilder b(tree);
    const uint32_t sps = b.addRoot("seq_parameter_set_rbsp", SyntaxKind::Structure);

    readU(r, b, sps, "sps_video_parameter_set_id", 4);
    const uint32_t max_sub = readU(r, b, sps, "sps_max_sub_layers_minus1", 3);
    readU(r, b, sps, "sps_temporal_id_nesting_flag", 1);
    profileTierLevel(r, b, sps, true, max_sub);
    readUE(r, b, sps, "sps_seq_parameter_set_id");
    const uint32_t chroma = readUE(r, b, sps, "chroma_format_idc");
    if (chroma == 3) {
        readU(r, b, sps, "separate_colour_plane_flag", 1);
    }
    readUE(r, b, sps, "pic_width_in_luma_samples");
    readUE(r, b, sps, "pic_height_in_luma_samples");
    if (readU(r, b, sps, "conformance_window_flag", 1)) {
        readUE(r, b, sps, "conf_win_left_offset");
        readUE(r, b, sps, "conf_win_right_offset");
        readUE(r, b, sps, "conf_win_top_offset");
        readUE(r, b, sps, "conf_win_bottom_offset");
    }
    readUE(r, b, sps, "bit_depth_luma_minus8");
    readUE(r, b, sps, "bit_depth_chroma_minus8");
    const uint32_t log2_max_poc = readUE(r, b, sps, "log2_max_pic_order_cnt_lsb_minus4") + 4;
    const uint32_t ordering = readU(r, b, sps, "sps_sub_layer_ordering_info_present_flag", 1);
    for (uint32_t i = (ordering ? 0 : max_sub); i <= max_sub && r.good(); ++i) {
        readUE(r, b, sps, ("sps_max_dec_pic_buffering_minus1[" + dec(i) + "]").c_str());
        readUE(r, b, sps, ("sps_max_num_reorder_pics[" + dec(i) + "]").c_str());
        readUE(r, b, sps, ("sps_max_latency_increase_plus1[" + dec(i) + "]").c_str());
    }
    readUE(r, b, sps, "log2_min_luma_coding_block_size_minus3");
    readUE(r, b, sps, "log2_diff_max_min_luma_coding_block_size");
    readUE(r, b, sps, "log2_min_luma_transform_block_size_minus2");
    readUE(r, b, sps, "log2_diff_max_min_luma_transform_block_size");
    readUE(r, b, sps, "max_transform_hierarchy_depth_inter");
    readUE(r, b, sps, "max_transform_hierarchy_depth_intra");
    if (readU(r, b, sps, "scaling_list_enabled_flag", 1)) {
        if (readU(r, b, sps, "sps_scaling_list_data_present_flag", 1)) {
            scalingListData(r, b, sps);
        }
    }
    readU(r, b, sps, "amp_enabled_flag", 1);
    readU(r, b, sps, "sample_adaptive_offset_enabled_flag", 1);
    if (readU(r, b, sps, "pcm_enabled_flag", 1)) {
        readU(r, b, sps, "pcm_sample_bit_depth_luma_minus1", 4);
        readU(r, b, sps, "pcm_sample_bit_depth_chroma_minus1", 4);
        readUE(r, b, sps, "log2_min_pcm_luma_coding_block_size_minus3");
        readUE(r, b, sps, "log2_diff_max_min_pcm_luma_coding_block_size");
        readU(r, b, sps, "pcm_loop_filter_disabled_flag", 1);
    }
    const uint32_t num_st_rps = readUE(r, b, sps, "num_short_term_ref_pic_sets");
    std::vector<int> delta_pocs;
    for (uint32_t i = 0; i < num_st_rps && r.good(); ++i) {
        const int n = stRefPicSet(r, b, sps, i, num_st_rps, delta_pocs);
        delta_pocs.push_back(n);
    }
    if (readU(r, b, sps, "long_term_ref_pics_present_flag", 1)) {
        const uint32_t num_lt = readUE(r, b, sps, "num_long_term_ref_pics_sps");
        for (uint32_t i = 0; i < num_lt && r.good(); ++i) {
            readU(r, b, sps, ("lt_ref_pic_poc_lsb_sps[" + dec(i) + "]").c_str(),
                  static_cast<int>(log2_max_poc));
            readU(r, b, sps, ("used_by_curr_pic_lt_sps_flag[" + dec(i) + "]").c_str(), 1);
        }
    }
    readU(r, b, sps, "sps_temporal_mvp_enabled_flag", 1);
    readU(r, b, sps, "strong_intra_smoothing_enabled_flag", 1);
    const uint32_t vui = readU(r, b, sps, "vui_parameters_present_flag", 1);
    if (vui != 0) {
        // vui_parameters (H.265 7.3.2.1.1). HRD is noted but not expanded.
        const uint32_t node = b.addStructure(sps, "vui_parameters");
        if (readU(r, b, node, "aspect_ratio_info_present_flag", 1)) {
            const uint32_t idc = readU(r, b, node, "aspect_ratio_idc", 8);
            if (idc == 255 /*EXTENDED_SAR*/) {
                readU(r, b, node, "sar_width", 16);
                readU(r, b, node, "sar_height", 16);
            }
        }
        if (readU(r, b, node, "overscan_info_present_flag", 1)) {
            readU(r, b, node, "overscan_appropriate_flag", 1);
        }
        if (readU(r, b, node, "video_signal_type_present_flag", 1)) {
            readU(r, b, node, "video_format", 3);
            readU(r, b, node, "video_full_range_flag", 1);
            if (readU(r, b, node, "colour_description_present_flag", 1)) {
                readU(r, b, node, "colour_primaries", 8);
                readU(r, b, node, "transfer_characteristics", 8);
                readU(r, b, node, "matrix_coeffs", 8);
            }
        }
        if (readU(r, b, node, "chroma_loc_info_present_flag", 1)) {
            readUE(r, b, node, "chroma_sample_loc_type_top_field");
            readUE(r, b, node, "chroma_sample_loc_type_bottom_field");
        }
        readU(r, b, node, "neutral_chroma_indication_flag", 1);
        readU(r, b, node, "field_seq_flag", 1);
        readU(r, b, node, "frame_field_info_present_flag", 1);
        if (readU(r, b, node, "default_display_window_flag", 1)) {
            readUE(r, b, node, "def_disp_win_left_offset");
            readUE(r, b, node, "def_disp_win_right_offset");
            readUE(r, b, node, "def_disp_win_top_offset");
            readUE(r, b, node, "def_disp_win_bottom_offset");
        }
        if (readU(r, b, node, "vui_timing_info_present_flag", 1)) {
            readU(r, b, node, "vui_num_units_in_tick", 32);
            readU(r, b, node, "vui_time_scale", 32);
            if (readU(r, b, node, "vui_poc_proportional_to_timing_flag", 1)) {
                readUE(r, b, node, "vui_num_ticks_poc_diff_one_minus1");
            }
            if (readU(r, b, node, "vui_hrd_parameters_present_flag", 1)) {
                b.addField(node, "note", "HRD parameters not expanded", -1, -1,
                           SyntaxKind::Reserved);
                return 1;
            }
        }
        if (readU(r, b, node, "bitstream_restriction_flag", 1)) {
            readU(r, b, node, "tiles_fixed_structure_flag", 1);
            readU(r, b, node, "motion_vectors_over_pic_boundaries_flag", 1);
            readU(r, b, node, "restricted_ref_pic_lists_flag", 1);
            readUE(r, b, node, "min_spatial_segmentation_idc");
            readUE(r, b, node, "max_bytes_per_pic_denom");
            readUE(r, b, node, "max_bits_per_min_cu_denom");
            readUE(r, b, node, "log2_max_mv_length_horizontal");
            readUE(r, b, node, "log2_max_mv_length_vertical");
        }
        return 1;  // remaining RBSP is SPS extension; stop cleanly
    }
    readU(r, b, sps, "sps_extension_present_flag", 1);
    return 1;
}

uint32_t parsePps(const std::vector<uint8_t>& rbsp, SyntaxTree& tree) {
    BitReader r(rbsp.data(), rbsp.size());
    r.readBits(16);
    SyntaxBuilder b(tree);
    const uint32_t pps = b.addRoot("pic_parameter_set_rbsp", SyntaxKind::Structure);

    readUE(r, b, pps, "pps_pic_parameter_set_id");
    readUE(r, b, pps, "pps_seq_parameter_set_id");
    readU(r, b, pps, "dependent_slice_segments_enabled_flag", 1);
    readU(r, b, pps, "output_flag_present_flag", 1);
    readU(r, b, pps, "num_extra_slice_header_bits", 3);
    readU(r, b, pps, "sign_data_hiding_enabled_flag", 1);
    readU(r, b, pps, "cabac_init_present_flag", 1);
    readUE(r, b, pps, "num_ref_idx_l0_default_active_minus1");
    readUE(r, b, pps, "num_ref_idx_l1_default_active_minus1");
    readSE(r, b, pps, "init_qp_minus26");
    readU(r, b, pps, "constrained_intra_pred_flag", 1);
    readU(r, b, pps, "transform_skip_enabled_flag", 1);
    if (readU(r, b, pps, "cu_qp_delta_enabled_flag", 1)) {
        readUE(r, b, pps, "diff_cu_qp_delta_depth");
    }
    readSE(r, b, pps, "pps_cb_qp_offset");
    readSE(r, b, pps, "pps_cr_qp_offset");
    readU(r, b, pps, "pps_slice_chroma_qp_offsets_present_flag", 1);
    readU(r, b, pps, "weighted_pred_flag", 1);
    readU(r, b, pps, "weighted_bipred_flag", 1);
    readU(r, b, pps, "transquant_bypass_enabled_flag", 1);
    const uint32_t tiles = readU(r, b, pps, "tiles_enabled_flag", 1);
    readU(r, b, pps, "entropy_coding_sync_enabled_flag", 1);
    if (tiles) {
        const uint32_t cols = readUE(r, b, pps, "num_tile_columns_minus1");
        const uint32_t rows = readUE(r, b, pps, "num_tile_rows_minus1");
        const uint32_t uniform = readU(r, b, pps, "uniform_spacing_flag", 1);
        if (!uniform) {
            for (uint32_t i = 0; i < cols && r.good(); ++i) {
                readUE(r, b, pps, ("column_width_minus1[" + dec(i) + "]").c_str());
            }
            for (uint32_t i = 0; i < rows && r.good(); ++i) {
                readUE(r, b, pps, ("row_height_minus1[" + dec(i) + "]").c_str());
            }
        }
        readU(r, b, pps, "loop_filter_across_tiles_enabled_flag", 1);
    }
    readU(r, b, pps, "pps_loop_filter_across_slices_enabled_flag", 1);
    if (readU(r, b, pps, "deblocking_filter_control_present_flag", 1)) {
        readU(r, b, pps, "deblocking_filter_override_enabled_flag", 1);
        if (!readU(r, b, pps, "pps_deblocking_filter_disabled_flag", 1)) {
            readSE(r, b, pps, "pps_beta_offset_div2");
            readSE(r, b, pps, "pps_tc_offset_div2");
        }
    }
    if (readU(r, b, pps, "pps_scaling_list_data_present_flag", 1)) {
        scalingListData(r, b, pps);
    }
    readU(r, b, pps, "lists_modification_present_flag", 1);
    readUE(r, b, pps, "log2_parallel_merge_level_minus2");
    readU(r, b, pps, "slice_segment_header_extension_present_flag", 1);
    readU(r, b, pps, "pps_extension_present_flag", 1);
    return 1;
}

}  // namespace

int parseHevcParameterSets(const uint8_t* data, std::size_t size, SyntaxTree& tree) {
    if (data == nullptr || size < 4) {
        return 0;
    }
    int parsed = 0;
    std::size_t i = 0;
    while (i + 4 < size) {
        // Find a start code (00 00 01).
        if (!(data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)) {
            ++i;
            continue;
        }
        const std::size_t start = i + 3;
        std::size_t j = start;
        while (j + 3 < size && !(data[j] == 0 && data[j + 1] == 0 && data[j + 2] == 1)) {
            ++j;
        }
        if (j <= start) {
            i = j + 1;
            continue;
        }
        const int type = (data[start] >> 1) & 0x3F;
        if (type == 32 || type == 33 || type == 34) {
            std::vector<uint8_t> rbsp;
            removeEmulationPrevention(data + start, j - start, rbsp);
            if (rbsp.size() >= 2) {
                if (type == 32) {
                    parsed += parseVps(rbsp, tree);
                } else if (type == 33) {
                    parsed += parseSps(rbsp, tree);
                } else {
                    parsed += parsePps(rbsp, tree);
                }
            }
        }
        i = j;
    }
    return parsed;
}

}  // namespace vide
