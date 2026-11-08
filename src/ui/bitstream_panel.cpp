#include "ui/bitstream_panel.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <imgui.h>

#include "common/frame.h"
#include "common/syntax.h"
#include "core/codec_session.h"
#include "core/session.h"
#include "core/workspace.h"
#include "ui/bitstream_view.h"
#include "ui/ui_util.h"

namespace vide {
namespace {

constexpr uint32_t kMaxUnitBytes = 4u << 20;  // cap per-unit load at 4 MB

bool annexBCodec(CodecId codec) {
    return codec == CodecId::H264 || codec == CodecId::HEVC || codec == CodecId::VVC;
}
bool ivfCodec(CodecId codec) {
    return codec == CodecId::VP9 || codec == CodecId::AV1;
}

bool has(const std::string& name, const char* s) {
    return name.find(s) != std::string::npos;
}

enum class NodeKind { Unknown, Block, Vps, Sps, Pps, Slice };

// Classifies a syntax node by scanning its ancestor chain (parameter sets win).
NodeKind classifyNode(const SyntaxTree& tree, uint32_t id) {
    NodeKind kind = NodeKind::Unknown;
    uint32_t cur = id;
    for (int guard = 0; cur != kInvalidId && cur < tree.nodes.size() && guard < 4096; ++guard) {
        const std::string& n = tree.nodes[cur].name;
        if (kind == NodeKind::Unknown) {
            if (has(n, "video_parameter_set") || n == "vps") {
                kind = NodeKind::Vps;
            } else if (has(n, "seq_parameter_set") || n == "sps") {
                kind = NodeKind::Sps;
            } else if (has(n, "pic_parameter_set") || n == "pps") {
                kind = NodeKind::Pps;
            } else if (has(n, "coding_tree") || has(n, "coding_unit") || has(n, "macroblock") ||
                       has(n, "partition") || has(n, "prediction_unit") ||
                       has(n, "transform_unit") || has(n, "sub_macroblock") || n == "block") {
                kind = NodeKind::Block;
            } else if (has(n, "slice") || has(n, "picture") || has(n, "frame") ||
                       has(n, "header")) {
                kind = NodeKind::Slice;
            }
        }
        const uint32_t p = tree.nodes[cur].parent;
        if (p == cur) {
            break;
        }
        cur = p;
    }
    return kind;
}

// NAL type for a parameter set, or -1 when the codec has none.
int paramNalType(CodecId codec, NodeKind kind) {
    if (kind == NodeKind::Vps) {
        return codec == CodecId::HEVC ? 32 : (codec == CodecId::VVC ? 14 : -1);
    }
    if (kind == NodeKind::Sps) {
        return codec == CodecId::H264 ? 7 : (codec == CodecId::HEVC ? 33 : (codec == CodecId::VVC ? 15 : -1));
    }
    if (kind == NodeKind::Pps) {
        return codec == CodecId::H264 ? 8 : (codec == CodecId::HEVC ? 34 : (codec == CodecId::VVC ? 16 : -1));
    }
    return -1;
}

int findUnitByType(const BsIndex& index, int type) {
    for (std::size_t i = 0; i < index.units.size(); ++i) {
        if (index.units[i].type == type) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Finds a field named `name` anywhere under `root` (or the whole tree when
// `root` is invalid) and parses its integer value.
bool findIntField(const SyntaxTree& tree, uint32_t root, const char* name, int& out) {
    return findSyntaxInt(tree, root, name, out);
}

}  // namespace

void BitstreamPanel::refresh(Workspace& workspace) {
    Session& session = workspace.active();
    CodecSession* codec = session.codec();
    if (codec == nullptr) {
        index_ = BsIndex{};
        path_.clear();
        unit_ = -1;
        bytes_.clear();
        return;
    }
    if (session.generation() == generation_ && codec->sourcePath() == path_) {
        return;
    }
    generation_ = session.generation();
    path_ = codec->sourcePath();
    if (ivfCodec(codec->codec())) {
        index_ = indexIvf(path_, codec->codec());
    } else if (annexBCodec(codec->codec())) {
        index_ = indexAnnexB(path_, codec->codec());
    } else {
        index_ = BsIndex{};
    }
    index_.codec = codec->codec();
    unit_ = -1;
    followed_frame_ = kInvalidId;
    bytes_.clear();
    scroll_pending_ = true;
    scrolled_unit_ = -1;
    scrolled_bit_ = UINT64_MAX;
}

void BitstreamPanel::ensureUnit(Workspace& workspace) {
    Session& session = workspace.active();
    CodecSession* codec = session.codec();
    if (codec == nullptr || index_.units.empty()) {
        unit_ = -1;
        return;
    }

    const uint32_t node_id = session.selection().syntax_node;
    const SyntaxTree* tree = session.currentSyntax();
    const NodeKind kind =
        (node_id != kInvalidId && tree != nullptr) ? classifyNode(*tree, node_id) : NodeKind::Unknown;
    const bool avc = codec->codec() == CodecId::H264;

    int target = -1;
    if (kind == NodeKind::Vps || kind == NodeKind::Sps || kind == NodeKind::Pps) {
        target = findUnitByType(index_, paramNalType(codec->codec(), kind));
    }

    // AVC multi-slice: a block (or slice-header node) carries its global VCL
    // slice ordinal, which indexes directly into the VCL NAL list.
    if (target < 0 && avc) {
        int ordinal = -1;
        uint32_t block_id = session.selection().block;
        if (kind == NodeKind::Block && tree != nullptr) {
            const SyntaxNode* n = tree->find(node_id);
            if (n != nullptr && n->block_id != kInvalidId) {
                block_id = n->block_id;
            }
        }
        if (block_id != kInvalidId) {
            if (const Block* b = findBlock(session.currentBlocks(), block_id)) {
                ordinal = static_cast<int>(b->slice_index);
            }
        }
        if (ordinal < 0 && kind == NodeKind::Slice && tree != nullptr) {
            findIntField(*tree, node_id, "slice_index", ordinal);
        }
        if (ordinal >= 0 && static_cast<std::size_t>(ordinal) < index_.vcl_units.size()) {
            target = static_cast<int>(index_.vcl_units[ordinal]);
        }
    }

    if (target < 0) {
        int ordinal = -1;
        if (avc && tree != nullptr) {
            findIntField(*tree, kInvalidId, "slice_index", ordinal);  // frame's first slice
        }
        if (ordinal >= 0 && static_cast<std::size_t>(ordinal) < index_.vcl_units.size()) {
            target = static_cast<int>(index_.vcl_units[ordinal]);
        } else {
            const uint32_t frame = session.selection().frame;
            if (frame != kInvalidId) {
                auto meta = codec->frameMeta(frame);
                if (meta.ok() &&
                    static_cast<std::size_t>(meta.value().index) < index_.vcl_units.size()) {
                    target = static_cast<int>(index_.vcl_units[meta.value().index]);
                }
            }
        }
    }
    if (target < 0) {
        target = (unit_ >= 0 && static_cast<std::size_t>(unit_) < index_.units.size())
                     ? unit_
                     : static_cast<int>(index_.vcl_units.empty() ? 0 : index_.vcl_units[0]);
    }
    if (target != unit_) {
        unit_ = target;
        const BsUnit& u = index_.units[unit_];
        bytes_ = readByteRange(path_, u.offset, std::min<uint32_t>(u.size, kMaxUnitBytes));
        scroll_pending_ = true;
    }
}

void BitstreamPanel::draw(Workspace& workspace) {
    Session& session = workspace.active();
    if (!ImGui::Begin(name())) {
        ImGui::End();
        return;
    }

    refresh(workspace);

    CodecSession* codec = session.codec();
    if (codec == nullptr || !session.hasDocument()) {
        ImGui::TextDisabled("Open a bitstream to inspect its bytes.");
        ImGui::End();
        return;
    }
    if (!annexBCodec(codec->codec()) && !ivfCodec(codec->codec())) {
        ImGui::TextDisabled("Bitstream view is available for AVC/HEVC/VVC (Annex-B) and VP9/AV1 (IVF).");
        ImGui::End();
        return;
    }

    ensureUnit(workspace);
    if (unit_ < 0 || bytes_.empty()) {
        ImGui::TextDisabled(annexBCodec(codec->codec())
                                ? "No NAL units found in the elementary stream."
                                : "No IVF frames found (is the input a raw elementary stream?).");
        ImGui::End();
        return;
    }
    const BsUnit& u = index_.units[unit_];

    // ---- resolve the highlight (syntax node, else current CU) --------------
    has_hl_ = false;
    hl_block_ = kInvalidId;
    const uint32_t node_id = session.selection().syntax_node;
    const SyntaxTree* tree = session.currentSyntax();
    NodeKind kind = NodeKind::Unknown;
    if (node_id != kInvalidId && tree != nullptr) {
        kind = classifyNode(*tree, node_id);
    }

    // Maps an RBSP bit range (relative to this unit's RBSP) to an absolute file
    // bit range. The end byte is clamped to the unit so CABAC read-ahead past
    // the NAL does not drop the highlight.
    auto setHighlight = [&](uint64_t rbsp_bit, uint64_t bit_len) {
        if (bit_len == 0) {
            return;
        }
        if (index_.annex_b) {
            const std::vector<uint32_t> map = rbspToRawByteMap(bytes_.data(), bytes_.size());
            if (map.empty()) {
                return;
            }
            const uint64_t first = rbsp_bit / 8;
            if (first >= map.size()) {
                return;
            }
            uint64_t last = (rbsp_bit + bit_len - 1) / 8;
            if (last >= map.size()) {
                last = map.size() - 1;
            }
            hl_bit_start_ = (u.offset + map[first]) * 8 + (rbsp_bit & 7);
            hl_bit_end_ = hl_bit_start_ + bit_len - 1;
            has_hl_ = true;
        } else {
            hl_bit_start_ = u.offset * 8 + rbsp_bit;
            hl_bit_end_ = hl_bit_start_ + bit_len - 1;
            has_hl_ = true;
        }
    };

    if ((kind == NodeKind::Vps || kind == NodeKind::Sps || kind == NodeKind::Pps) && tree != nullptr) {
        const SyntaxNode* n = tree->find(node_id);
        if (n != nullptr && n->bit_length > 0 && n->bit_offset >= 0) {
            setHighlight(static_cast<uint64_t>(n->bit_offset), static_cast<uint64_t>(n->bit_length));
        } else {
            setHighlight(0, static_cast<uint64_t>(bytes_.size()) * 8);  // whole NAL
        }
    } else if (kind == NodeKind::Slice) {
        // Slice / picture header: highlight the header region of the current
        // slice NAL (up to the slice-data start, when the syntax exposes it).
        uint64_t hdr_bits = 0;
        if (tree != nullptr) {
            for (const SyntaxNode& n : tree->nodes) {
                if (n.name == "slice_data_start_bit" || n.name == "slice_data_bit") {
                    hdr_bits = std::strtoull(n.value.c_str(), nullptr, 10);
                    break;
                }
            }
        }
        if (hdr_bits == 0) {
            hdr_bits = static_cast<uint64_t>(bytes_.size()) * 8;
        }
        setHighlight(0, hdr_bits);
    } else {
        uint32_t block_id = session.selection().block;
        if (kind == NodeKind::Block && tree != nullptr) {
            const SyntaxNode* n = tree->find(node_id);
            if (n != nullptr && n->block_id != kInvalidId) {
                block_id = n->block_id;
            }
        }
        const Block* block = findBlock(session.currentBlocks(), block_id);
        if (block != nullptr && block->bit_length > 0) {
            hl_block_ = block->id;
            setHighlight(block->bit_offset, block->bit_length);
        }
    }

    // ---- auto-scroll whenever the target position changes ------------------
    const uint64_t anchor = has_hl_ ? hl_bit_start_ : (u.offset * 8);
    if (unit_ != scrolled_unit_ || anchor != scrolled_bit_) {
        scrolled_unit_ = unit_;
        scrolled_bit_ = anchor;
        scroll_pending_ = true;
    }

    // ---- controls ----------------------------------------------------------
    int radix = (radix_ == Radix::Hex) ? 0 : 1;
    ImGui::RadioButton("Hex", &radix, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Binary", &radix, 1);
    radix_ = (radix == 0) ? Radix::Hex : Radix::Binary;
    ImGui::SameLine();
    ImGui::TextDisabled("|  %s", nalTypeName(codec->codec(), u.type).c_str());

    ImGui::Text("File: %s", path_.c_str());
    ImGui::Text("Unit %d/%llu   offset 0x%llX   size %u B", unit_,
                static_cast<unsigned long long>(index_.total_units),
                static_cast<unsigned long long>(u.offset), u.size);
    if (has_hl_) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                           "highlight: bits [%llu, %llu]  (0x%llX)",
                           static_cast<unsigned long long>(hl_bit_start_),
                           static_cast<unsigned long long>(hl_bit_end_),
                           static_cast<unsigned long long>(hl_bit_start_ / 8));
    } else {
        ImGui::TextDisabled("Select an SPS/PPS/VPS/header or a block to highlight it.");
    }

    // ---- viewer ------------------------------------------------------------
    const int bytes_per_row = (radix_ == Radix::Hex) ? 16 : 8;
    BitstreamGrid view;
    view.bytes = bytes_.data();
    view.size = bytes_.size();
    view.base_offset = u.offset;
    view.binary = (radix_ == Radix::Binary);
    view.has_highlight = has_hl_;
    view.hl_bit_start = hl_bit_start_;
    view.hl_bit_end = hl_bit_end_;
    if (anchor / 8 >= u.offset) {
        view.target_row = static_cast<int>((anchor / 8 - u.offset) / bytes_per_row);
    }
    drawBitstreamGrid(view, scroll_pending_);

    ImGui::End();
}

}  // namespace vide
