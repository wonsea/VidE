// CodecSession: asynchronous thumbnail + FFmpeg motion-vector fallback worker.
// MediaSource is not thread-safe with the main thread, so all source access
// happens on this single worker. Split out of codec_session.cpp.
#include "core/codec_session.h"

#include <mutex>
#include <utility>

namespace vide {

Result<DecodedImage> CodecSession::thumbnailFor(uint32_t index, int max_w, int max_h) {
    if (raw_) {
        return raw_->thumbnail(index, max_w, max_h);
    }
    std::lock_guard<std::mutex> lock(media_mutex_);
    return media_.thumbnail(index, max_w, max_h);
}

void CodecSession::startThumbnailWorker() {
    thumb_stop_ = false;
    thumb_thread_ = std::thread([this] { thumbnailWorkerLoop(); });
}

void CodecSession::requestThumbnail(uint32_t index) {
    {
        std::lock_guard<std::mutex> lock(thumb_mutex_);
        if (thumbnails_.count(index) != 0 || thumb_pending_.count(index) != 0 ||
            thumb_failed_.count(index) != 0) {
            return;  // already ready, queued/in-flight, or known-bad
        }
        thumb_requests_.push(index);
        thumb_pending_[index] = true;
    }
    thumb_cv_.notify_one();
}

void CodecSession::requestMotionBlocks(uint32_t index) {
    {
        std::lock_guard<std::mutex> lock(thumb_mutex_);
        if (motion_blocks_.count(index) != 0) {
            return;
        }
        block_requests_.push(index);
    }
    thumb_cv_.notify_one();
}

bool CodecSession::copyMotionBlocks(uint32_t index, BlockGrid& out) {
    std::lock_guard<std::mutex> lock(thumb_mutex_);
    auto it = motion_blocks_.find(index);
    if (it == motion_blocks_.end()) {
        return false;
    }
    // Refresh LRU position.
    for (auto o = block_order_.begin(); o != block_order_.end(); ++o) {
        if (*o == index) {
            block_order_.erase(o);
            break;
        }
    }
    block_order_.push_front(index);
    out = it->second;
    return true;
}

bool CodecSession::copyThumbnail(uint32_t index, DecodedImage& out) {
    std::lock_guard<std::mutex> lock(thumb_mutex_);
    auto it = thumbnails_.find(index);
    if (it == thumbnails_.end()) {
        return false;
    }
    // Refresh LRU position.
    for (auto o = thumb_order_.begin(); o != thumb_order_.end(); ++o) {
        if (*o == index) {
            thumb_order_.erase(o);
            break;
        }
    }
    thumb_order_.push_front(index);
    out = it->second;  // copy so callers never touch worker storage
    return true;
}

void CodecSession::thumbnailWorkerLoop() {
    while (!thumb_stop_) {
        uint32_t thumb_index = 0;
        uint32_t block_index = 0;
        bool have_thumb = false;
        bool have_block = false;
        {
            std::unique_lock<std::mutex> lock(thumb_mutex_);
            thumb_cv_.wait(lock, [this] {
                return thumb_stop_ || !thumb_requests_.empty() || !block_requests_.empty();
            });
            if (thumb_stop_) {
                break;
            }
            if (!thumb_requests_.empty()) {
                thumb_index = thumb_requests_.front();
                thumb_requests_.pop();
                have_thumb = true;
            }
            if (!block_requests_.empty()) {
                if (mv_support_) {
                    block_index = block_requests_.front();
                    block_requests_.pop();
                    have_block = true;
                } else {
                    // Helper-backed codecs produce block info in the pack; drop
                    // spurious MV requests.
                    std::queue<uint32_t> empty;
                    block_requests_.swap(empty);
                }
            }
        }

        // Note: the source (FFmpeg / raw) is not thread-safe with the main
        // thread; all MediaSource access happens on this worker exclusively.
        if (have_thumb) {
            auto image = thumbnailFor(thumb_index, 160, 90);
            std::lock_guard<std::mutex> lock(thumb_mutex_);
            thumb_pending_.erase(thumb_index);
            if (image.ok()) {
                auto inserted = thumbnails_.emplace(thumb_index, std::move(image.value()));
                if (inserted.second) {
                    thumb_order_.push_front(thumb_index);
                }
                while (thumbnails_.size() > kMaxThumbnails && !thumb_order_.empty()) {
                    const uint32_t victim = thumb_order_.back();
                    thumb_order_.pop_back();
                    thumbnails_.erase(victim);
                }
            } else {
                // Remember the failure so the UI does not re-queue it every frame
                // (which would starve later frames in the FIFO).
                thumb_failed_[thumb_index] = true;
            }
        }
        if (have_block) {
            BlockGrid grid;
            {
                std::lock_guard<std::mutex> lock(media_mutex_);
                auto g = media_.motionVectors(block_index);
                if (g.ok()) {
                    grid = std::move(g.value());
                }
            }
            if (!grid.blocks.empty()) {
                std::lock_guard<std::mutex> lock(thumb_mutex_);
                auto inserted = motion_blocks_.emplace(block_index, std::move(grid));
                if (inserted.second) {
                    block_order_.push_front(block_index);
                }
                while (motion_blocks_.size() > kMaxThumbnails && !block_order_.empty()) {
                    const uint32_t victim = block_order_.back();
                    block_order_.pop_back();
                    motion_blocks_.erase(victim);
                }
            }
        }
    }
}

}  // namespace vide
