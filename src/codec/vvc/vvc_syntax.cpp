#include "codec/vvc/vvc_syntax.h"

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

int ceilLog2(int v) {
    int r = 0;
    while ((1 << r) < v) {
        ++r;
    }
    return r;
}

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

// constraint_info (H.266 7.3.3.2) — exact VTM field order.
void constraintInfo(BitReader& r, SyntaxBuilder& b, uint32_t parent) {
    const uint32_t gci = b.addStructure(parent, "constraint_info");
    if (readU(r, b, gci, "gci_present_flag", 1) == 0) {
        return;
    }
    readU(r, b, gci, "gci_intra_only_constraint_flag", 1);
    readU(r, b, gci, "gci_all_layers_independent_constraint_flag", 1);
    readU(r, b, gci, "gci_one_au_only_constraint_flag", 1);
    readU(r, b, gci, "gci_sixteen_minus_max_bitdepth_constraint_idc", 4);
    readU(r, b, gci, "gci_three_minus_max_chroma_format_constraint_idc", 2);
    readU(r, b, gci, "gci_no_mixed_nalu_types_in_pic_constraint_flag", 1);
    readU(r, b, gci, "gci_no_trail_constraint_flag", 1);
    readU(r, b, gci, "gci_no_stsa_constraint_flag", 1);
    readU(r, b, gci, "gci_no_rasl_constraint_flag", 1);
    readU(r, b, gci, "gci_no_radl_constraint_flag", 1);
    readU(r, b, gci, "gci_no_idr_constraint_flag", 1);
    readU(r, b, gci, "gci_no_cra_constraint_flag", 1);
    readU(r, b, gci, "gci_no_gdr_constraint_flag", 1);
    readU(r, b, gci, "gci_no_aps_constraint_flag", 1);
    readU(r, b, gci, "gci_no_idr_rpl_constraint_flag", 1);
    readU(r, b, gci, "gci_one_tile_per_pic_constraint_flag", 1);
    readU(r, b, gci, "gci_pic_header_in_slice_header_constraint_flag", 1);
    readU(r, b, gci, "gci_one_slice_per_pic_constraint_flag", 1);
    readU(r, b, gci, "gci_no_rectangular_slice_constraint_flag", 1);
    readU(r, b, gci, "gci_one_slice_per_subpic_constraint_flag", 1);
    readU(r, b, gci, "gci_no_subpic_info_constraint_flag", 1);
    readU(r, b, gci, "gci_three_minus_max_log2_ctu_size_constraint_idc", 2);
    readU(r, b, gci, "gci_no_partition_constraints_override_constraint_flag", 1);
    readU(r, b, gci, "gci_no_mtt_constraint_flag", 1);
    readU(r, b, gci, "gci_no_qtbtt_dual_tree_intra_constraint_flag", 1);
    readU(r, b, gci, "gci_no_palette_constraint_flag", 1);
    readU(r, b, gci, "gci_no_ibc_constraint_flag", 1);
    readU(r, b, gci, "gci_no_isp_constraint_flag", 1);
    readU(r, b, gci, "gci_no_mrl_constraint_flag", 1);
    readU(r, b, gci, "gci_no_mip_constraint_flag", 1);
    readU(r, b, gci, "gci_no_cclm_constraint_flag", 1);
    readU(r, b, gci, "gci_no_ref_pic_resampling_constraint_flag", 1);
    readU(r, b, gci, "gci_no_res_change_in_clvs_constraint_flag", 1);
    readU(r, b, gci, "gci_no_weighted_prediction_constraint_flag", 1);
    readU(r, b, gci, "gci_no_ref_wraparound_constraint_flag", 1);
    readU(r, b, gci, "gci_no_temporal_mvp_constraint_flag", 1);
    readU(r, b, gci, "gci_no_sbtmvp_constraint_flag", 1);
    readU(r, b, gci, "gci_no_amvr_constraint_flag", 1);
    readU(r, b, gci, "gci_no_bdof_constraint_flag", 1);
    readU(r, b, gci, "gci_no_smvd_constraint_flag", 1);
    readU(r, b, gci, "gci_no_dmvr_constraint_flag", 1);
    readU(r, b, gci, "gci_no_mmvd_constraint_flag", 1);
    readU(r, b, gci, "gci_no_affine_motion_constraint_flag", 1);
    readU(r, b, gci, "gci_no_prof_constraint_flag", 1);
    readU(r, b, gci, "gci_no_bcw_constraint_flag", 1);
    readU(r, b, gci, "gci_no_ciip_constraint_flag", 1);
    readU(r, b, gci, "gci_no_gpm_constraint_flag", 1);
    readU(r, b, gci, "gci_no_luma_transform_size_64_constraint_flag", 1);
    readU(r, b, gci, "gci_no_transform_skip_constraint_flag", 1);
    readU(r, b, gci, "gci_no_bdpcm_constraint_flag", 1);
    readU(r, b, gci, "gci_no_mts_constraint_flag", 1);
    readU(r, b, gci, "gci_no_lfnst_constraint_flag", 1);
    readU(r, b, gci, "gci_no_joint_cbcr_constraint_flag", 1);
    readU(r, b, gci, "gci_no_sbt_constraint_flag", 1);
    readU(r, b, gci, "gci_no_act_constraint_flag", 1);
    readU(r, b, gci, "gci_no_explicit_scaling_list_constraint_flag", 1);
    readU(r, b, gci, "gci_no_dep_quant_constraint_flag", 1);
    readU(r, b, gci, "gci_no_sign_data_hiding_constraint_flag", 1);
    readU(r, b, gci, "gci_no_cu_qp_delta_constraint_flag", 1);
    readU(r, b, gci, "gci_no_chroma_qp_offset_constraint_flag", 1);
    readU(r, b, gci, "gci_no_sao_constraint_flag", 1);
    readU(r, b, gci, "gci_no_alf_constraint_flag", 1);
    readU(r, b, gci, "gci_no_ccalf_constraint_flag", 1);
    readU(r, b, gci, "gci_no_lmcs_constraint_flag", 1);
    readU(r, b, gci, "gci_no_ladf_constraint_flag", 1);
    readU(r, b, gci, "gci_no_virtual_boundaries_constraint_flag", 1);
}

