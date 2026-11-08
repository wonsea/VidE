// GPU-side picture storage.
//
// Owns the OpenGL textures for the main picture and for the thumbnail strip.
// Pixel-format conversion happens in the codec adapters, so the renderer only
// ever uploads RGBA8888 data.
#pragma once

#include <cstdint>
#include <unordered_map>

#include "common/frame.h"

namespace vide {

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // ---- Main picture ------------------------------------------------------
    void setImage(const DecodedImage& image);
    void clearImage();
    bool hasImage() const { return texture_ != 0; }
    uint32_t textureId() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }

    // ---- Thumbnails --------------------------------------------------------
    // Keyed by (session generation << 32 | frame index) so multiple open
    // streams can show thumbnails at once without colliding.
    void setThumbnail(uint64_t key, const DecodedImage& image);
    uint32_t thumbnailTexture(uint64_t key) const;
    bool hasThumbnail(uint64_t key) const;

    // Drops every thumbnail texture (e.g. when a different file is opened).
    void clearThumbnails();

    // Releases every GPU resource. Must be called while the GL context is
    // current.
    void clear();

private:
    void uploadRgba(const DecodedImage& image);
    uint32_t createTexture(const DecodedImage& image);

    uint32_t texture_ = 0;
    int width_ = 0;
    int height_ = 0;

    std::unordered_map<uint64_t, uint32_t> thumbnails_;
};

}  // namespace vide
