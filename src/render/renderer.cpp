#include "render/renderer.h"

#include <GLFW/glfw3.h>

// Some MinGW OpenGL headers only expose the GL 1.1 constants; provide the
// handful of GL 1.2+ tokens used here.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

namespace vide {

Renderer::~Renderer() {
    // Note: the GL context is expected to be current. Callers shut the renderer
    // down explicitly before destroying the window.
}

void Renderer::uploadRgba(const DecodedImage& image) {
    if (texture_ == 0) {
        glGenTextures(1, &texture_);
    }

    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);

    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image.data.data());

    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));

    width_ = image.width;
    height_ = image.height;
}

uint32_t Renderer::createTexture(const DecodedImage& image) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image.data.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

void Renderer::setImage(const DecodedImage& image) {
    if (!image.valid()) {
        return;
    }
    if (image.format != PixelFormat::RGBA8888) {
        return;  // adapters are expected to hand over RGBA
    }
    uploadRgba(image);
}

void Renderer::clearImage() {
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    width_ = 0;
    height_ = 0;
}

void Renderer::setThumbnail(uint64_t key, const DecodedImage& image) {
    if (!image.valid() || image.format != PixelFormat::RGBA8888) {
        return;
    }
    auto found = thumbnails_.find(key);
    if (found != thumbnails_.end()) {
        glDeleteTextures(1, &found->second);
        thumbnails_.erase(found);
    }
    thumbnails_[key] = createTexture(image);
}

uint32_t Renderer::thumbnailTexture(uint64_t key) const {
    auto found = thumbnails_.find(key);
    return found != thumbnails_.end() ? found->second : 0;
}

bool Renderer::hasThumbnail(uint64_t key) const {
    return thumbnails_.find(key) != thumbnails_.end();
}

void Renderer::clearThumbnails() {
    for (auto& entry : thumbnails_) {
        GLuint id = entry.second;
        glDeleteTextures(1, &id);
    }
    thumbnails_.clear();
}

void Renderer::clear() {
    clearImage();
    clearThumbnails();
}

}  // namespace vide