// VVC profile_tier_level (H.266 7.3.3.1), matching VTM's HLSyntaxReader.
void profileTierLevel(BitReader& r, SyntaxBuilder& b, uint32_t parent, bool present,
                      uint32_t max_sub_minus1) {
    const uint32_t ptl = b.addStructure(parent, "profile_tier_level");
    if (present) {
        readU(r, b, ptl, "general_profile_idc", 7);
        readU(r, b, ptl, "general_tier_flag", 1);
    }
    readU(r, b, ptl, "general_level_idc", 8);
    readU(r, b, ptl, "ptl_frame_only_constraint_flag", 1);
    readU(r, b, ptl, "ptl_multilayer_enabled_flag", 1);
    if (present) {
        constraintInfo(r, b, ptl);
    }
    std::vector<uint32_t> sub_level(max_sub_minus1, 0);
    for (int i = static_cast<int>(max_sub_minus1) - 1; i >= 0; --i) {
        sub_level[i] = readU(r, b, ptl, ("sub_layer_level_present_flag[" + dec(i) + "]").c_str(), 1);
    }
    int guard = 0;
    while ((r.bitPosition() % 8) != 0 && guard++ < 8) {
        readU(r, b, ptl, "ptl_reserved_zero_bit", 1);
    }
    for (int i = static_cast<int>(max_sub_minus1) - 1; i >= 0; --i) {
        if (sub_level[i]) {
            readU(r, b, ptl, ("sub_layer_level_idc[" + dec(i) + "]").c_str(), 8);
        }
    }
    if (present) {
        const uint32_t num_sub = readU(r, b, ptl, "ptl_num_sub_profiles", 8);
        for (uint32_t i = 0; i < num_sub; ++i) {
            readU(r, b, ptl, ("general_sub_profile_idc[" + dec(i) + "]").c_str(), 32);
        }
    }
}

// dpb_parameters() common path.
void dpbParameters(BitReader& r, SyntaxBuilder& b, uint32_t parent, uint32_t max_sub_minus1,
                   bool sub_layer_present) {
    const uint32_t dpb = b.addStructure(parent, "dpb_parameters");
    for (uint32_t i = (sub_layer_present ? 0 : max_sub_minus1); i <= max_sub_minus1; ++i) {
        readUE(r, b, dpb, ("dpb_max_dec_pic_buffering_minus1[" + dec(i) + "]").c_str());
        readUE(r, b, dpb, ("dpb_max_num_reorder_pics[" + dec(i) + "]").c_str());
        readUE(r, b, dpb, ("dpb_max_latency_increase_plus1[" + dec(i) + "]").c_str());
    }
}

