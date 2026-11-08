#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/bitstream_index.h"
#include "test_framework.h"

using namespace vide;

namespace {

std::string writeTempFile(const std::string& name, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path(ec) / "vide_tests_root" / "tmp";
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

}  // namespace

int runBitstreamIndexTests() {
    // H.264 Annex-B: 4-byte start + SPS(7), 3-byte start + PPS(8), 3-byte + IDR(5).
    const std::vector<uint8_t> h264 = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0xAA, 0xBB, 0xCC,  // SPS  -> offset 4
        0x00, 0x00, 0x01,       0x68, 0xDD,              // PPS  -> offset 11
        0x00, 0x00, 0x01,       0x65, 0xEE, 0xFF};       // IDR  -> offset 16
    const std::string h264_path = writeTempFile("test_h264.264", h264);
    const BsIndex h = indexAnnexB(h264_path, CodecId::H264);
    CHECK(h.units.size() == 3);
    CHECK(h.file_size == h264.size());
    CHECK(h.units[0].offset == 4 && h.units[0].type == 7 && !h.units[0].vcl);
    CHECK(h.units[1].offset == 11 && h.units[1].type == 8 && !h.units[1].vcl);
    CHECK(h.units[2].offset == 16 && h.units[2].type == 5 && h.units[2].vcl);
    CHECK(h.units[0].size == 7);
    CHECK(h.units[1].size == 5);
    CHECK(h.units[2].size == 3);
    CHECK(h.vcl_units.size() == 1 && h.vcl_units[0] == 2);

    const std::vector<uint8_t> slice = readByteRange(h264_path, 16, 3);
    CHECK(slice.size() == 3 && slice[0] == 0x65 && slice[1] == 0xEE && slice[2] == 0xFF);
    CHECK(nalTypeName(CodecId::H264, 7) == "SPS");

    // HEVC Annex-B: 2-byte NAL header; SPS type 33 => first byte 0x42.
    const std::vector<uint8_t> hevc = {
        0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0xAB,  // SPS(33), temporal_id 1
        0x00, 0x00, 0x01,       0x26, 0x01, 0xCD}; // IDR(19), vcl
    const std::string hevc_path = writeTempFile("test_hevc.265", hevc);
    const BsIndex v = indexAnnexB(hevc_path, CodecId::HEVC);
    CHECK(v.units.size() == 2);
    CHECK(v.units[0].type == 33 && !v.units[0].vcl);
    CHECK(v.units[0].temporal_id == 1);
    CHECK(v.units[1].type == 19 && v.units[1].vcl);
    CHECK(nalTypeName(CodecId::HEVC, 33) == "SPS");

    // VVC Annex-B: 2-byte NAL header; VPS type 14 => first byte (14<<3)|... .
    const std::vector<uint8_t> vvc = {
        0x00, 0x00, 0x01, 0x00, 0x74, 0x01, 0x11,  // (b0 reserved, b1 = 14<<3 = 0x70 + tid 1)
        0x00, 0x00, 0x01, 0x00, 0x02, 0x01, 0x22}; // slice type 0, vcl
    const std::string vvc_path = writeTempFile("test_vvc.266", vvc);
    const BsIndex c = indexAnnexB(vvc_path, CodecId::VVC);
    CHECK(c.units.size() == 2);
    CHECK(c.units[0].type == 14 && !c.units[0].vcl);
    CHECK(c.units[1].type == 0 && c.units[1].vcl);
    CHECK(nalTypeName(CodecId::VVC, 15) == "SPS");

    // Missing file: no units, no crash.
    const BsIndex missing = indexAnnexB("Z:/definitely/not/here.264", CodecId::H264);
    CHECK(missing.units.empty());

    // EPB-aware RBSP -> raw byte mapping.
    {
        const std::vector<uint8_t> raw = {0x00, 0x00, 0x03, 0x01, 0xAB};
        const std::vector<uint32_t> map = rbspToRawByteMap(raw.data(), raw.size());
        CHECK(map.size() == 4);
        CHECK(map[0] == 0 && map[1] == 1 && map[2] == 3 && map[3] == 4);
    }
    {
        const std::vector<uint8_t> raw = {0x00, 0x00, 0x00, 0x03, 0x02};
        const std::vector<uint32_t> map = rbspToRawByteMap(raw.data(), raw.size());
        CHECK(map.size() == 4);
        CHECK(map[3] == 4);
    }

    // IVF container (VP9/AV1): header 32B + frames {size(4) + timestamp(8) + data}.
    {
        std::vector<uint8_t> ivf = {'D', 'K', 'I', 'F', 0, 0, 32, 0,
                                    'V', 'P', '9', '0', 0x40, 0x01, 0xF0, 0x00,
                                    30, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0};
        auto put_frame = [&ivf](uint32_t sz, const std::vector<uint8_t>& data) {
            for (int b = 0; b < 4; ++b) ivf.push_back(static_cast<uint8_t>((sz >> (8 * b)) & 0xFF));
            for (int b = 0; b < 8; ++b) ivf.push_back(0);
            ivf.insert(ivf.end(), data.begin(), data.end());
        };
        put_frame(5, {1, 2, 3, 4, 5});
        put_frame(3, {6, 7, 8});
        const std::string ivf_path = writeTempFile("test_vp9.ivf", ivf);
        const BsIndex iv = indexIvf(ivf_path, CodecId::VP9);
        CHECK(!iv.annex_b);
        CHECK(iv.units.size() == 2);
        CHECK(iv.units[0].offset == 44 && iv.units[0].size == 5);
        CHECK(iv.units[1].offset == 61 && iv.units[1].size == 3);
        CHECK(iv.vcl_units.size() == 2);
    }

    return test::report("bitstream_index");
}
