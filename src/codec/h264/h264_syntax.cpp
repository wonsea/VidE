#include "codec/h264/h264_syntax.h"

#include <cstdio>
#include <string>

#include "common/bitreader.h"

namespace vide {
namespace {

std::string num(uint32_t value) { return std::to_string(value); }

std::string hex(uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%02X", value);
    return buffer;
}

void removeEmulationPrevention(const uint8_t* data, std::size_t size,
                               std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(size);
    int zeros = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const uint8_t byte = data[i];
        if (zeros >= 2 && byte == 0x03) {
            zeros = 0;
            continue;  // emulation_prevention_three_byte
        }
        out.push_back(byte);
        zeros = (byte == 0) ? zeros + 1 : 0;
    }
}

void skipScalingList(BitReader& reader, int size) {
    int last_scale = 8;
    int next_scale = 8;
    for (int j = 0; j < size && reader.good(); ++j) {
        if (next_scale != 0) {
            const int delta = reader.readSE();
            next_scale = (last_scale + delta + 256) % 256;
        }
        last_scale = (next_scale == 0) ? last_scale : next_scale;
    }
}

uint32_t elemU(BitReader& r, SyntaxBuilder& b, uint32_t parent, const char* name, int bits) {
    const std::size_t start = r.bitPosition();
    const uint32_t v = r.readBits(bits);
    b.addField(parent, name, num(v), static_cast<int64_t>(start),
               static_cast<int64_t>(r.bitPosition() - start));
    return v;
}

uint32_t elemUE(BitReader& r, SyntaxBuilder& b, uint32_t parent, const char* name) {
    const std::size_t start = r.bitPosition();
    const uint32_t v = r.readUE();
    b.addField(parent, name, num(v), static_cast<int64_t>(start),
               static_cast<int64_t>(r.bitPosition() - start));
    return v;
}

int elemSE(BitReader& r, SyntaxBuilder& b, uint32_t parent, const char* name) {
    const std::size_t start = r.bitPosition();
    const int v = r.readSE();
    b.addField(parent, name, std::to_string(v), static_cast<int64_t>(start),
               static_cast<int64_t>(r.bitPosition() - start));
    return v;
}

// vui_parameters (H.264 7.3.2.1.1). HRD parameters are noted but not expanded.
void parseH264Vui(BitReader& r, SyntaxBuilder& b, uint32_t parent) {
    const uint32_t vui = b.addStructure(parent, "vui_parameters");

    if (elemU(r, b, vui, "aspect_ratio_info_present_flag", 1)) {
        const uint32_t idc = elemU(r, b, vui, "aspect_ratio_idc", 8);
        if (idc == 255 /*Extended_SAR*/) {
            elemU(r, b, vui, "sar_width", 16);
            elemU(r, b, vui, "sar_height", 16);
        }
    }
    if (elemU(r, b, vui, "overscan_info_present_flag", 1)) {
        elemU(r, b, vui, "overscan_appropriate_flag", 1);
    }
    if (elemU(r, b, vui, "video_signal_type_present_flag", 1)) {
        elemU(r, b, vui, "video_format", 3);
        elemU(r, b, vui, "video_full_range_flag", 1);
        if (elemU(r, b, vui, "colour_description_present_flag", 1)) {
            elemU(r, b, vui, "colour_primaries", 8);
            elemU(r, b, vui, "transfer_characteristics", 8);
            elemU(r, b, vui, "matrix_coefficients", 8);
        }
    }
    if (elemU(r, b, vui, "chroma_loc_info_present_flag", 1)) {
        elemUE(r, b, vui, "chroma_sample_loc_type_top_field");
        elemUE(r, b, vui, "chroma_sample_loc_type_bottom_field");
    }
    if (elemU(r, b, vui, "timing_info_present_flag", 1)) {
        elemU(r, b, vui, "num_units_in_tick", 32);
        elemU(r, b, vui, "time_scale", 32);
        elemU(r, b, vui, "fixed_frame_rate_flag", 1);
    }
    const uint32_t nal_hrd = elemU(r, b, vui, "nal_hrd_parameters_present_flag", 1);
    const uint32_t vcl_hrd = elemU(r, b, vui, "vcl_hrd_parameters_present_flag", 1);
    if (nal_hrd || vcl_hrd) {
        b.addField(vui, "note", "HRD parameters not expanded", -1, -1, SyntaxKind::Reserved);
        return;
    }
    elemU(r, b, vui, "pic_struct_present_flag", 1);
    if (elemU(r, b, vui, "bitstream_restriction_flag", 1)) {
        elemU(r, b, vui, "motion_vectors_over_pic_boundaries_flag", 1);
        elemUE(r, b, vui, "max_bytes_per_pic_denom");
        elemUE(r, b, vui, "max_bits_per_mb_denom");
        elemUE(r, b, vui, "log2_max_mv_length_horizontal");
        elemUE(r, b, vui, "log2_max_mv_length_vertical");
        elemUE(r, b, vui, "max_num_reorder_frames");
        elemUE(r, b, vui, "max_dec_frame_buffering");
    }
}

struct SpsInfo {
    int width = 0;
    int height = 0;
    int chroma_format_idc = 1;
    bool valid = false;
};

}  // namespace