uint32_t parseSps(const std::vector<uint8_t>& rbsp, SyntaxTree& tree) {
    BitReader r(rbsp.data(), rbsp.size());
    r.readBits(16);  // NAL unit header
    SyntaxBuilder b(tree);
    const uint32_t sps = b.addRoot("seq_parameter_set_rbsp", SyntaxKind::Structure);

    readU(r, b, sps, "sps_seq_parameter_set_id", 4);
    readU(r, b, sps, "sps_video_parameter_set_id", 4);
    const uint32_t max_sub = readU(r, b, sps, "sps_max_sublayers_minus1", 3);
    const uint32_t chroma = readU(r, b, sps, "sps_chroma_format_idc", 2);
    const uint32_t log2_ctu = readU(r, b, sps, "sps_log2_ctu_size_minus5", 2) + 5;
    const uint32_t ptl_present = readU(r, b, sps, "sps_ptl_dpb_hrd_params_present_flag", 1);
    if (ptl_present) {
        profileTierLevel(r, b, sps, true, max_sub);
    }
    readU(r, b, sps, "sps_gdr_enabled_flag", 1);
    const uint32_t ref_resampling = readU(r, b, sps, "sps_ref_pic_resampling_enabled_flag", 1);
    if (ref_resampling) {
        readU(r, b, sps, "sps_res_change_in_clvs_allowed_flag", 1);
    }
    const uint32_t width = readUE(r, b, sps, "sps_pic_width_max_in_luma_samples");
    const uint32_t height = readUE(r, b, sps, "sps_pic_height_max_in_luma_samples");
    if (readU(r, b, sps, "sps_conformance_window_flag", 1)) {
        readUE(r, b, sps, "sps_conf_win_left_offset");
        readUE(r, b, sps, "sps_conf_win_right_offset");
        readUE(r, b, sps, "sps_conf_win_top_offset");
        readUE(r, b, sps, "sps_conf_win_bottom_offset");
    }
    const uint32_t subpic_info = readU(r, b, sps, "sps_subpic_info_present_flag", 1);
    if (subpic_info) {
        const uint32_t num_subpics = readUE(r, b, sps, "sps_num_subpics_minus1");
        if (num_subpics > 0) {
            readU(r, b, sps, "sps_independent_subpics_flag", 1);
            const uint32_t same_size = readU(r, b, sps, "sps_subpic_same_size_flag", 1);
            const int ctu_size = 1 << log2_ctu;
            const int w_ctu = (static_cast<int>(width) + ctu_size - 1) / ctu_size;
            const int h_ctu = (static_cast<int>(height) + ctu_size - 1) / ctu_size;
            const int x_bits = ceilLog2(std::max(1, w_ctu));
            const int y_bits = ceilLog2(std::max(1, h_ctu));
            for (uint32_t i = 0; i < num_subpics; ++i) {
                if (!same_size || i == 0) {
                    readU(r, b, sps, ("sps_subpic_ctu_top_left_x[" + dec(i) + "]").c_str(), x_bits);
                    readU(r, b, sps, ("sps_subpic_ctu_top_left_y[" + dec(i) + "]").c_str(), y_bits);
                    if (!same_size) {
                        readU(r, b, sps, ("sps_subpic_width_minus1[" + dec(i) + "]").c_str(), x_bits);
                        readU(r, b, sps, ("sps_subpic_height_minus1[" + dec(i) + "]").c_str(), y_bits);
                    }
                }
            }
            if (!subpic_info) {
                // unreachable
            }
        }
        // subpic id signalling: stop (rare) to avoid desync.
        b.addField(sps, "note", "subpicture id signalling not expanded", -1, -1,
                   SyntaxKind::Reserved);
        (void)width;
        (void)height;
        return 1;
    }

    const uint32_t bitdepth = readUE(r, b, sps, "sps_bitdepth_minus8");
    (void)bitdepth;
    readU(r, b, sps, "sps_entropy_coding_sync_enabled_flag", 1);
    readU(r, b, sps, "sps_entry_point_offsets_present_flag", 1);
    readU(r, b, sps, "sps_log2_max_pic_order_cnt_lsb_minus4", 4);
    const uint32_t poc_msb = readU(r, b, sps, "sps_poc_msb_cycle_flag", 1);
    if (poc_msb) {
        readUE(r, b, sps, "sps_poc_msb_cycle_len_minus1");
    }
    const uint32_t ph_bytes = readU(r, b, sps, "sps_num_extra_ph_bytes", 2);
    for (uint32_t i = 0; i < ph_bytes * 8; ++i) {
        readU(r, b, sps, ("sps_extra_ph_bit_present_flag[" + dec(i) + "]").c_str(), 1);
    }
    const uint32_t sh_bytes = readU(r, b, sps, "sps_num_extra_sh_bytes", 2);
    for (uint32_t i = 0; i < sh_bytes * 8; ++i) {
        readU(r, b, sps, ("sps_extra_sh_bit_present_flag[" + dec(i) + "]").c_str(), 1);
    }
    if (ptl_present) {
        bool sub_dpb = false;
        if (max_sub > 0) {
            sub_dpb = readU(r, b, sps, "sps_sublayer_dpb_params_flag", 1) != 0;
        }
        dpbParameters(r, b, sps, max_sub, sub_dpb);
    }
    readUE(r, b, sps, "sps_log2_min_luma_coding_block_size_minus2");
    readU(r, b, sps, "sps_partition_constraints_override_enabled_flag", 1);
    readUE(r, b, sps, "sps_log2_diff_min_qt_min_cb_intra_slice_luma");
    const uint32_t mtt_intra_luma = readUE(r, b, sps, "sps_max_mtt_hierarchy_depth_intra_slice_luma");
    if (mtt_intra_luma > 0) {
        readUE(r, b, sps, "sps_log2_diff_max_bt_min_qt_intra_slice_luma");
        readUE(r, b, sps, "sps_log2_diff_max_tt_min_qt_intra_slice_luma");
    }
    if (chroma != 0) {
        readU(r, b, sps, "sps_qtbtt_dual_tree_intra_flag", 1);
        readUE(r, b, sps, "sps_log2_diff_min_qt_min_cb_intra_slice_chroma");
        const uint32_t mtt_intra_chroma =
            readUE(r, b, sps, "sps_max_mtt_hierarchy_depth_intra_slice_chroma");
        if (mtt_intra_chroma > 0) {
            readUE(r, b, sps, "sps_log2_diff_max_bt_min_qt_intra_slice_chroma");
            readUE(r, b, sps, "sps_log2_diff_max_tt_min_qt_intra_slice_chroma");
        }
    }
    readUE(r, b, sps, "sps_log2_diff_min_qt_min_cb_inter_slice");
    const uint32_t mtt_inter = readUE(r, b, sps, "sps_max_mtt_hierarchy_depth_inter_slice");
    if (mtt_inter > 0) {
        readUE(r, b, sps, "sps_log2_diff_max_bt_min_qt_inter_slice");
        readUE(r, b, sps, "sps_log2_diff_max_tt_min_qt_inter_slice");
    }
    readU(r, b, sps, "sps_max_luma_transform_size_64_flag", 1);
    const uint32_t ts = readU(r, b, sps, "sps_transform_skip_enabled_flag", 1);
    if (ts) {
        readUE(r, b, sps, "sps_log2_transform_skip_max_size_minus2");
        readU(r, b, sps, "sps_bdpcm_enabled_flag", 1);
    }
    if (readU(r, b, sps, "sps_mts_enabled_flag", 1)) {
        readU(r, b, sps, "sps_explicit_mts_intra_enabled_flag", 1);
        readU(r, b, sps, "sps_explicit_mts_inter_enabled_flag", 1);
    }
    readU(r, b, sps, "sps_lfnst_enabled_flag", 1);
    if (chroma != 0) {
        const uint32_t joint = readU(r, b, sps, "sps_joint_cbcr_enabled_flag", 1);
        const uint32_t same_qp = readU(r, b, sps, "sps_same_qp_table_for_chroma_flag", 1);
        const uint32_t num_qp_tables = same_qp ? 1 : (joint ? 3 : 2);
        for (uint32_t i = 0; i < num_qp_tables; ++i) {
            readSE(r, b, sps, ("sps_qp_table_start_minus26[" + dec(i) + "]").c_str());
            const uint32_t num_points = readUE(r, b, sps,
                                               ("sps_num_points_in_qp_table_minus1[" + dec(i) + "]").c_str());
            for (uint32_t j = 0; j < num_points + 1; ++j) {
                readUE(r, b, sps, ("sps_delta_qp_in_val_minus1[" + dec(i) + "][" + dec(j) + "]").c_str());
                readUE(r, b, sps, ("sps_delta_qp_diff_val[" + dec(i) + "][" + dec(j) + "]").c_str());
            }
        }
    }
    readU(r, b, sps, "sps_sao_enabled_flag", 1);
    const uint32_t alf = readU(r, b, sps, "sps_alf_enabled_flag", 1);
    if (alf && chroma == 3) {
        readU(r, b, sps, "sps_ccalf_enabled_flag", 1);
    }
    readU(r, b, sps, "sps_lmcs_enabled_flag", 1);
    readU(r, b, sps, "sps_weighted_pred_flag", 1);
    readU(r, b, sps, "sps_weighted_bipred_flag", 1);
    const uint32_t long_term = readU(r, b, sps, "sps_long_term_ref_pics_flag", 1);
    if (poc_msb || long_term) {
        readU(r, b, sps, "sps_inter_layer_prediction_enabled_flag", 1);
    }
    readU(r, b, sps, "sps_idr_rpl_present_flag", 1);
    const uint32_t rpl1_same = readU(r, b, sps, "sps_rpl1_same_as_rpl0_flag", 1);
    const uint32_t num_rpl_lists = rpl1_same ? 1 : 2;
    for (uint32_t i = 0; i < num_rpl_lists; ++i) {
        const uint32_t num_rpls = readUE(r, b, sps, ("sps_num_ref_pic_lists[" + dec(i) + "]").c_str());
        if (num_rpls > 0) {
            b.addField(sps, "ref_pic_list_struct", "present (not expanded)", -1, -1,
                       SyntaxKind::Reserved);
            return 1;
        }
    }
    readU(r, b, sps, "sps_ref_wraparound_enabled_flag", 1);
    b.addField(sps, "note", "SPS parsed to reference-picture lists; later fields not expanded",
               -1, -1, SyntaxKind::Reserved);
    return 1;
}

