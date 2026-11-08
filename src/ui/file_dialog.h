// Native "open file" dialog wrapper.
//
// Windows uses the common dialog; other platforms currently return an empty
// string (the Open dialog still accepts a typed path).
#pragma once

#include <string>

namespace vide {

// Shows a modal open-file dialog. Returns the chosen path, or an empty string
// when the user cancels. `filter` is a human-readable description used on
// platforms that support it (e.g. "Media files").
std::string openFileDialog(const std::string& title, const std::string& filter);

}  // namespace vide
