#pragma once

#include <cstdint>

#include "common/types.h"
#include "ui/panel.h"

namespace vide {

class SyntaxPanel final : public IPanel {
public:
    const char* name() const override { return "Syntax"; }
    void draw(Workspace& workspace) override;

private:
    uint64_t last_generation_ = 0;
    uint32_t last_selection_ = kInvalidId;
    bool scroll_pending_ = false;
    int active_section_ = 0;
};

}  // namespace vide