uint32_t parsePps(const std::vector<uint8_t>& rbsp, SyntaxTree& tree) {
    BitReader r(rbsp.data(), rbsp.size());
    r.readBits(16);
    SyntaxBuilder b(tree);
    const uint32_t pps = b.addRoot("pic_parameter_set_rbsp", SyntaxKind::Structure);

    readU(r, b, pps, "pps_pic_parameter_set_id", 6);
    readU(r, b, pps, "pps_seq_parameter_set_id", 4);
    readU(r, b, pps, "pps_mixed_nalu_types_in_pic_flag", 1);
    readUE(r, b, pps, "pps_pic_width_in_luma_samples");
    readUE(r, b, pps, "pps_pic_height_in_luma_samples");
    if (readU(r, b, pps, "pps_conformance_window_flag", 1)) {
        readUE(r, b, pps, "pps_conf_win_left_offset");
        readUE(r, b, pps, "pps_conf_win_right_offset");
        readUE(r, b, pps, "pps_conf_win_top_offset");
        readUE(r, b, pps, "pps_conf_win_bottom_offset");
    }
    if (readU(r, b, pps, "pps_scaling_window_explicit_signalling_flag", 1)) {
        readSE(r, b, pps, "pps_scaling_win_left_offset");
        readSE(r, b, pps, "pps_scaling_win_right_offset");
        readSE(r, b, pps, "pps_scaling_win_top_offset");
        readSE(r, b, pps, "pps_scaling_win_bottom_offset");
    }
    readU(r, b, pps, "pps_output_flag_present_flag", 1);
    const uint32_t no_part = readU(r, b, pps, "pps_no_pic_partition_flag", 1);
    const uint32_t subpic_map = readU(r, b, pps, "pps_subpic_id_mapping_present_flag", 1);
    if (subpic_map) {
        uint32_t num_subpics = 1;
        if (!no_part) {
            num_subpics = readUE(r, b, pps, "pps_num_subpics_minus1") + 1;
        }
        const uint32_t id_len = readUE(r, b, pps, "pps_subpic_id_len_minus1") + 1;
        for (uint32_t i = 0; i < num_subpics; ++i) {
            readU(r, b, pps, ("pps_subpic_id[" + dec(i) + "]").c_str(), static_cast<int>(id_len));
        }
    }
    if (!no_part) {
        b.addField(pps, "note", "PPS tile/slice partitioning not expanded", -1, -1,
                   SyntaxKind::Reserved);
        return 1;
    }
    readU(r, b, pps, "pps_cabac_init_present_flag", 1);
    readUE(r, b, pps, "pps_num_ref_idx_default_active_minus1[0]");
    readUE(r, b, pps, "pps_num_ref_idx_default_active_minus1[1]");
    readU(r, b, pps, "pps_rpl1_idx_present_flag", 1);
    readU(r, b, pps, "pps_weighted_pred_flag", 1);
    readU(r, b, pps, "pps_weighted_bipred_flag", 1);
    if (readU(r, b, pps, "pps_ref_wraparound_enabled_flag", 1)) {
        readUE(r, b, pps, "pps_ref_wraparound_offset");
    }
    readSE(r, b, pps, "pps_init_qp_minus26");
    readU(r, b, pps, "pps_cu_qp_delta_enabled_flag", 1);
    const uint32_t chroma_tool =
        readU(r, b, pps, "pps_chroma_tool_offsets_present_flag", 1);
    if (chroma_tool) {
        readSE(r, b, pps, "pps_cb_qp_offset");
        readSE(r, b, pps, "pps_cr_qp_offset");
        const uint32_t jcc = readU(r, b, pps, "pps_joint_cbcr_qp_offset_present_flag", 1);
        if (jcc) {
            readSE(r, b, pps, "pps_joint_cbcr_qp_offset_value");
        }
        readU(r, b, pps, "pps_slice_chroma_qp_offsets_present_flag", 1);
        if (readU(r, b, pps, "pps_cu_chroma_qp_offset_list_enabled_flag", 1)) {
            const uint32_t len = readUE(r, b, pps, "pps_chroma_qp_offset_list_len_minus1");
            for (uint32_t i = 0; i <= len; ++i) {
                readSE(r, b, pps, ("pps_cb_qp_offset_list[" + dec(i) + "]").c_str());
                readSE(r, b, pps, ("pps_cr_qp_offset_list[" + dec(i) + "]").c_str());
                if (jcc) {
                    readSE(r, b, pps,
                           ("pps_joint_cbcr_qp_offset_list[" + dec(i) + "]").c_str());
                }
            }
        }
    }

    if (readU(r, b, pps, "pps_deblocking_filter_control_present_flag", 1)) {
        const uint32_t db_override =
            readU(r, b, pps, "pps_deblocking_filter_override_enabled_flag", 1);
        const uint32_t db_disabled =
            readU(r, b, pps, "pps_deblocking_filter_disabled_flag", 1);
        if (!no_part && db_override) {
            readU(r, b, pps, "pps_dbf_info_in_ph_flag", 1);
        }
        if (!db_disabled) {
            readSE(r, b, pps, "pps_beta_offset_div2");
            readSE(r, b, pps, "pps_tc_offset_div2");
            if (chroma_tool) {
                readSE(r, b, pps, "pps_cb_beta_offset_div2");
                readSE(r, b, pps, "pps_cb_tc_offset_div2");
                readSE(r, b, pps, "pps_cr_beta_offset_div2");
                readSE(r, b, pps, "pps_cr_tc_offset_div2");
            }
        }
    }

    b.addField(pps, "note", "PPS parsed to deblocking filter; later fields not expanded", -1, -1,
               SyntaxKind::Reserved);
    return 1;
}

