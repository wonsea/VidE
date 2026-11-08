#pragma once

#include <cstdint>

#include "common/types.h"
#include "ui/panel.h"

namespace vide {

class Renderer;

class ImagePanel final : public IPanel {
public:
    explicit ImagePanel(Renderer& renderer) : renderer_(&renderer) {}

    const char* name() const override { return "Picture"; }
    void draw(Workspace& workspace) override;

private:
    Renderer* renderer_;
    uint64_t uploaded_revision_ = 0;
    uint64_t last_generation_ = 0;
    uint32_t last_frame_ = kInvalidId;  // for auto-selecting the top-left CU
    bool has_uploaded_ = false;
    bool fit_ = true;  // true: fit window, false: original size
};

}  // namespace vide
