#pragma once

#include <cstdint>

#include "ui/panel.h"

namespace vide {

class Renderer;

class ThumbnailPanel final : public IPanel {
public:
    explicit ThumbnailPanel(Renderer& renderer) : renderer_(&renderer) {}

    const char* name() const override { return "Thumbnails"; }
    void draw(Workspace& workspace) override;

private:
    Renderer* renderer_;
    int last_count_ = 0;
};

}  // namespace vide
