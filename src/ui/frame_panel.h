// Frame-level panel: current frame identity and its DPB / reference lists.
#pragma once

#include "ui/panel.h"

namespace vide {

class FramePanel final : public IPanel {
public:
    const char* name() const override { return "Frame"; }
    void draw(Workspace& workspace) override;
};

}  // namespace vide
