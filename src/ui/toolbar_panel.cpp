#include "ui/toolbar_panel.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "core/cache_store.h"
#include "core/session.h"
#include "core/workspace.h"
#include "ui/file_dialog.h"

namespace vide {
namespace {

struct RawFormatEntry {
    const char* name;
    PixelFormat format;
};

constexpr RawFormatEntry kRawFormats[] = {
    {"I420", PixelFormat::I420},     {"NV12", PixelFormat::NV12},
    {"NV21", PixelFormat::NV21},     {"YUV422P", PixelFormat::YUV422P},
    {"YUV400", PixelFormat::YUV400}, {"YUV444P", PixelFormat::YUV444P},
    {"P010", PixelFormat::P010},     {"P012", PixelFormat::P012},
    {"P016", PixelFormat::P016},     {"RGB888", PixelFormat::RGB888},
};

}  // namespace

void ToolbarPanel::draw(Workspace& workspace) {
    if (!ImGui::Begin(name())) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Add...")) {
        popup_requested_ = true;
    }
    ImGui::SameLine();

    // Remove: pop up a menu of every open stream; clicking one removes it.
    if (ImGui::Button("Remove...")) {
        ImGui::OpenPopup("Remove stream");
    }
    if (ImGui::BeginPopup("Remove stream")) {
        if (workspace.empty()) {
            ImGui::TextDisabled("(no streams)");
        }
        for (int i = 0; i < workspace.count(); ++i) {
            Session* candidate = workspace.at(i);
            if (candidate == nullptr) {
                continue;
            }
            const std::string label =
                std::to_string(i + 1) + ": " +
                std::filesystem::path(candidate->path()).filename().string();
            if (ImGui::MenuItem(label.c_str())) {
                workspace.removeAt(i);
                break;  // the list changed underneath us
            }
        }
        ImGui::EndPopup();
    }

    // Cache manager lives on the first row so it is always visible (the toolbar
    // dock is short; a bottom section would be clipped).
    ImGui::SameLine();
    if (ImGui::Button("Cache manager...")) {
        ImGui::OpenPopup("Cache manager");
    }
    drawCachePopup(workspace);

    // Cache on/off (default on): when disabled, every open re-runs the helper
    // and the produced pack is removed on close.
    ImGui::SameLine();
    bool cache_on = CacheStore::instance().enabled();
    if (ImGui::Checkbox("Cache##cache_enabled", &cache_on)) {
        CacheStore::instance().setEnabled(cache_on);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Use the content-addressed cache (fast reuse on reopen).\n"
                          "When off, every open re-runs the reference helper.");
    }

    const bool has_document = workspace.active().hasDocument();
    Session& session = workspace.active();
    const int64_t frame_count = session.frameCount();

    drawStreamList(workspace);

    ImGui::SameLine();
    ImGui::Text("Frame: %u", session.selection().frame);
    if (frame_count > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("/ %lld", static_cast<long long>(frame_count));
    }

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::TextUnformatted("Overlays:");
    ImGui::SameLine();
    ImGui::Checkbox("Borders", &session.overlay().block_borders);
    ImGui::SameLine();
    ImGui::Checkbox("Type", &session.overlay().block_type);
    ImGui::SameLine();
    ImGui::Checkbox("QP", &session.overlay().qp_heatmap);
    ImGui::SameLine();
    ImGui::Checkbox("Bits", &session.overlay().bits_heatmap);
    ImGui::SameLine();
    ImGui::Checkbox("MV", &session.overlay().motion_vectors);

    ImGui::Separator();

    if (has_document) {
        const MediaInfo& info = session.info();
        const CodecSession* codec = session.codec();
        const char* state = "?";
        if (codec != nullptr) {
            switch (codec->state()) {
                case SessionState::Idle: state = "idle"; break;
                case SessionState::Probing: state = "probing"; break;
                case SessionState::Analyzing: state = "analysing"; break;
                case SessionState::Ready: state = "ready"; break;
                case SessionState::Failed: state = "failed"; break;
            }
        }
        ImGui::Text("Codec: %s  |  %dx%d  |  %s  |  %s  |  %.3g fps  |  %s  |  %s",
                    toString(info.codec), info.width, info.height, toString(info.format),
                    toString(info.bit_depth), session.frameRate(),
                    info.container.empty() ? "?" : info.container.c_str(), state);
        if (codec != nullptr && !codec->error().empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.3f, 1.0f));
            ImGui::TextWrapped("%s", codec->error().c_str());
            ImGui::PopStyleColor();
        }
    } else {
        ImGui::TextUnformatted("No bitstream loaded");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("| %s", session.status_message.c_str());

    drawOpenPopup(workspace);

    ImGui::End();
}

void ToolbarPanel::drawStreamList(Workspace& workspace) {
    ImGui::TextUnformatted("Streams:");
    ImGui::SameLine();

    // Compact selector; a label is built from the file name + analysis state.
    const std::string current =
        workspace.empty()
            ? std::string("(none)")
            : (std::filesystem::path(workspace.active().path()).filename().string() + "  [" +
               std::to_string(workspace.activeIndex() + 1) + "/" +
               std::to_string(workspace.count()) + "]");
    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::BeginCombo("##streams", current.c_str())) {
        for (int i = 0; i < workspace.count(); ++i) {
            const Session* s = workspace.at(i);
            if (s == nullptr) {
                continue;
            }
            const bool selected = (i == workspace.activeIndex());
            const std::string name =
                std::filesystem::path(s->path()).filename().string();
            if (ImGui::Selectable((name + "##" + std::to_string(i)).c_str(), selected)) {
                workspace.setActive(i);
            }
        }
        ImGui::EndCombo();
    }
}

