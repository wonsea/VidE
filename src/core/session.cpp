#include "core/session.h"

#include <atomic>
#include <utility>

namespace vide {
namespace {

// Process-wide so generations stay unique across streams and switches.
uint64_t nextGeneration() {
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1);
}

}  // namespace

Session::Session() : generation_(nextGeneration()) {}
Session::~Session() = default;

Status Session::open(const std::string& path, const OpenOptions& options) {
    close();
    generation_ = nextGeneration();
    codec_ = std::make_unique<CodecSession>();
    Status status = codec_->open(path, options);
    if (!status.ok()) {
        status_message = "Open failed: " + status.message();
        codec_.reset();
        return status;
    }
    selection_ = Selection{};
    selection_.frame = 0;
    auto_selected_frame_ = kInvalidId;
    frame_ready_for_ = kInvalidId;
    zoom = 1.0f;
    invalidateFrameCaches();
    status_message = "Opened " + path;
    return Status::success();
}

void Session::close() {
    if (codec_) {
        codec_->close();
        codec_.reset();
    }
    selection_ = Selection{};
    auto_selected_frame_ = kInvalidId;
    frame_ready_for_ = kInvalidId;
    invalidateFrameCaches();
    generation_ = nextGeneration();
}

const std::string& Session::path() const {
    static const std::string empty;
    return codec_ ? codec_->path() : empty;
}

void Session::invalidateFrameCaches() {
    reco_valid_ = false;
    reco_frame_ = kInvalidId;
    reco_ = DecodedImage{};
    blocks_valid_ = false;
    blocks_frame_ = kInvalidId;
    blocks_ = BlockGrid{};
    syntax_valid_ = false;
    syntax_frame_ = kInvalidId;
    syntax_ = SyntaxTree{};
    image_valid_ = false;
    image_frame_ = kInvalidId;
    image_ = DecodedImage{};
}

void Session::setImageMode(ImageMode mode) {
    if (mode != image_mode_) {
        image_mode_ = mode;
        image_valid_ = false;
        image_frame_ = kInvalidId;
        image_ = DecodedImage{};
    }
}

const DecodedImage* Session::currentImage() {
    if (!codec_ || selection_.frame == kInvalidId) {
        return nullptr;
    }
    if (image_valid_ && image_frame_ == selection_.frame && image_cached_mode_ == image_mode_) {
        return &image_;
    }
    Result<DecodedImage> result =
        (image_mode_ == ImageMode::Pred)
            ? codec_->pred(selection_.frame)
            : (image_mode_ == ImageMode::Resi) ? codec_->resi(selection_.frame)
                                               : codec_->reco(selection_.frame);
    if (!result.ok()) {
        return nullptr;  // not ready / unavailable for this mode
    }
    image_ = std::move(result.value());
    image_frame_ = selection_.frame;
    image_cached_mode_ = image_mode_;
    image_valid_ = true;
    ++image_revision_;
    return &image_;
}

const MediaInfo& Session::info() const {
    static const MediaInfo empty{};
    return codec_ ? codec_->info() : empty;
}

int64_t Session::frameCount() const {
    if (!codec_) {
        return 0;
    }
    return codec_->frameCount();
}

double Session::frameRate() const {
    const MediaInfo& in = info();
    if (in.fps_num > 0 && in.fps_den > 0) {
        return static_cast<double>(in.fps_num) / in.fps_den;
    }
    return 25.0;
}

void Session::gotoFrame(uint32_t index) {
    if (!codec_) {
        return;
    }
    const int64_t count = frameCount();
    int64_t target = static_cast<int64_t>(index);
    if (target < 0) {
        target = 0;
    }
    if (count > 0 && target >= count) {
        target = count - 1;
    }
    if (static_cast<uint32_t>(target) != selection_.frame) {
        selection_.frame = static_cast<uint32_t>(target);
        selection_.block = kInvalidId;
        frame_ready_for_ = kInvalidId;
        invalidateFrameCaches();
    }
}

void Session::poll() {
    if (codec_) {
        codec_->poll();
        // When the selected frame's data first becomes available (helper finished
        // flushing it, or the cache was populated), drop the per-frame caches so
        // the panels re-fetch the now-complete syntax / blocks / image.
        if (frame_ready_for_ != selection_.frame &&
            static_cast<int64_t>(selection_.frame) < codec_->framesReady()) {
            invalidateFrameCaches();
            frame_ready_for_ = selection_.frame;
        }
        ensureDefaultSelection();
    }
}

void Session::ensureDefaultSelection() {
    if (!codec_ || selection_.frame == kInvalidId) {
        return;
    }
    if (auto_selected_frame_ == selection_.frame) {
        return;  // already defaulted this frame (or user changed it)
    }
    const BlockGrid* grid = currentBlocks();
    if (grid == nullptr || grid->blocks.empty()) {
        return;  // not ready yet; retry on a later poll
    }
    const Block* tl = nullptr;
    for (const Block& block : grid->blocks) {
        if (block.rect.x == 0 && block.rect.y == 0) {
            if (tl == nullptr || block.rect.w * block.rect.h < tl->rect.w * tl->rect.h) {
                tl = &block;
            }
        }
    }
    if (tl == nullptr) {
        tl = &grid->blocks.front();
    }
    const int ctu = grid->ctu_size > 0 ? grid->ctu_size : 64;
    const int cols = (grid->width + ctu - 1) / ctu;
    selection_.block = tl->id;
    selection_.ctu = static_cast<uint32_t>((tl->rect.y / ctu) * cols + (tl->rect.x / ctu));
    const SyntaxTree* syntax = currentSyntax();
    if (syntax != nullptr) {
        for (const SyntaxNode& node : syntax->nodes) {
            if (node.block_id == tl->id) {
                selection_.syntax_node = node.id;
                break;
            }
        }
    }
    auto_selected_frame_ = selection_.frame;
}

const DecodedImage* Session::currentReco() {
    if (!codec_ || selection_.frame == kInvalidId) {
        return nullptr;
    }
    if (reco_valid_ && reco_frame_ == selection_.frame) {
        return &reco_;
    }
    auto result = codec_->reco(selection_.frame);
    if (!result.ok()) {
        return nullptr;  // not ready yet
    }
    reco_ = std::move(result.value());
    reco_frame_ = selection_.frame;
    reco_valid_ = true;
    ++reco_revision_;
    return &reco_;
}

const BlockGrid* Session::currentBlocks() {
    if (!codec_ || selection_.frame == kInvalidId) {
        return nullptr;
    }
    if (blocks_valid_ && blocks_frame_ == selection_.frame) {
        return &blocks_;
    }
    auto result = codec_->blocks(selection_.frame);
    if (!result.ok()) {
        return nullptr;
    }
    blocks_ = std::move(result.value());
    blocks_frame_ = selection_.frame;
    blocks_valid_ = true;
    return &blocks_;
}

const SyntaxTree* Session::currentSyntax() {
    if (!codec_ || selection_.frame == kInvalidId) {
        return nullptr;
    }
    if (syntax_valid_ && syntax_frame_ == selection_.frame) {
        return &syntax_;
    }
    auto result = codec_->syntax(selection_.frame);
    if (!result.ok()) {
        return nullptr;
    }
    syntax_ = std::move(result.value());
    syntax_frame_ = selection_.frame;
    syntax_valid_ = true;
    return &syntax_;
}

}  // namespace vide