H264ParameterSets scanH264AnnexB(const uint8_t* data, std::size_t size) {
    H264ParameterSets sets;
    std::size_t i = 0;
    while (i + 3 < size) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            const std::size_t start = i + 3;
            std::size_t j = start;
            while (j + 2 < size &&
                   !(data[j] == 0 && data[j + 1] == 0 && data[j + 2] == 1)) {
                ++j;
            }
            const int type = data[start] & 0x1F;
            if (type == 7) {
                sets.sps.emplace_back(data + start, data + j);
            } else if (type == 8) {
                sets.pps.emplace_back(data + start, data + j);
            } else if ((type == 1 || type == 5) && sets.first_slice.empty()) {
                sets.first_slice.assign(data + start, data + j);
                sets.first_slice_nal_type = type;
            }
            i = j;
        } else {
            ++i;
        }
    }
    return sets;
}

H264ParameterSets extractH264ParameterSets(const uint8_t* data, std::size_t size) {
    H264ParameterSets sets;
    if (data == nullptr || size < 7) {
        return sets;
    }

    if (data[0] == 1) {  // AVCDecoderConfigurationRecord
        std::size_t pos = 5;
        const int num_sps = data[pos++] & 0x1F;
        for (int i = 0; i < num_sps && pos + 2 <= size; ++i) {
            const int len = (data[pos] << 8) | data[pos + 1];
            pos += 2;
            if (len <= 0 || pos + len > size) {
                break;
            }
            sets.sps.emplace_back(data + pos, data + pos + len);
            pos += len;
        }
        if (pos < size) {
            const int num_pps = data[pos++];
            for (int i = 0; i < num_pps && pos + 2 <= size; ++i) {
                const int len = (data[pos] << 8) | data[pos + 1];
                pos += 2;
                if (len <= 0 || pos + len > size) {
                    break;
                }
                sets.pps.emplace_back(data + pos, data + pos + len);
                pos += len;
            }
        }
        return sets;
    }

    return scanH264AnnexB(data, size);
}

