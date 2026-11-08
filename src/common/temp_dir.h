// Central helper for VidE's temporary files.
//
// Per project policy all scratch data lives under `VidE/tmp`. The directory is
// resolved at runtime as:
//   1. $VIDE_TMP_DIR when set,
//   2. the build-time default (<repo>/tmp) when writable,
//   3. the system temp directory as a last resort.
#pragma once

#include <filesystem>

namespace vide {

// Returns (and creates) a writable temporary directory for VidE.
std::filesystem::path tempDirectory();

// Directory containing the running executable (for portable, next-to-binary
// caches). Empty when it cannot be determined.
std::filesystem::path executableDirectory();

}  // namespace vide
