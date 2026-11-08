// Programmatic construction of the default dockspace layout.
#pragma once

#include <imgui.h>

namespace vide {

// Builds the default analyzer layout (toolbar / syntax / picture / blocks /
// thumbnails) the first time it is called for `dockspace_id`.
void buildDefaultLayout(ImGuiID dockspace_id);

}  // namespace vide