void ToolbarPanel::drawOpenPopup(Workspace& workspace) {
    if (popup_requested_) {
        ImGui::OpenPopup("Add bitstream");
        popup_requested_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Add bitstream", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Input file path:");
        ImGui::SetNextItemWidth(420.0f);
        bool submitted = ImGui::InputText("##path", path_buffer_, IM_ARRAYSIZE(path_buffer_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(90.0f, 0.0f))) {
            const std::string chosen = openFileDialog("Add bitstream", "Media files");
            if (!chosen.empty()) {
                std::snprintf(path_buffer_, sizeof(path_buffer_), "%s", chosen.c_str());
            }
        }

        ImGui::Checkbox("Raw video (YUV/RGB)", &raw_enabled_);
        if (raw_enabled_) {
            const int format_count = IM_ARRAYSIZE(kRawFormats);
            if (raw_format_index_ < 0 || raw_format_index_ >= format_count) {
                raw_format_index_ = 0;
            }
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::BeginCombo("Format", kRawFormats[raw_format_index_].name)) {
                for (int i = 0; i < format_count; ++i) {
                    const bool selected = (i == raw_format_index_);
                    if (ImGui::Selectable(kRawFormats[i].name, selected)) {
                        raw_format_index_ = i;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputInt("Width", &raw_width_);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputInt("Height", &raw_height_);
        }

        if (ImGui::Button("Add", ImVec2(120.0f, 0.0f)) || submitted) {
            if (path_buffer_[0] != '\0') {
                OpenOptions options;
                if (raw_enabled_) {
                    options.raw_format = kRawFormats[raw_format_index_].format;
                    options.raw_width = raw_width_;
                    options.raw_height = raw_height_;
                }
                Status status = workspace.open(path_buffer_, options);
                if (!status.ok()) {
                    workspace.active().setStatus("Open failed: " + status.message());
                }
            } else {
                workspace.active().setStatus("Please enter a file path");
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ToolbarPanel::drawCachePopup(Workspace& workspace) {
    ImGui::SetNextWindowSize(ImVec2(640.0f, 420.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Cache manager", nullptr, ImGuiWindowFlags_None)) {
        return;
    }

    CacheStore& cache = CacheStore::instance();
    constexpr double kMiB = 1024.0 * 1024.0;
    const auto entries = cache.entries();

    uint64_t total = 0;
    int in_use_count = 0;
    for (const auto& e : entries) {
        total += e.size;
        if (e.in_use) {
            ++in_use_count;
        }
    }

    ImGui::Text("Cache root: %s", cache.root().c_str());
    ImGui::Text("%zu pack(s), %u in use, %.2f MB total", entries.size(), in_use_count,
                static_cast<double>(total) / kMiB);

    // Delete ALL: the running streams hold the pack files open, so close them
    // first (they simply re-analyze on next use). This also releases the handles.
    if (ImGui::Button("Delete ALL cache", ImVec2(160.0f, 0.0f))) {
        workspace.closeAll();
        const uint64_t freed = cache.clearAll();
        workspace.active().setStatus("Cleared all cache (" +
                                     std::to_string(freed / (1024 * 1024)) + " MB)");
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete offline only", ImVec2(160.0f, 0.0f))) {
        const uint64_t freed = cache.clearOffline();
        workspace.active().setStatus("Deleted offline cache (" +
                                     std::to_string(freed / (1024 * 1024)) + " MB)");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(Delete ALL closes open streams)");

    ImGui::Separator();
    ImGui::TextUnformatted("Filter (codec/name):");
    ImGui::SameLine();
    char filter_buf[128];
    std::snprintf(filter_buf, sizeof(filter_buf), "%s", cache_filter_.c_str());
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputText("##cachefilter", filter_buf, sizeof(filter_buf))) {
        cache_filter_ = filter_buf;
    }

    std::string to_remove;
    bool remove_in_use = false;
    ImGui::BeginChild("cache_list", ImVec2(0, 240), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("cache_table", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Codec", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("Pack", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();

        for (const auto& e : entries) {
            if (!cache_filter_.empty() &&
                e.name.find(cache_filter_) == std::string::npos) {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::PushID(e.name.c_str());
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(e.codec.c_str());
            ImGui::TableSetColumnIndex(1);
            if (e.in_use) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s (in use)",
                                   e.name.c_str());
            } else {
                ImGui::TextUnformatted(e.name.c_str());
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f MB", static_cast<double>(e.size) / kMiB);
            ImGui::TableSetColumnIndex(3);
            {
                char buf[32] = "-";
                const std::time_t t = static_cast<std::time_t>(e.mtime);
                std::tm* tm = std::localtime(&t);
                if (tm != nullptr) {
                    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
                }
                ImGui::TextUnformatted(buf);
            }
            ImGui::TableSetColumnIndex(4);
            const char* label = e.in_use ? "Delete (close)" : "Delete";
            if (ImGui::SmallButton(label)) {
                to_remove = e.name;
                remove_in_use = e.in_use;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (entries.empty()) {
        ImGui::TextDisabled("(cache is empty)");
    }
    ImGui::EndChild();

    if (!to_remove.empty()) {
        if (remove_in_use) {
            workspace.closeAll();  // release the file handle before deleting
        }
        const bool ok = CacheStore::instance().removePack(to_remove);
        workspace.active().setStatus(std::string(ok ? "Deleted " : "Could not delete ") +
                                     to_remove);
    }

    ImGui::Separator();
    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace vide
