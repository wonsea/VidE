#pragma once

#include <string>

#include "ui/panel.h"

namespace vide {

class ToolbarPanel final : public IPanel {
public:
    const char* name() const override { return "Toolbar"; }
    void draw(Workspace& workspace) override;

private:
    void drawOpenPopup(Workspace& workspace);
    void drawStreamList(Workspace& workspace);
    void drawCachePopup(Workspace& workspace);

    char path_buffer_[512] = {};
    bool popup_requested_ = false;
    std::string cache_filter_;

    // Raw-video open options (header-less YUV/RGB inputs).
    bool raw_enabled_ = false;
    int raw_format_index_ = 0;
    int raw_width_ = 0;
    int raw_height_ = 0;
};

}  // namespace vide
