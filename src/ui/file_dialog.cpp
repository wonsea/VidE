#include "ui/file_dialog.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace vide {

#ifdef _WIN32

std::string openFileDialog(const std::string& title, const std::string& filter) {
    // Build a double-NUL-terminated filter string:
    //   "Media files\0*.mp4;*.265;...\0All files\0*.*\0\0"
    const std::string description = filter.empty() ? "Media files" : filter;
    std::string filter_spec;
    filter_spec += description;
    filter_spec.push_back('\0');
    filter_spec += "*.mp4;*.mkv;*.mov;*.ts;*.m2ts;*.webm;*.ivf;*.h264;*.264;*.h265;*.265;"
                   "*.h266;*.266;*.vvc;*.av1;*.obu;*.yuv;*.y4m;*.rgb;*.jpg;*.jpeg;*.png";
    filter_spec.push_back('\0');
    filter_spec += "All files";
    filter_spec.push_back('\0');
    filter_spec += "*.*";
    filter_spec.push_back('\0');
    filter_spec.push_back('\0');

    char buffer[MAX_PATH] = {0};

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter_spec.c_str();
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(buffer));
    ofn.lpstrTitle = title.empty() ? nullptr : title.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(buffer);
    }
    return {};
}

#else

std::string openFileDialog(const std::string&, const std::string&) { return {}; }

#endif

}  // namespace vide
