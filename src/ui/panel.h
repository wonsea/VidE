// Base interface for every dockable UI panel.
#pragma once

namespace vide {

class Workspace;

class IPanel {
public:
    virtual ~IPanel() = default;

    // Must be unique and stable: it is used as the docking window name.
    virtual const char* name() const = 0;

    virtual void draw(Workspace& workspace) = 0;
};

}  // namespace vide
