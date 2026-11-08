// Content-addressed cache store for helper-produced .vpk files.
//
// Responsibilities:
//   * compute a source fingerprint (size+mtime quick key + full xxHash confirm)
//   * locate / create the cache directory under the working directory
//   * open an existing completed pack (fast "second open")
//   * provide random access to per-frame syntax / blocks / reco (mmap-friendly)
//   * enforce the global size limit by evicting *offline* packs only
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/frame.h"
#include "common/status.h"
#include "common/syntax.h"
#include "common/types.h"
#include "common/vpk.h"

namespace vide {

struct SourceFingerprint {
    uint64_t size = 0;
    int64_t mtime = 0;
    uint64_t hash = 0;   // full-content xxHash64
    bool valid = false;  // hash computed

    std::string baseName(CodecId codec) const;
};

// Computes the quick key and (optionally) the full hash of a file.
SourceFingerprint fingerprintFile(const std::string& path, bool full_hash);

// Writes a .vpk produced by a helper. Handles section encoding and footer.
class VpkWriter {
public:
    VpkWriter();
    ~VpkWriter();

    VpkWriter(const VpkWriter&) = delete;
    VpkWriter& operator=(const VpkWriter&) = delete;
    VpkWriter(VpkWriter&&) = delete;
    VpkWriter& operator=(VpkWriter&&) = delete;

    // Begins writing `path`. `frame_count` is an optional hint (0 when unknown);
    // the real count is derived from the frames actually appended.
    Status begin(const std::string& path, CodecId codec, const SourceFingerprint& fp,
                 int width, int height, BitDepth bit_depth, uint32_t frame_count);

    // Appends one frame's payload, refreshes the index and flushes. Safe to call
    // while a reader is refreshing a partial pack. pred/resi may be null.
    Status appendFrame(const FrameMeta& meta, const SyntaxTree* syntax,
                       const BlockGrid* blocks, const DecodedImage* reco,
                       const DecodedImage* pred = nullptr,
                       const DecodedImage* resi = nullptr);

    // Finalises the footer (complete=1) and closes.
    Status finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Read-only view of a pack. It may be re-opened against a partial (in-progress)
// pack: refresh() re-reads header/index/footer and leaves the previous snapshot
// intact when the file is momentarily inconsistent.
class VpkReader {
public:
    Status open(const std::string& path);

    // Re-reads header/index/footer. Returns an error (keeping the last good
    // snapshot) when the file is not yet consistent.
    Status refresh();

    const VpkHeader& header() const { return header_; }
    uint32_t frameCount() const { return header_.frame_count; }
    bool complete() const { return footer_.complete != 0; }

    // Frame index / metadata.
    Result<FrameMeta> frameMeta(uint32_t index) const;
    Result<SyntaxTree> syntax(uint32_t index) const;
    Result<BlockGrid> blocks(uint32_t index) const;
    Result<DecodedImage> reco(uint32_t index) const;
    Result<DecodedImage> pred(uint32_t index) const;  // reserved when absent
    Result<DecodedImage> resi(uint32_t index) const;

private:
    std::string path_;
    VpkHeader header_{};
    VpkFooter footer_{};
    std::vector<VpkFrameEntry> index_;
};

// Manages the cache directory and eviction policy.
class CacheStore {
public:
    static CacheStore& instance();

    // Cache root: <cwd>/vide_cache unless $VIDE_TMP_DIR overrides the base.
    const std::string& root() const { return root_; }

    // Whether the content-addressed cache is used at all (reuse + persist).
    // When disabled, sessions always re-run the helper and delete the pack on
    // close. Default: enabled.
    bool enabled() const { return enabled_; }
    void setEnabled(bool on) { enabled_ = on; }

    void setMaxTotalBytes(uint64_t bytes) { max_total_bytes_ = bytes; }
    uint64_t maxTotalBytes() const { return max_total_bytes_; }

    uint64_t totalBytes() const;
    void refresh();

    // Evicts offline packs (LRU) until total <= 90% of the limit. Never evicts
    // packs whose name is in `in_use_names`. Returns the total after eviction.
    uint64_t evictOffline(const std::vector<std::string>& in_use_names);

    // Removes leftover *.tmp / *.lock from crashed helpers.
    void cleanStale();

    // Removes every offline pack (keeps in-use ones). Returns bytes freed.
    uint64_t clearOffline();

    // Removes every pack regardless of in-use state. Returns bytes freed. The
    // caller is responsible for having closed any session still reading a pack.
    uint64_t clearAll();

    // Lists cached packs (file name + size in bytes), for the cache manager UI.
    std::vector<std::pair<std::string, uint64_t>> list() const;

    struct Entry {
        std::string name;   // e.g. "hevc_0123abcd.vpk"
        std::string codec;  // leading tag before '_' (hevc/vvc/avc/vp9/av1)
        uint64_t size = 0;
        int64_t mtime = 0;  // epoch seconds
        bool in_use = false;
    };
    // Rich listing (name/codec/size/mtime/in-use), sorted by size desc.
    std::vector<Entry> entries() const;

    // Removes one pack by file name. Returns true when it was deleted.
    bool removePack(const std::string& pack_name);

    // Marks a pack (file name, e.g. "hevc_<hash>.vpk") as belonging to an open
    // session; in-use packs are never evicted by capacity management.
    void markInUse(const std::string& pack_name);
    void markOffline(const std::string& pack_name);
    const std::vector<std::string>& inUse() const { return in_use_; }

    // Enforces the global limit. Evicts offline packs first; returns false when
    // the limit is still exceeded with only in-use packs left (ADR D5, plan B).
    bool enforceLimit();

private:
    CacheStore();

    std::string root_;
    uint64_t max_total_bytes_ = 40ull * 1024 * 1024 * 1024;  // 40 GB
    bool enabled_ = true;
    std::vector<std::string> in_use_;
};

}  // namespace vide
