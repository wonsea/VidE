// CodecSession: display-order <-> pack(decode-order) mapping. The .vpk stores
// frames in decode order; the UI works in display order (POC order). Split out
// of codec_session.cpp.
#include "core/codec_session.h"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vide {

void CodecSession::ensureDisplayOrder() const {
    const uint32_t n = reader_ == nullptr ? 0 : reader_->frameCount();
    if (reader_ == nullptr) {
        display_to_pack_.clear();
        display_built_count_ = 0xFFFFFFFFu;
        return;
    }
    if (display_built_count_ == n) {
        return;  // cached
    }
    display_to_pack_.clear();
    display_built_count_ = n;
    if (n == 0) {
        return;
    }
    std::vector<std::pair<int64_t, uint32_t>> pairs;
    pairs.reserve(n);
    bool distinct = true;
    std::unordered_map<int64_t, int> seen;
    for (uint32_t i = 0; i < n; ++i) {
        auto meta = reader_->frameMeta(i);
        const int64_t poc = meta.ok() ? meta.value().poc : static_cast<int64_t>(i);
        pairs.emplace_back(poc, i);
        if (++seen[poc] > 1) {
            distinct = false;
        }
    }
    if (!distinct) {
        return;  // identity mapping
    }
    std::stable_sort(pairs.begin(), pairs.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    bool identity = true;
    for (uint32_t i = 0; i < n; ++i) {
        if (pairs[i].second != i) {
            identity = false;
            break;
        }
    }
    if (identity) {
        return;  // already in display order
    }
    display_to_pack_.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        display_to_pack_[i] = pairs[i].second;
    }
}

uint32_t CodecSession::packIndex(uint32_t display_index) const {
    ensureDisplayOrder();
    if (display_index < display_to_pack_.size()) {
        return display_to_pack_[display_index];
    }
    return display_index;
}

}  // namespace vide
