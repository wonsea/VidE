#include "core/bitstream_index.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace vide {
namespace {

// Finds Annex-B NAL start offsets (position of the NAL header byte) in a file.
// A 3-byte 00 00 01 pattern is matched; for 4-byte 00 00 00 01 start codes the
// match lands on the second 00 so `+3` still points at the header.
std::vector<uint64_t> scanNalStarts(const std::string& path, uint64_t max_units) {
    std::vector<uint64_t> starts;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return starts;
    }
    constexpr std::size_t kChunk = 1u << 20;
    std::vector<uint8_t> buf(kChunk + 2);
    uint64_t base = 0;
    std::size_t carry = 0;
    bool done = false;
    while (!done) {
        in.read(reinterpret_cast<char*>(buf.data() + carry),
                static_cast<std::streamsize>(kChunk));
        const std::size_t got = static_cast<std::size_t>(in.gcount());
        if (got == 0) {
            break;
        }
        const std::size_t total = carry + got;
        for (std::size_t i = 0; i + 3 <= total; ++i) {
            if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1) {
                const uint64_t header = base + i + 3;
                if (!starts.empty() && header == starts.back()) {
                    continue;
                }
                starts.push_back(header);
                if (starts.size() >= max_units) {
                    done = true;
                    break;
                }
            }
        }
        if (done) {
            break;
        }
        // Keep the last 2 bytes so a start code split across chunks is seen.
        carry = std::min<std::size_t>(2, total);
        std::copy(buf.data() + total - carry, buf.data() + total, buf.data());
        base += got;
        if (static_cast<std::size_t>(in.gcount()) < kChunk) {
            break;
        }
    }
    return starts;
}

}  // namespace

BsIndex indexAnnexB(const std::string& path, CodecId codec, uint64_t max_units) {
    BsIndex idx;
    idx.codec = codec;

    std::error_code ec;
    idx.file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        idx.file_size = 0;
    }

    const std::vector<uint64_t> starts = scanNalStarts(path, max_units);
    if (starts.empty()) {
        return idx;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return idx;
    }

    idx.units.reserve(starts.size());
    for (std::size_t i = 0; i < starts.size(); ++i) {
        BsUnit u;
        u.offset = starts[i];
        const uint64_t end = (i + 1 < starts.size()) ? starts[i + 1] : idx.file_size;
        u.size = static_cast<uint32_t>(end > u.offset ? end - u.offset : 0);

        in.seekg(static_cast<std::streamoff>(u.offset));
        std::array<uint8_t, 2> hdr{0, 0};
        in.read(reinterpret_cast<char*>(hdr.data()), 2);
        const int b0 = hdr[0];

        switch (codec) {
            case CodecId::H264:
                u.type = b0 & 0x1F;
                u.vcl = (u.type >= 1 && u.type <= 5);
                break;
            case CodecId::HEVC:
                u.type = (b0 >> 1) & 0x3F;
                u.temporal_id = hdr[1] & 0x07;
                u.vcl = (u.type <= 31);
                break;
            case CodecId::VVC:
                u.type = (hdr[1] >> 3) & 0x1F;
                u.temporal_id = (hdr[1] & 0x07) - 1;
                u.vcl = (u.type <= 11);
                break;
            default:
                u.type = -1;
                break;
        }
        idx.units.push_back(u);
    }

    for (std::size_t i = 0; i < idx.units.size(); ++i) {
        if (idx.units[i].vcl) {
            idx.vcl_units.push_back(static_cast<uint32_t>(i));
        }
    }
    idx.total_units = starts.size();
    idx.truncated = (starts.size() >= max_units);
    return idx;
}

