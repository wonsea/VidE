// Session is the UI-facing facade over a CodecSession. Panels read/mutate the
// Session; it never owns analysis data beyond transient UI state.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "codec/adapter.h"
#include "common/frame.h"
#include "common/status.h"
#include "common/syntax.h"
#include "core/codec_session.h"

namespace vide {

struct Selection {
    uint32_t frame = kInvalidId;
    uint32_t syntax_node = kInvalidId;
    uint32_t block = kInvalidId;
    uint32_t ctu = kInvalidId;  // CTU index containing `block` (if any)
};

struct OverlaySettings {
    bool block_borders = false;
    bool block_type = false;
    bool qp_heatmap = false;
    bool bits_heatmap = false;
    bool motion_vectors = false;
};

// Which plane is shown in the central picture panel (R3).
enum class ImageMode {
    Reco = 0,
    Pred,
    Resi,
};

class Session {
public:
    Session();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Status open(const std::string& path, const OpenOptions& options = {});
    void close();

    CodecSession* codec() { return codec_.get(); }
    const CodecSession* codec() const { return codec_.get(); }

    Selection& selection() { return selection_; }
    const Selection& selection() const { return selection_; }

    OverlaySettings& overlay() { return overlay_; }
    const OverlaySettings& overlay() const { return overlay_; }

    bool hasDocument() const { return codec_ != nullptr; }

    // Source path of the opened stream (empty when nothing is open).
    const std::string& path() const;

    // Globally-unique, bumped on every open/close so panels can drop per-file
    // GPU/UI state (also changes when switching between open streams).
    uint64_t generation() const { return generation_; }

    const MediaInfo& info() const;
    int64_t frameCount() const;
    double frameRate() const;

    void gotoFrame(uint32_t index);

    // Advances helper progress / thumbnails. Called each UI frame.
    void poll();

    ImageMode imageMode() const { return image_mode_; }
    void setImageMode(ImageMode mode);
    // Currently selected frame's image for the active mode, if ready.
    const DecodedImage* currentImage();
    uint64_t imageRevision() const { return image_revision_; }

    // Currently selected frame's reco image (from cache) if ready.
    const DecodedImage* currentReco();
    uint64_t recoRevision() const { return reco_revision_; }

    // Blocks / syntax for the selected frame (cached per selection).
    const BlockGrid* currentBlocks();
    const SyntaxTree* currentSyntax();

    // Transient UI state.
    float zoom = 1.0f;
    std::string status_message = "Ready - open a bitstream to begin";
    void setStatus(std::string message) { status_message = std::move(message); }

private:
    void invalidateFrameCaches();
    // Selects the top-left CU of the current frame once blocks are available
    // (runs from poll(), so it does not depend on the Picture panel being shown).
    void ensureDefaultSelection();

    std::unique_ptr<CodecSession> codec_;
    Selection selection_;
    OverlaySettings overlay_;
    uint64_t generation_ = 1;
    uint32_t auto_selected_frame_ = kInvalidId;
    // Frame whose data first became ready (so poll() invalidates the per-frame
    // caches exactly once when the helper produces the selected frame).
    uint32_t frame_ready_for_ = kInvalidId;

    DecodedImage reco_;
    uint32_t reco_frame_ = kInvalidId;
    bool reco_valid_ = false;
    uint64_t reco_revision_ = 0;

    DecodedImage image_;
    uint32_t image_frame_ = kInvalidId;
    bool image_valid_ = false;
    ImageMode image_mode_ = ImageMode::Reco;
    ImageMode image_cached_mode_ = ImageMode::Reco;
    uint64_t image_revision_ = 0;

    BlockGrid blocks_;
    uint32_t blocks_frame_ = kInvalidId;
    bool blocks_valid_ = false;

    SyntaxTree syntax_;
    uint32_t syntax_frame_ = kInvalidId;
    bool syntax_valid_ = false;
};

}  // namespace vide