void parseH264Sps(const uint8_t* nal, std::size_t size, SyntaxBuilder& builder, uint32_t parent,
                  H264SpsContext* ctx) {
    std::vector<uint8_t> rbsp;
    removeEmulationPrevention(nal, size, rbsp);
    BitReader reader(rbsp.data(), rbsp.size());

    const uint32_t sps = builder.addStructure(parent, "seq_parameter_set_rbsp");

    const uint32_t forbidden = reader.readBits(1);
    builder.addField(sps, "forbidden_zero_bit", num(forbidden), 0, 1);
    const uint32_t nal_ref_idc = reader.readBits(2);
    builder.addField(sps, "nal_ref_idc", num(nal_ref_idc), 1, 2);
    const uint32_t nal_type = reader.readBits(5);
    builder.addField(sps, "nal_unit_type", num(nal_type), 3, 5);

    const uint32_t profile_idc = reader.readBits(8);
    builder.addField(sps, "profile_idc", hex(profile_idc), 8, 8);
    const uint32_t constraints = reader.readBits(8);
    builder.addField(sps, "constraint_set_flags", hex(constraints), 16, 8);
    const uint32_t level_idc = reader.readBits(8);
    builder.addField(sps, "level_idc", num(level_idc), 24, 8);

    const uint32_t sps_id = reader.readUE();
    builder.addField(sps, "seq_parameter_set_id", num(sps_id),
                     static_cast<int64_t>(reader.bitPosition()) - 1, 0, SyntaxKind::Field);

    int chroma_format_idc = 1;
    int bit_depth_luma = 8;
    int bit_depth_chroma = 8;
    bool separate_colour_plane = false;

    const bool high_profile = (profile_idc == 100 || profile_idc == 110 ||
                               profile_idc == 122 || profile_idc == 244 ||
                               profile_idc == 44 || profile_idc == 83 ||
                               profile_idc == 86 || profile_idc == 118 ||
                               profile_idc == 128 || profile_idc == 138 ||
                               profile_idc == 139 || profile_idc == 134 ||
                               profile_idc == 135);
    if (high_profile) {
        chroma_format_idc = static_cast<int>(reader.readUE());
        builder.addField(sps, "chroma_format_idc", num(chroma_format_idc), reader.bitPosition(), 0);
        if (chroma_format_idc == 3) {
            const uint32_t separate = reader.readBits(1);
            separate_colour_plane = separate != 0;
            builder.addField(sps, "separate_colour_plane_flag", num(separate),
                             static_cast<int64_t>(reader.bitPosition()) - 1, 1);
        }
        bit_depth_luma = 8 + static_cast<int>(reader.readUE());
        builder.addField(sps, "bit_depth_luma_minus8",
                         num(static_cast<uint32_t>(bit_depth_luma - 8)), 0, 0);
        bit_depth_chroma = 8 + static_cast<int>(reader.readUE());
        builder.addField(sps, "bit_depth_chroma_minus8",
                         num(static_cast<uint32_t>(bit_depth_chroma - 8)), 0, 0);
        const uint32_t qpprime = reader.readBits(1);
        builder.addField(sps, "qpprime_y_zero_transform_bypass_flag", num(qpprime), 0, 1);

        const uint32_t scaling_present = reader.readBits(1);
        builder.addField(sps, "seq_scaling_matrix_present_flag", num(scaling_present), 0, 1);
        if (scaling_present != 0) {
            const int count = (chroma_format_idc != 3) ? 8 : 12;
            for (int i = 0; i < count && reader.good(); ++i) {
                const uint32_t present = reader.readBits(1);
                builder.addField(sps, "seq_scaling_list_present_flag[" + num(i) + "]",
                                 num(present), 0, 1);
                if (present != 0) {
                    skipScalingList(reader, i < 6 ? 16 : 64);
                }
            }
        }
    }

    const uint32_t log2_max_frame_num = reader.readUE() + 4;
    builder.addField(sps, "log2_max_frame_num_minus4",
                     num(static_cast<uint32_t>(log2_max_frame_num - 4)), 0, 0);

    const uint32_t poc_type = reader.readUE();
    builder.addField(sps, "pic_order_cnt_type", num(poc_type), 0, 0);
    int log2_max_poc_lsb = 0;
    bool delta_poc_always_zero = false;
    if (poc_type == 0) {
        const uint32_t v = reader.readUE();
        log2_max_poc_lsb = static_cast<int>(v) + 4;
        builder.addField(sps, "log2_max_pic_order_cnt_lsb_minus4", num(v), 0, 0);
    } else if (poc_type == 1) {
        delta_poc_always_zero = reader.readBits(1) != 0;
        builder.addField(sps, "delta_pic_order_always_zero_flag",
                         num(delta_poc_always_zero ? 1u : 0u), 0, 1);
        builder.addField(sps, "offset_for_non_ref_pic", num(static_cast<uint32_t>(reader.readSE())), 0, 0);
        builder.addField(sps, "offset_for_top_to_bottom_field",
                         num(static_cast<uint32_t>(reader.readSE())), 0, 0);
        const uint32_t cycle = reader.readUE();
        builder.addField(sps, "num_ref_frames_in_pic_order_cnt_cycle", num(cycle), 0, 0);
        for (uint32_t i = 0; i < cycle && reader.good(); ++i) {
            reader.readSE();
        }
    }

    builder.addField(sps, "max_num_ref_frames", num(reader.readUE()), 0, 0);
    builder.addField(sps, "gaps_in_frame_num_value_allowed_flag",
                     num(reader.readBits(1)), 0, 1);

    const uint32_t width_mbs = reader.readUE() + 1;
    builder.addField(sps, "pic_width_in_mbs_minus1", num(width_mbs - 1), 0, 0);
    const uint32_t height_map_units = reader.readUE() + 1;
    builder.addField(sps, "pic_height_in_map_units_minus1", num(height_map_units - 1), 0, 0);

    const uint32_t frame_mbs_only = reader.readBits(1);
    builder.addField(sps, "frame_mbs_only_flag", num(frame_mbs_only), 0, 1);
    if (frame_mbs_only == 0) {
        builder.addField(sps, "mb_adaptive_frame_field_flag", num(reader.readBits(1)), 0, 1);
    }
    builder.addField(sps, "direct_8x8_inference_flag", num(reader.readBits(1)), 0, 1);

    const uint32_t crop_flag = reader.readBits(1);
    builder.addField(sps, "frame_cropping_flag", num(crop_flag), 0, 1);
    uint32_t crop_left = 0;
    uint32_t crop_right = 0;
    uint32_t crop_top = 0;
    uint32_t crop_bottom = 0;
    if (crop_flag != 0) {
        crop_left = reader.readUE();
        crop_right = reader.readUE();
        crop_top = reader.readUE();
        crop_bottom = reader.readUE();
        builder.addField(sps, "frame_crop_left_offset", num(crop_left), 0, 0);
        builder.addField(sps, "frame_crop_right_offset", num(crop_right), 0, 0);
        builder.addField(sps, "frame_crop_top_offset", num(crop_top), 0, 0);
        builder.addField(sps, "frame_crop_bottom_offset", num(crop_bottom), 0, 0);
    }

    const std::size_t vui_start = reader.bitPosition();
    const uint32_t vui_present = reader.good() ? reader.readBits(1) : 0;
    builder.addField(sps, "vui_parameters_present_flag", num(vui_present),
                     static_cast<int64_t>(vui_start), 1);
    if (vui_present != 0) {
        parseH264Vui(reader, builder, sps);
    }

    // Derive the coded picture size.
    const int sub_width_c = (chroma_format_idc == 1 || chroma_format_idc == 2) ? 2 :
                            (chroma_format_idc == 3 ? 1 : 1);
    const int sub_height_c = (chroma_format_idc == 1) ? 2 : 1;
    int width = static_cast<int>(width_mbs) * 16;
    int height = static_cast<int>(height_map_units) * 16 * (2 - static_cast<int>(frame_mbs_only));
    const int crop_unit_x = sub_width_c;
    const int crop_unit_y = sub_height_c * (2 - static_cast<int>(frame_mbs_only));
    width -= static_cast<int>(crop_left + crop_right) * crop_unit_x;
    height -= static_cast<int>(crop_top + crop_bottom) * crop_unit_y;

    builder.addField(sps, "coded_width", num(static_cast<uint32_t>(width)), -1, -1,
                     SyntaxKind::Derived);
    builder.addField(sps, "coded_height", num(static_cast<uint32_t>(height)), -1, -1,
                     SyntaxKind::Derived);
    builder.addField(sps, "bit_depth_luma", num(static_cast<uint32_t>(bit_depth_luma)), -1, -1,
                     SyntaxKind::Derived);
    builder.addField(sps, "bit_depth_chroma", num(static_cast<uint32_t>(bit_depth_chroma)), -1, -1,
                     SyntaxKind::Derived);

    if (ctx != nullptr) {
        ctx->valid = true;
        ctx->profile_idc = static_cast<int>(profile_idc);
        ctx->chroma_format_idc = chroma_format_idc;
        ctx->log2_max_frame_num = static_cast<int>(log2_max_frame_num);
        ctx->pic_order_cnt_type = static_cast<int>(poc_type);
        ctx->log2_max_pic_order_cnt_lsb = log2_max_poc_lsb;
        ctx->delta_pic_order_always_zero = delta_poc_always_zero;
        ctx->frame_mbs_only_flag = static_cast<int>(frame_mbs_only);
        ctx->separate_colour_plane_flag = separate_colour_plane;
    }
}

