#pragma once

#include "ui/panel.h"

namespace vide {

class Renderer;

// Magnified view of the currently selected CTU, with every CU outlined.
class CtuViewPanel final : public IPanel {
public:
    explicit CtuViewPanel(Renderer& renderer) : renderer_(&renderer) {}
    const char* name() const override { return "CTU View"; }
    void draw(Workspace& workspace) override;
    // Draws the content without its own window (used inside the CTU container).
    void drawBody(Workspace& workspace);

private:
    Renderer* renderer_;
};

}  // namespace vide
