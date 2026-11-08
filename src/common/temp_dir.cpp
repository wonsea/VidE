#include "common/temp_dir.h"

#include <cstdlib>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace vide {
namespace {

bool ensureDirectory(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        return false;
    }
    return std::filesystem::is_directory(path, ec);
}

}  // namespace

std::filesystem::path tempDirectory() {
    if (const char* env = std::getenv("VIDE_TMP_DIR"); env != nullptr && env[0] != '\0') {
        std::filesystem::path path(env);
        if (ensureDirectory(path)) {
            return path;
        }
    }

#ifdef VIDE_DEFAULT_TMP_DIR
    {
        std::filesystem::path path(VIDE_DEFAULT_TMP_DIR);
        if (ensureDirectory(path)) {
            return path;
        }
    }
#endif

    std::error_code ec;
    std::filesystem::path fallback = std::filesystem::temp_directory_path(ec) / "vide";
    ensureDirectory(fallback);
    return fallback;
}

std::filesystem::path executableDirectory() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buf).parent_path();
#elif defined(__linux__)
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return {};
    }
    return exe.parent_path();
#else
    return {};
#endif
}

}  // namespace vide