BsIndex indexIvf(const std::string& path, CodecId codec, uint64_t max_units) {
    BsIndex idx;
    idx.codec = codec;
    idx.annex_b = false;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return idx;
    }
    in.seekg(0, std::ios::end);
    idx.file_size = static_cast<uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    uint8_t hdr[32];
    in.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(hdr)) ||
        hdr[0] != 'D' || hdr[1] != 'K' || hdr[2] != 'I' || hdr[3] != 'F') {
        return idx;
    }
    const uint32_t header_len = static_cast<uint32_t>(hdr[6]) | (static_cast<uint32_t>(hdr[7]) << 8);
    uint64_t pos = header_len < 32 ? 32 : header_len;

    while (pos + 12 <= idx.file_size && idx.units.size() < max_units) {
        uint8_t fh[12];
        in.seekg(static_cast<std::streamoff>(pos));
        in.read(reinterpret_cast<char*>(fh), sizeof(fh));
        if (in.gcount() != static_cast<std::streamsize>(sizeof(fh))) {
            break;
        }
        const uint32_t size = static_cast<uint32_t>(fh[0]) | (static_cast<uint32_t>(fh[1]) << 8) |
                              (static_cast<uint32_t>(fh[2]) << 16) |
                              (static_cast<uint32_t>(fh[3]) << 24);
        BsUnit u;
        u.offset = pos + 12;
        u.size = size;
        u.type = 0;
        u.vcl = true;
        idx.units.push_back(u);
        pos += 12u + size;
    }

    for (std::size_t i = 0; i < idx.units.size(); ++i) {
        idx.vcl_units.push_back(static_cast<uint32_t>(i));
    }
    idx.total_units = idx.units.size();
    idx.truncated = (idx.units.size() >= max_units);
    return idx;
}

std::string nalTypeName(CodecId codec, int type) {
    static const char* const h264[] = {
        "unspecified",       "slice (non-IDR)",   "slice data A",  "slice data B",
        "slice data C",      "slice (IDR)",       "SEI",           "SPS",
        "PPS",               "AUD",               "end of sequence", "end of stream",
        "filler data",       "SPS extension",     "prefix NAL",    "subset SPS"};
    static const char* const hevc[] = {
        "TRAIL_N", "TRAIL_R", "TSA_N",  "TSA_R",  "STSA_N", "STSA_R", "RADL_N",
        "RADL_R",  "RASL_N",  "RASL_R", "RSV_VCL_N10", "RSV_VCL_R11", "RSV_VCL_N12",
        "RSV_VCL_R13", "RSV_VCL_N14", "RSV_VCL_R15", "BLA_W_LP", "BLA_W_RADL",
        "BLA_N_LP", "IDR_W_RADL", "IDR_N_LP", "CRA_NUT", "RSV_IRAP_VCL22",
        "RSV_IRAP_VCL23", "RSV_VCL24", "RSV_VCL25", "RSV_VCL26", "RSV_VCL27",
        "RSV_VCL28", "RSV_VCL29", "RSV_VCL30", "RSV_VCL31", "VPS", "SPS", "PPS",
        "AUD", "EOS", "EOB", "filler", "prefix SEI", "suffix SEI"};
    static const char* const vvc[] = {
        "TRAIL", "STSA", "RADL", "RASL", "IDR_W_RADL", "IDR_N_LP", "CRA", "GDR",
        "RSV_IRAP", "RSV_VCL9", "RSV_VCL10", "RSV_VCL11", "OPI", "DCI", "VPS",
        "SPS", "PPS", "prefix APS", "suffix APS", "PH", "AUD", "EOS", "EOB",
        "prefix SEI", "suffix SEI", "FD"};

    auto lookup = [&](const char* const* arr, std::size_t n, const char* fallback) -> std::string {
        if (type >= 0 && static_cast<std::size_t>(type) < n) {
            return arr[type];
        }
        return fallback;
    };
    switch (codec) {
        case CodecId::H264:
            return lookup(h264, std::size(h264), "NAL");
        case CodecId::HEVC:
            return lookup(hevc, std::size(hevc), "NAL");
        case CodecId::VVC:
            return lookup(vvc, std::size(vvc), "NAL");
        case CodecId::VP9:
        case CodecId::AV1:
            return "frame";
        default:
            return "unit";
    }
}

std::vector<uint8_t> readByteRange(const std::string& path, uint64_t offset, uint32_t size) {
    std::vector<uint8_t> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return out;
    }
    in.seekg(static_cast<std::streamoff>(offset));
    if (!in) {
        return out;
    }
    out.resize(size);
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    out.resize(static_cast<std::size_t>(in.gcount()));
    return out;
}

std::vector<uint32_t> rbspToRawByteMap(const uint8_t* raw, std::size_t size) {
    std::vector<uint32_t> map;
    map.reserve(size);
    int zeros = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const uint8_t b = raw[i];
        if (zeros >= 2 && b == 0x03) {
            zeros = 0;  // emulation-prevention byte: removed from the RBSP
            continue;
        }
        map.push_back(static_cast<uint32_t>(i));
        zeros = (b == 0) ? zeros + 1 : 0;
    }
    return map;
}

}  // namespace vide
