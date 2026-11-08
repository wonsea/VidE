// CTU container: the magnified CTU image on top and the block info below, shown
// as a single dockable window so the right column can tab it with Bitstream.
#pragma once

#include "ui/block_info_panel.h"
#include "ui/ctu_view_panel.h"
#include "ui/panel.h"

namespace vide {

class Renderer;

class CtuPanel final : public IPanel {
public:
    explicit CtuPanel(Renderer& renderer) : view_(renderer) {}
    const char* name() const override { return "CTU"; }
    void draw(Workspace& workspace) override;

private:
    CtuViewPanel view_;
    BlockInfoPanel info_;
};

}  // namespace vide