uint32_t parseVps(const std::vector<uint8_t>& rbsp, SyntaxTree& tree) {
    BitReader r(rbsp.data(), rbsp.size());
    r.readBits(16);
    SyntaxBuilder b(tree);
    const uint32_t vps = b.addRoot("video_parameter_set_rbsp", SyntaxKind::Structure);
    readU(r, b, vps, "vps_video_parameter_set_id", 4);
    const uint32_t max_layers = readU(r, b, vps, "vps_max_layers_minus1", 6);
    const uint32_t max_sub = readU(r, b, vps, "vps_max_sublayers_minus1", 3);
    readU(r, b, vps, "vps_default_ptl_dpb_hrd_max_tid_flag", 1);
    readU(r, b, vps, "vps_all_independent_layers_flag", 1);
    for (uint32_t i = 0; i <= max_layers; ++i) {
        readU(r, b, vps, ("vps_layer_id[" + dec(i) + "]").c_str(), 6);
    }
    (void)max_sub;
    b.addField(vps, "note", "VPS parsed to layer ids", -1, -1, SyntaxKind::Reserved);
    return 1;
}

}  // namespace

int parseVvcParameterSets(const uint8_t* data, std::size_t size, SyntaxTree& tree) {
    if (data == nullptr || size < 4) {
        return 0;
    }
    int parsed = 0;
    std::size_t i = 0;
    while (i + 4 < size) {
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
        const int type = (data[start + 1] >> 3) & 0x1F;
        if (type == 14 || type == 15 || type == 16) {
            std::vector<uint8_t> rbsp;
            removeEmulationPrevention(data + start, j - start, rbsp);
            if (rbsp.size() >= 2) {
                if (type == 14) {
                    parsed += parseVps(rbsp, tree);
                } else if (type == 15) {
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
