// CodecSession: one opened bitstream in the main process.
//
// This file covers probing, elementary-stream extraction, cache lookup and the
// helper lifecycle (open/close/poll) plus synchronous frame access. The syntax
// assembly lives in codec_session_syntax.cpp, the display-order mapping in
// codec_session_display.cpp and the thumbnail worker in
// codec_session_thumbnail.cpp.
#include "core/codec_session.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <process.h>
#define VIDE_GETPID _getpid
#else
#include <unistd.h>
#define VIDE_GETPID getpid
#endif

namespace vide {
namespace {

const char* helperNameFor(CodecId codec) {
    switch (codec) {
        case CodecId::HEVC: return "vide_hevc_helper";
        case CodecId::VVC: return "vide_vvc_helper";
        case CodecId::H264: return "vide_avc_helper";
        case CodecId::VP9: return "vide_vp9_helper";
        case CodecId::AV1: return "vide_av1_helper";
        default: return nullptr;
    }
}

const char* esSuffixFor(CodecId codec) {
    switch (codec) {
        case CodecId::HEVC: return ".265";
        case CodecId::VVC: return ".266";
        case CodecId::H264: return ".264";
        case CodecId::AV1: return ".av1";
        default: return ".es";
    }
}

const char* bsfFor(CodecId codec) {
    switch (codec) {
        case CodecId::HEVC: return "hevc_mp4toannexb";
        case CodecId::VVC: return "vvc_mp4toannexb";
        case CodecId::H264: return "h264_mp4toannexb";
        // AV1 helpers (libaom) read the original container directly (IVF/OBU);
        // no Annex-B conversion, and FFmpeg's av1_metadata bsf would corrupt IVF.
        default: return nullptr;
    }
}

}  // namespace

CodecSession::CodecSession() = default;

CodecSession::~CodecSession() {
    close();
}

std::string CodecSession::helperExecutable() const {
    const char* name = helperNameFor(media_.info().codec);
    if (name == nullptr) {
        return {};
    }
    // Environment override wins (set by the app from its own directory).
    // Key form: VIDE_<NAME>_PATH, e.g. VIDE_HEVC_HELPER_PATH.
    const char* base = name;
    if (std::strncmp(base, "vide_", 5) == 0) {
        base += 5;
    }
    std::string key = "VIDE_";
    for (const char* p = base; *p; ++p) {
        key += (*p >= 'a' && *p <= 'z') ? static_cast<char>(*p - 32) : *p;
    }
    key += "_PATH";
    if (const char* from_env = std::getenv(key.c_str()); from_env != nullptr && *from_env != '\0') {
        return from_env;
    }
#ifdef _WIN32
    return std::string(name) + ".exe";
#else
    return name;
#endif
}

Status CodecSession::open(const std::string& path, const OpenOptions& options) {
    close();
    path_ = path;

    // Raw video has no container/parameter sets: decode it directly.
    if (options.raw_format != PixelFormat::Unknown) {
        raw_ = std::make_unique<RawVideoAdapter>();
        auto result = raw_->open(options);
        if (!result.ok()) {
            error_ = result.status().message();
            state_ = SessionState::Failed;
            raw_.reset();
            return result.status();
        }
        const StreamInfo& si = result.value();
        info_ = MediaInfo{};
        info_.codec = si.codec;
        info_.format = si.format;
        info_.bit_depth = si.bit_depth;
        info_.chroma = si.chroma;
        info_.width = si.width;
        info_.height = si.height;
        info_.fps_num = si.fps_num;
        info_.fps_den = si.fps_den;
        info_.frame_count = si.frame_count;
        info_.container = si.container;
        block_support_ = false;
        state_ = SessionState::Ready;
        startThumbnailWorker();
        return Status::success();
    }

    Status status = media_.open(path);
    if (!status.ok()) {
        error_ = status.message();
        state_ = SessionState::Failed;
        return status;
    }

    info_ = media_.info();
    const MediaInfo& info = info_;
    block_support_ = helperNameFor(info.codec) != nullptr;

    // Extract the elementary stream the reference decoder consumes. AVC/HEVC/VVC
    // use an Annex-B elementary stream; the AV1 helper reads IVF, so containers
    // are remuxed to IVF; the VP9 helper reads the original container directly.
    if (block_support_) {
        const auto cache_root = std::filesystem::path(CacheStore::instance().root());
        const std::string es_name = "es_" + std::to_string(VIDE_GETPID()) + "_" +
                                    std::to_string(reinterpret_cast<std::uintptr_t>(this));
        const char* bsf = bsfFor(info.codec);
        if (info.codec == CodecId::AV1) {
            es_path_ = (cache_root / (es_name + ".ivf")).string();
            auto es = media_.extractAv1Ivf(es_path_);
            if (!es.ok()) {
                error_ = es.status().message();
                es_path_.clear();
                block_support_ = false;
            } else {
                es_path_ = es.value();
                es_extracted_ = (es_path_ != path_);
            }
        } else if (bsf == nullptr) {
            // No Annex-B converter (e.g. VP9): pass the source path through.
            es_path_ = path_;
            es_extracted_ = false;
        } else {
            es_path_ = (cache_root / (es_name + esSuffixFor(info.codec))).string();
            auto es = media_.extractEs(es_path_, bsf);
            if (!es.ok()) {
                error_ = es.status().message();
                // Non-fatal: thumbnails/syntax still work without a helper.
                es_path_.clear();
                block_support_ = false;
            } else {
                // extractEs may return the original path when the input already
                // is an Annex-B elementary stream; only delete files we created.
                es_path_ = es.value();
                es_extracted_ = (es_path_ != path_);
            }
        }
    }

    // Content fingerprint + cache lookup.
    if (block_support_) {
        SourceFingerprint fp = fingerprintFile(path, true);
        fingerprint_name_ = fp.baseName(info.codec);
        const auto cache_root = std::filesystem::path(CacheStore::instance().root());
        vpk_path_ = (cache_root / (fingerprint_name_ + ".vpk")).string();

        if (CacheStore::instance().enabled() && std::filesystem::exists(vpk_path_)) {
            reader_ = std::make_unique<VpkReader>();
            if (reader_->open(vpk_path_).ok() && reader_->complete()) {
                frames_ready_ = reader_->frameCount();
                state_ = SessionState::Ready;
            } else {
                // Incomplete/stale pack: the helper truncates and rewrites it.
                reader_.reset();
            }
        }

        // Register the pack as in-use so capacity management never evicts it.
        const std::string pack_name = fingerprint_name_ + ".vpk";
        CacheStore::instance().markInUse(pack_name);

        if (reader_ == nullptr) {
            // A new pack must be produced: enforce the global cache limit first
            // (ADR D5). If only in-use packs remain and we are still over the
            // limit, abort this analysis (plan B).
            if (!CacheStore::instance().enforceLimit()) {
                error_ = "cache limit reached; remove offline packs or raise the limit";
                block_support_ = false;
            } else {
                // Remove any stale/incomplete pack now so poll() cannot briefly
                // read a torn pack from a previous run before the helper
                // truncates it.
                std::error_code rm;
                std::filesystem::remove(vpk_path_, rm);
                const std::string helper = helperExecutable();
                helper_ = std::make_unique<HelperProcess>();
                Status started = helper_->start(
                    helper, {es_path_, vpk_path_, std::to_string(fp.hash),
                             std::to_string(fp.size), std::to_string(fp.mtime)});
                if (!started.ok()) {
                    error_ = started.message();
                    block_support_ = false;
                    helper_.reset();
                } else {
                    helper_launched_ = true;
                    state_ = SessionState::Analyzing;
                }
            }
        }
    }

    if (!block_support_ && state_ == SessionState::Idle) {
        state_ = SessionState::Ready;  // thumbnail-only session
    }

    // Codecs whose reference helper is unavailable fall back to FFmpeg's
    // export_mvs motion-vector grid.
    mv_support_ = !block_support_ &&
                  (info.codec == CodecId::H264 || info.codec == CodecId::HEVC ||
                   info.codec == CodecId::VP9 || info.codec == CodecId::AV1);

    startThumbnailWorker();
    return Status::success();
}

void CodecSession::close() {
    // Set the stop flag while holding the mutex: otherwise a notify that races
    // with the worker evaluating its wait predicate can be lost, leaving the
    // worker asleep and this join() blocked forever (intermittent hang).
    {
        std::lock_guard<std::mutex> lock(thumb_mutex_);
        thumb_stop_ = true;
    }
    thumb_cv_.notify_all();
    if (thumb_thread_.joinable()) {
        thumb_thread_.join();
    }
    if (helper_) {
        helper_->terminate();
        helper_.reset();
    }
    reader_.reset();
    if (raw_) {
        raw_->close();
        raw_.reset();
    }
    media_.close();
    info_ = MediaInfo{};
    header_syntax_ = SyntaxTree{};
    header_syntax_loaded_ = false;
    if (!es_path_.empty() && es_extracted_) {
        std::error_code ec;
        std::filesystem::remove(es_path_, ec);
    }
    es_path_.clear();
    es_extracted_ = false;
    state_ = SessionState::Idle;
    frames_ready_ = 0;
    probed_count_ = -1;
    block_support_ = false;
    mv_support_ = false;
    helper_launched_ = false;
    display_to_pack_.clear();
    display_built_count_ = 0xFFFFFFFFu;
    {
        std::lock_guard<std::mutex> lock(thumb_mutex_);
        std::queue<uint32_t> empty;
        thumb_requests_.swap(empty);
        thumbnails_.clear();  // never leak thumbnails into the next file
        thumb_order_.clear();
        thumb_pending_.clear();
        thumb_failed_.clear();
        std::queue<uint32_t> empty_blocks;
        block_requests_.swap(empty_blocks);
        motion_blocks_.clear();
        block_order_.clear();
    }
    // The pack is no longer in use: allow capacity management to reclaim it.
    if (!fingerprint_name_.empty()) {
        const std::string pack = fingerprint_name_ + ".vpk";
        CacheStore::instance().markOffline(pack);
        if (!CacheStore::instance().enabled()) {
            // Caching disabled: do not leave the pack behind.
            CacheStore::instance().removePack(pack);
        }
        CacheStore::instance().enforceLimit();
    }
    fingerprint_name_.clear();
    vpk_path_.clear();
}

void CodecSession::poll() {
    if (helper_ && helper_->running()) {
        std::vector<HelperMessage> messages;
        helper_->poll(messages);
        for (const HelperMessage& message : messages) {
            switch (message.type) {
                case HelperMessageType::Hello:
                case HelperMessageType::Progress:
                case HelperMessageType::Ready:
                case HelperMessageType::Done:
                    break;
                case HelperMessageType::Error:
                    error_ = message.text;
                    state_ = SessionState::Failed;
                    break;
            }
        }
    }

    if (helper_launched_ && helper_ && helper_->running()) {
        // The pack is written incrementally; refresh a partial view so frames
        // become visible as soon as the helper flushes them (ADR D8).
        if (std::filesystem::exists(vpk_path_)) {
            if (reader_ == nullptr) {
                auto reader = std::make_unique<VpkReader>();
                if (reader->open(vpk_path_).ok()) {
                    reader_ = std::move(reader);
                }
            } else {
                reader_->refresh();
            }
            if (reader_ != nullptr) {
                frames_ready_ = std::max(frames_ready_, reader_->frameCount());
            }
        }
    }

    if (helper_launched_ && helper_ && !helper_->running()) {
        // Helper finished (normally or crashed). Any frames already flushed to
        // the pack stay usable even when the helper failed afterwards (E-5).
        if (reader_ == nullptr && std::filesystem::exists(vpk_path_)) {
            reader_ = std::make_unique<VpkReader>();
            if (reader_->open(vpk_path_).ok()) {
                frames_ready_ = reader_->frameCount();
            } else {
                reader_.reset();
            }
        } else if (reader_ != nullptr) {
            reader_->refresh();
            frames_ready_ = std::max(frames_ready_, reader_->frameCount());
        }

        if (state_ != SessionState::Failed) {
            if (reader_ != nullptr) {
                state_ = SessionState::Ready;
            } else if (frames_ready_ == 0 && helper_->exitCode() != 0) {
                state_ = SessionState::Failed;
                error_ = "reference helper failed (exit " +
                         std::to_string(helper_->exitCode()) + ")";
            } else {
                state_ = SessionState::Ready;  // thumbnails/syntax still usable
            }
        }
        // Graceful degradation: with no helper output, count frames with FFmpeg
        // so the thumbnail strip still has a length (R2/W-5).
        if (reader_ == nullptr && probed_count_ < 0) {
            probeFrameCount();
        }
        helper_launched_ = false;
    }
}

Result<DecodedImage> CodecSession::preview(uint32_t index) {
    if (raw_) {
        return raw_->decode(index);
    }
    std::lock_guard<std::mutex> lock(media_mutex_);
    return media_.decode(index);
}

Result<DecodedImage> CodecSession::reco(uint32_t index) {
    if (raw_) {
        return raw_->decode(index);
    }
    if (block_support_) {
        // Reference-decoded reconstruction lives in the pack.
        if (reader_ == nullptr) {
            return Status::error(StatusCode::Unsupported, "reco not ready yet");
        }
        return reader_->reco(packIndex(index));
    }
    // Codecs without a reference helper: fall back to the FFmpeg decode.
    std::lock_guard<std::mutex> lock(media_mutex_);
    return media_.decode(index);
}

int64_t CodecSession::frameCount() const {
    if (info_.frame_count > 0) {
        return info_.frame_count;
    }
    if (reader_ != nullptr) {
        const int64_t n = reader_->frameCount();
        if (n > 0) {
            return n;
        }
    }
    if (probed_count_ >= 0) {
        return probed_count_;
    }
    return 0;
}

int64_t CodecSession::probeFrameCount() {
    if (probed_count_ >= 0) {
        return probed_count_;
    }
    int64_t count = 0;
    {
        std::lock_guard<std::mutex> lock(media_mutex_);
        while (count < 1000000) {
            auto image = media_.decode(static_cast<uint32_t>(count));
            if (!image.ok()) {
                break;
            }
            ++count;
        }
        media_.close();
        media_.open(path_);  // reset the source for normal frame access
    }
    probed_count_ = count;
    return count;
}

Result<DecodedImage> CodecSession::pred(uint32_t index) {
    if (reader_ == nullptr) {
        return Status::error(StatusCode::Unsupported, "pred not available");
    }
    return reader_->pred(packIndex(index));
}

Result<DecodedImage> CodecSession::resi(uint32_t index) {
    if (reader_ == nullptr) {
        return Status::error(StatusCode::Unsupported, "resi not available");
    }
    return reader_->resi(packIndex(index));
}

Result<BlockGrid> CodecSession::blocks(uint32_t index) {
    const uint32_t pack = packIndex(index);
    if (reader_ != nullptr) {
        auto result = reader_->blocks(pack);
        if (result.ok()) {
            return result;
        }
    }
    if (raw_) {
        return Status::error(StatusCode::Unsupported, "raw video has no block info");
    }
    if (!mv_support_) {
        return Status::error(StatusCode::Unsupported, "no block info for this codec");
    }
    // Fallback: FFmpeg export_mvs motion vectors, produced by the worker.
    BlockGrid grid;
    if (copyMotionBlocks(pack, grid)) {
        return grid;
    }
    requestMotionBlocks(pack);
    return Status::error(StatusCode::Unsupported, "motion vectors not ready yet");
}

Result<FrameMeta> CodecSession::frameMeta(uint32_t index) {
    if (reader_ != nullptr) {
        auto meta = reader_->frameMeta(packIndex(index));
        if (meta.ok()) {
            // Fill in the frame type when the pack predates the field: derive it
            // from the key-frame flag and block content.
            if (meta.value().frame_type == FrameType::Unknown) {
                auto grid = reader_->blocks(packIndex(index));
                if (grid.ok()) {
                    bool has_inter = false;
                    bool has_bi = false;
                    for (const Block& b : grid.value().blocks) {
                        if (b.type == BlockType::Inter || b.type == BlockType::Skip) {
                            has_inter = true;
                            if (b.ref_idx[1] >= 0) {
                                has_bi = true;
                            }
                        }
                    }
                    meta.value().frame_type =
                        meta.value().is_keyframe
                            ? FrameType::Key
                            : (!has_inter ? FrameType::I : (has_bi ? FrameType::B : FrameType::P));
                } else {
                    meta.value().frame_type =
                        meta.value().is_keyframe ? FrameType::Key : FrameType::Unknown;
                }
            }
            return meta;
        }
    }
    return Status::error(StatusCode::Unsupported, "no frame metadata");
}

}  // namespace vide
