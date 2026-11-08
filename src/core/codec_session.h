// CodecSession: one opened bitstream in the main process.
//
// Responsibilities:
//   * probe the container and extract an Annex-B elementary stream (FFmpeg)
//   * resolve the content-addressed cache (.vpk); reuse it for instant "second
//     open", otherwise spawn the codec's reference helper to produce it
//   * poll the helper for progress and expose per-frame syntax/blocks/reco
//   * provide asynchronous thumbnails decoded by FFmpeg
//   * manage the session's lifecycle (remove / close)
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "codec/adapter.h"
#include "codec/raw/raw_video_adapter.h"
#include "common/frame.h"
#include "common/status.h"
#include "common/syntax.h"
#include "common/types.h"
#include "core/cache_store.h"
#include "core/helper_process.h"
#include "core/media_source.h"

namespace vide {

enum class SessionState {
    Idle,
    Probing,
    Analyzing,   // helper running
    Ready,       // all frames analysed (helper done)
    Failed,
};

class CodecSession {
public:
    CodecSession();
    ~CodecSession();

    CodecSession(const CodecSession&) = delete;
    CodecSession& operator=(const CodecSession&) = delete;

    Status open(const std::string& path, const OpenOptions& options = {});
    void close();

    // --- state -------------------------------------------------------------
    const std::string& path() const { return path_; }
    // Elementary stream actually decoded (extracted Annex-B, or the input when
    // it already is one). Used by the bitstream hex viewer.
    const std::string& sourcePath() const { return es_path_.empty() ? path_ : es_path_; }
    CodecId codec() const { return info_.codec; }
    const MediaInfo& info() const { return info_; }
    SessionState state() const { return state_; }
    const std::string& error() const { return error_; }
    uint32_t framesReady() const { return frames_ready_; }
    // Total frames for the strip: container count, else the probed fallback
    // count, else the pack high-water mark.
    int64_t frameCount() const;
    // True when a reference helper provides block info (HEVC/VVC).
    bool hasBlockSupport() const { return block_support_; }
    // True when any block info (helper or FFmpeg MVs) is available.
    bool hasMotionInfo() const { return block_support_ || mv_support_; }

    // --- frame access (main thread) ---------------------------------------
    // Indices are in *display order* (matching the thumbnail strip / FFmpeg);
    // the pack is stored in decode order, so a mapping is applied internally.
    // Decoded picture for preview (always FFmpeg / raw, no reference helper).
    Result<DecodedImage> preview(uint32_t index);
    // Reference reconstruction from the pack (helper codecs), or the FFmpeg
    // decode for codecs without a helper.
    Result<DecodedImage> reco(uint32_t index);
    Result<DecodedImage> pred(uint32_t index);  // helper codecs only (reserved)
    Result<DecodedImage> resi(uint32_t index);  // helper codecs only (reserved)
    Result<BlockGrid> blocks(uint32_t index);
    Result<SyntaxTree> syntax(uint32_t index);
    // Frame metadata (display order / coding order / frame type) for the pack.
    Result<FrameMeta> frameMeta(uint32_t index);

    // Asynchronous thumbnail. `requestThumbnail` queues `index`; the worker
    // thread produces it. `copyThumbnail` copies a ready image into `out`
    // (thread-safe: callers never hold a pointer into the worker's storage).
    bool copyThumbnail(uint32_t index, DecodedImage& out);
    void requestThumbnail(uint32_t index);

    // FFmpeg export_mvs fallback block info (AVC/VP9/AV1). Produced by the
    // worker to keep MediaSource single-threaded.
    bool copyMotionBlocks(uint32_t index, BlockGrid& out);
    void requestMotionBlocks(uint32_t index);

    // Pump: advance helper progress and finished thumbnail jobs. Call regularly
    // from the UI/main thread.
    void poll();

private:
    void startThumbnailWorker();
    void thumbnailWorkerLoop();
    std::string helperExecutable() const;
    Result<SyntaxTree> headerSyntax();
    Result<DecodedImage> thumbnailFor(uint32_t index, int max_w, int max_h);
    // Counts decodable frames with FFmpeg (used when the reference helper cannot
    // decode the stream). Resets MediaSource afterwards.
    int64_t probeFrameCount();

    // Rebuilds the display-order -> pack(decode-order) mapping from POC values.
    void ensureDisplayOrder() const;
    uint32_t packIndex(uint32_t display_index) const;

    std::string path_;
    MediaInfo info_{};
    MediaSource media_;
    mutable std::mutex media_mutex_;  // serialises MediaSource across threads
    bool es_extracted_ = false;  // es_path_ is a temporary we must delete
    std::unique_ptr<RawVideoAdapter> raw_;
    SyntaxTree header_syntax_;
    bool header_syntax_loaded_ = false;
    std::string es_path_;
    std::string vpk_path_;
    std::string fingerprint_name_;

    int64_t probed_count_ = -1;  // FFmpeg-decoded count when the helper fails

    std::unique_ptr<VpkReader> reader_;
    std::unique_ptr<HelperProcess> helper_;
    // display index -> pack index; rebuilt as the pack grows. Empty means the
    // identity mapping (no reader / unknown POC).
    mutable std::vector<uint32_t> display_to_pack_;
    mutable uint32_t display_built_count_ = 0xFFFFFFFFu;
    SessionState state_ = SessionState::Idle;
    std::string error_;
    uint32_t frames_ready_ = 0;
    bool block_support_ = false;
    bool mv_support_ = false;  // FFmpeg export_mvs fallback (AVC/VP9/AV1)
    bool helper_launched_ = false;

    // Thumbnail worker. `thumb_order_` implements an LRU bound so a very long
    // stream cannot grow thumbnail memory without limit.
    static constexpr std::size_t kMaxThumbnails = 1024;

    std::thread thumb_thread_;
    std::atomic<bool> thumb_stop_{false};
    std::mutex thumb_mutex_;
    std::condition_variable thumb_cv_;
    std::queue<uint32_t> thumb_requests_;
    std::unordered_map<uint32_t, DecodedImage> thumbnails_;
    std::list<uint32_t> thumb_order_;  // front = most recently used
    std::unordered_map<uint32_t, bool> thumb_pending_;  // queued/in-flight dedup
    std::unordered_map<uint32_t, bool> thumb_failed_;   // decode failed once

    std::queue<uint32_t> block_requests_;
    std::unordered_map<uint32_t, BlockGrid> motion_blocks_;
    std::list<uint32_t> block_order_;  // front = most recently used (bounded)
    std::unordered_map<uint32_t, bool> block_pending_;
};

}  // namespace vide