void parseH264Pps(const uint8_t* nal, std::size_t size, SyntaxBuilder& builder, uint32_t parent,
                  H264PpsContext* ctx) {
    std::vector<uint8_t> rbsp;
    removeEmulationPrevention(nal, size, rbsp);
    BitReader reader(rbsp.data(), rbsp.size());

    const uint32_t pps = builder.addStructure(parent, "pic_parameter_set_rbsp");

    builder.addField(pps, "forbidden_zero_bit", num(reader.readBits(1)), 0, 1);
    builder.addField(pps, "nal_ref_idc", num(reader.readBits(2)), 1, 2);
    builder.addField(pps, "nal_unit_type", num(reader.readBits(5)), 3, 5);

    builder.addField(pps, "pic_parameter_set_id", num(reader.readUE()), 0, 0);
    builder.addField(pps, "seq_parameter_set_id", num(reader.readUE()), 0, 0);
    const uint32_t entropy_coding = reader.readBits(1);
    builder.addField(pps, "entropy_coding_mode_flag", num(entropy_coding), 0, 1);
    const uint32_t bottom_field_poc = reader.readBits(1);
    builder.addField(pps, "bottom_field_pic_order_in_frame_present_flag",
                     num(bottom_field_poc), 0, 1);

    const uint32_t slice_groups = reader.readUE();
    builder.addField(pps, "num_slice_groups_minus1", num(slice_groups), 0, 0);
    if (slice_groups > 0) {
        // Slice-group maps are uncommon; stop here to avoid mis-parsing.
        builder.addField(pps, "slice_group_map (unparsed)", "", -1, -1,
                         SyntaxKind::Reserved);
        return;
    }

    builder.addField(pps, "num_ref_idx_l0_default_active_minus1", num(reader.readUE()), 0, 0);
    builder.addField(pps, "num_ref_idx_l1_default_active_minus1", num(reader.readUE()), 0, 0);
    builder.addField(pps, "weighted_pred_flag", num(reader.readBits(1)), 0, 1);
    builder.addField(pps, "weighted_bipred_idc", num(reader.readBits(2)), 0, 2);
    elemSE(reader, builder, pps, "pic_init_qp_minus26");
    elemSE(reader, builder, pps, "pic_init_qs_minus26");
    elemSE(reader, builder, pps, "chroma_qp_index_offset");
    builder.addField(pps, "deblocking_filter_control_present_flag", num(reader.readBits(1)), 0, 1);
    builder.addField(pps, "constrained_intra_pred_flag", num(reader.readBits(1)), 0, 1);
    const uint32_t redundant_pic_cnt = reader.readBits(1);
    builder.addField(pps, "redundant_pic_cnt_present_flag", num(redundant_pic_cnt), 0, 1);

    if (ctx != nullptr) {
        ctx->valid = true;
        ctx->entropy_coding_mode_flag = entropy_coding != 0;
        ctx->bottom_field_pic_order_in_frame_present_flag = bottom_field_poc != 0;
        ctx->redundant_pic_cnt_present_flag = redundant_pic_cnt != 0;
    }

    // Optional trailing extension (present for high profiles).
    if (reader.good() && reader.moreRbspData()) {
        const uint32_t t8x8 = elemU(reader, builder, pps, "transform_8x8_mode_flag", 1);
        if (elemU(reader, builder, pps, "pic_scaling_matrix_present_flag", 1)) {
            const int count = 6 + (t8x8 ? 2 : 0);  // assume 4:2:0 chroma
            for (int i = 0; i < count && reader.good(); ++i) {
                const std::string name =
                    "pic_scaling_list_present_flag[" + num(static_cast<uint32_t>(i)) + "]";
                if (elemU(reader, builder, pps, name.c_str(), 1)) {
                    skipScalingList(reader, i < 6 ? 16 : 64);
                }
            }
        }
        elemSE(reader, builder, pps, "second_chroma_qp_index_offset");
    }
}

