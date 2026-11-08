// CodecSession: syntax-tree assembly (header/parameter sets from the main
// process, merged with the reference helper's per-frame syntax). Split out of
// codec_session.cpp to keep that file focused on lifecycle/frame access.
#include "core/codec_session.h"

#include <fstream>
#include <utility>

#include "codec/av1/av1_syntax.h"
#include "codec/h264/h264_syntax.h"
#include "codec/hevc/hevc_syntax.h"
#include "codec/image/jpeg_syntax.h"
#include "codec/image/png_syntax.h"
#include "codec/vp9/vp9_syntax.h"
#include "codec/vvc/vvc_syntax.h"
#include "common/block_syntax.h"
#include "common/syntax_builder.h"

namespace vide {
namespace {

std::vector<uint8_t> readFileHead(const std::string& path, std::size_t max_bytes) {
    std::vector<uint8_t> data;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return data;
    }
    data.resize(max_bytes);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(max_bytes));
    data.resize(static_cast<std::size_t>(file.gcount()));
    return data;
}

// Appends every node/root of `src` to `dst`, re-indexing ids by the current size.
void appendRoots(SyntaxTree& dst, const SyntaxTree& src) {
    const uint32_t offset = static_cast<uint32_t>(dst.nodes.size());
    dst.nodes.reserve(dst.nodes.size() + src.nodes.size());
    for (const SyntaxNode& node : src.nodes) {
        SyntaxNode copy = node;
        copy.id += offset;
        if (copy.parent != kInvalidId) {
            copy.parent += offset;
        }
        for (uint32_t& child : copy.children) {
            child += offset;
        }
        dst.nodes.push_back(std::move(copy));
    }
    for (uint32_t root : src.au_roots) {
        dst.au_roots.push_back(root + offset);
    }
}

}  // namespace

Result<SyntaxTree> CodecSession::syntax(uint32_t index) {
    if (raw_) {
        return Status::error(StatusCode::Unsupported, "raw video has no syntax tree");
    }
    if (info_.codec == CodecId::HEVC || info_.codec == CodecId::H264 ||
        info_.codec == CodecId::VVC || info_.codec == CodecId::VP9 ||
        info_.codec == CodecId::AV1) {
        // Spec-faithful parameter sets (main process) + helper slice / coding tree.
        Result<SyntaxTree> combined = headerSyntax();
        if (reader_ != nullptr) {
            auto from_pack = reader_->syntax(packIndex(index));
            if (from_pack.ok()) {
                appendRoots(combined.value(), from_pack.value());
            }
        }
        return combined;
    }
    Result<SyntaxTree> tree = Status::error(StatusCode::Unsupported, "no syntax tree");
    if (reader_ != nullptr) {
        auto from_pack = reader_->syntax(packIndex(index));
        if (from_pack.ok()) {
            tree = std::move(from_pack);
        }
    }
    if (!tree.ok()) {
        tree = headerSyntax();
    }
    if (tree.ok()) {
        // Codecs whose helper exports a real CU/PU/TU tree already carry block
        // links; only add the derived flat fallback for the others (R1/S-3).
        bool has_real_tree = false;
        for (const SyntaxNode& node : tree.value().nodes) {
            if (node.name == "coding_tree") {
                has_real_tree = true;
                break;
            }
        }
        if (!has_real_tree) {
            auto blocks = this->blocks(index);
            if (blocks.ok()) {
                appendBlockSyntax(tree.value(), blocks.value(), 0);
            }
        }
    }
    return tree;
}

Result<SyntaxTree> CodecSession::headerSyntax() {
    if (header_syntax_loaded_) {
        return header_syntax_;
    }

    SyntaxTree tree;
    SyntaxBuilder builder(tree);
    // Container-level metadata (codec/container/width/height/...) is intentionally
    // not added: it is not bitstream syntax and only cluttered the panel.
    const uint32_t root = builder.addRoot(toString(info_.codec));

    switch (info_.codec) {
        case CodecId::H264: {
            H264ParameterSets sets;
            const std::vector<uint8_t> extradata = media_.extradata();
            if (!extradata.empty()) {
                sets = extractH264ParameterSets(extradata.data(), extradata.size());
            }
            if (sets.empty()) {
                const std::vector<uint8_t> head = readFileHead(path_, 1u << 20);
                sets = scanH264AnnexB(head.data(), head.size());
            }
            // Spec SPS/PPS from the main process; the per-frame slice header is
            // supplied by the JM helper (so it updates with the selected frame).
            for (const auto& sps : sets.sps) {
                parseH264Sps(sps.data(), sps.size(), builder, root);
            }
            for (const auto& pps : sets.pps) {
                parseH264Pps(pps.data(), pps.size(), builder, root);
            }
            if (sets.empty()) {
                builder.addField(root, "parameter_sets",
                                 "not found in container or stream head", -1, -1,
                                 SyntaxKind::Reserved);
            }
            break;
        }
        case CodecId::HEVC: {
            // Spec-faithful VPS/SPS/PPS from the elementary stream (Annex-B).
            const std::string source = es_path_.empty() ? path_ : es_path_;
            const std::vector<uint8_t> data = readFileHead(source, 8u << 20);
            if (parseHevcParameterSets(data.data(), data.size(), tree) == 0) {
                builder.addField(root, "parameter_sets",
                                 "not found in stream (VPS/SPS/PPS)", -1, -1,
                                 SyntaxKind::Reserved);
            }
            break;
        }
        case CodecId::VVC: {
            const std::string source = es_path_.empty() ? path_ : es_path_;
            const std::vector<uint8_t> data = readFileHead(source, 8u << 20);
            if (parseVvcParameterSets(data.data(), data.size(), tree) == 0) {
                builder.addField(root, "parameter_sets",
                                 "not found in stream (VPS/SPS/PPS)", -1, -1,
                                 SyntaxKind::Reserved);
            }
            break;
        }
        case CodecId::JPEG: {
            const std::vector<uint8_t> data = readFileHead(path_, 4u << 20);
            parseJpegSyntax(data.data(), data.size(), builder, root);
            break;
        }
        case CodecId::PNG: {
            const std::vector<uint8_t> data = readFileHead(path_, 4u << 20);
            parsePngSyntax(data.data(), data.size(), builder, root);
            break;
        }
        case CodecId::VP9: {
            const std::vector<uint8_t> data = readFileHead(path_, 8u << 20);
            if (!parseVp9Syntax(data.data(), data.size(), builder, root)) {
                builder.addField(root, "note",
                                 "supply an IVF elementary stream for VP9 header parsing",
                                 -1, -1, SyntaxKind::Reserved);
            }
            break;
        }
        case CodecId::AV1: {
            const std::vector<uint8_t> data = readFileHead(path_, 8u << 20);
            parseAv1Syntax(data.data(), data.size(), builder, root);
            break;
        }
        default:
            builder.addField(root, "note",
                             "detailed syntax for this codec is produced by its reference "
                             "helper",
                             -1, -1, SyntaxKind::Reserved);
            break;
    }

    header_syntax_ = std::move(tree);
    header_syntax_loaded_ = true;
    return header_syntax_;
}

}  // namespace vide
