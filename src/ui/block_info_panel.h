#pragma once

#include <cstdint>
#include <vector>

#include "common/frame.h"
#include "common/types.h"
#include "ui/panel.h"

namespace vide {

class BlockInfoPanel final : public IPanel {
public:
    const char* name() const override { return "Block Info"; }
    void draw(Workspace& workspace) override;
    // Draws the content without its own window (used inside the CTU container).
    void drawBody(Workspace& workspace);

private:
    void drawBlockTable(const Block& block, const std::vector<int>& ref0_poc,
                        const std::vector<int>& ref1_poc);

    uint32_t last_block_ = kInvalidId;
    bool scroll_pending_ = false;
};

}  // namespace vide