void parseH264SliceHeader(const uint8_t* nal, std::size_t size, const H264SpsContext& sps,
                          const H264PpsContext& pps, SyntaxBuilder& builder, uint32_t parent) {
    std::vector<uint8_t> rbsp;
    removeEmulationPrevention(nal, size, rbsp);
    BitReader reader(rbsp.data(), rbsp.size());
    const uint32_t node = builder.addStructure(parent, "slice_header");

    builder.addField(node, "forbidden_zero_bit", num(reader.readBits(1)), 0, 1);
    builder.addField(node, "nal_ref_idc", num(reader.readBits(2)), 1, 2);
    const uint32_t nal_type = reader.readBits(5);
    builder.addField(node, "nal_unit_type", num(nal_type), 3, 5);

    const std::size_t first_mb_off = reader.bitPosition();
    const uint32_t first_mb = reader.readUE();
    builder.addField(node, "first_mb_in_slice", num(first_mb),
                     static_cast<int64_t>(first_mb_off),
                     static_cast<int64_t>(reader.bitPosition() - first_mb_off));

    const std::size_t st_off = reader.bitPosition();
    const uint32_t slice_type = reader.readUE();
    builder.addField(node, "slice_type", num(slice_type),
                     static_cast<int64_t>(st_off),
                     static_cast<int64_t>(reader.bitPosition() - st_off));
    const char* type_name =
        (slice_type == 0 || slice_type == 5)   ? "P"
        : (slice_type == 1 || slice_type == 6) ? "B"
        : (slice_type == 2 || slice_type == 7) ? "I"
        : (slice_type == 3 || slice_type == 8) ? "SP"
        : (slice_type == 4 || slice_type == 9) ? "SI"
                                               : "?";
    builder.addField(node, "slice_type_name", type_name, -1, -1, SyntaxKind::Derived);

    elemUE(reader, builder, node, "pic_parameter_set_id");

    if (sps.separate_colour_plane_flag) {
        elemU(reader, builder, node, "colour_plane_id", 2);
    }
    if (sps.log2_max_frame_num > 0) {
        elemU(reader, builder, node, "frame_num", sps.log2_max_frame_num);
    }

    bool field_pic = false;
    if (!sps.frame_mbs_only_flag) {
        field_pic = elemU(reader, builder, node, "field_pic_flag", 1) != 0;
        if (field_pic) {
            elemU(reader, builder, node, "bottom_field_flag", 1);
        }
    }
    if (nal_type == 5) {
        elemUE(reader, builder, node, "idr_pic_id");
    }

    if (sps.pic_order_cnt_type == 0) {
        if (sps.log2_max_pic_order_cnt_lsb > 0) {
            elemU(reader, builder, node, "pic_order_cnt_lsb", sps.log2_max_pic_order_cnt_lsb);
        }
        if (pps.bottom_field_pic_order_in_frame_present_flag && !field_pic) {
            elemSE(reader, builder, node, "delta_pic_order_cnt_bottom");
        }
    } else if (sps.pic_order_cnt_type == 1 && !sps.delta_pic_order_always_zero) {
        elemSE(reader, builder, node, "delta_pic_order_cnt[0]");
        if (pps.bottom_field_pic_order_in_frame_present_flag && !field_pic) {
            elemSE(reader, builder, node, "delta_pic_order_cnt[1]");
        }
    }
    if (pps.redundant_pic_cnt_present_flag) {
        elemUE(reader, builder, node, "redundant_pic_cnt");
    }

    builder.addField(node, "note",
                     "later slice-header fields (ref lists / dec ref marking / slice QP) "
                     "not expanded",
                     -1, -1, SyntaxKind::Reserved);
}

}  // namespace vide
