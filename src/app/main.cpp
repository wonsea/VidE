// VidE application entry point: window, ImGui back-end, dockspace, panel
// registry and the small command-line interface used for automation.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "common/syntax_json.h"
#include "core/codec_session.h"
#include "core/session.h"
#include "core/workspace.h"
#include "render/renderer.h"
#include "ui/block_info_panel.h"
#include "ui/bitstream_panel.h"
#include "ui/ctu_panel.h"
#include "ui/frame_panel.h"
#include "ui/image_panel.h"
#include "ui/layout.h"
#include "ui/panel.h"
#include "ui/syntax_panel.h"
#include "ui/thumbnail_panel.h"
#include "ui/toolbar_panel.h"

namespace {

struct CliOptions {
    bool no_gui = false;
    bool show_help = false;
    bool print_info = false;
    std::string input;
    int decode_frame = -1;
    std::string decode_output;
    std::string dump_syntax_output;
    int dump_syntax_frame = 0;
    int dump_blocks_frame = -1;
    std::string dump_blocks_output;
    std::string dump_plane_mode;
    int dump_plane_frame = -1;
    std::string dump_plane_output;
    int bench_frames = 0;

    // Raw-video parameters.
    vide::PixelFormat raw_format = vide::PixelFormat::Unknown;
    int raw_width = 0;
    int raw_height = 0;
    int raw_fps_num = 0;
    int raw_fps_den = 1;
};

vide::PixelFormat parsePixelFormat(const std::string& text) {
    if (text == "I420" || text == "YUV420P") return vide::PixelFormat::I420;
    if (text == "NV12") return vide::PixelFormat::NV12;
    if (text == "NV21") return vide::PixelFormat::NV21;
    if (text == "YUV422P") return vide::PixelFormat::YUV422P;
    if (text == "YUV400" || text == "GRAY") return vide::PixelFormat::YUV400;
    if (text == "YUV444P") return vide::PixelFormat::YUV444P;
    if (text == "P010") return vide::PixelFormat::P010;
    if (text == "P012") return vide::PixelFormat::P012;
    if (text == "P016") return vide::PixelFormat::P016;
    if (text == "RGB888" || text == "RGB24") return vide::PixelFormat::RGB888;
    return vide::PixelFormat::Unknown;
}

vide::OpenOptions toOpenOptions(const CliOptions& options) {
    vide::OpenOptions open;
    open.path = options.input;
    open.raw_format = options.raw_format;
    open.raw_width = options.raw_width;
    open.raw_height = options.raw_height;
    open.raw_fps_num = options.raw_fps_num;
    open.raw_fps_den = options.raw_fps_den;
    return open;
}

void printUsage() {
    std::printf(
        "VidE - video bitstream analyzer\n"
        "Usage: vide [options] [input]\n\n"
        "Options:\n"
        "  --no-gui                    Run without opening a window\n"
        "  --info                      Print stream information and exit\n"
        "  --decode <n> <out.ppm>      Decode frame n to a PPM image and exit\n"
        "  --dump-syntax <out.json>    Export the syntax tree as JSON and exit\n"
        "  --syntax-frame <n>          Frame for --dump-syntax (default 0)\n"
        "  --dump-blocks <n> <out.csv> Export block info for frame n as CSV and exit\n"
        "  --dump-plane <reco|pred|resi> <n> <out.ppm>\n"
        "                              Decode frame n's plane to PPM and exit (HEVC/VVC)\n"
        "  --bench <n>                 Time open, helper decode and n cached reads\n"
        "  --raw-format <fmt>          Raw video format: I420, NV12, NV21, YUV422P,\n"
        "                              YUV400, YUV444P, P010, P012, P016, RGB888\n"
        "  --width <n> --height <n>    Raw video dimensions\n"
        "  --fps <n[/d]>               Raw video frame rate (default 25/1)\n"
        "  -h, --help                  Show this help\n");
}

CliOptions parseArgs(int argc, char** argv) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            options.show_help = true;
        } else if (arg == "--no-gui") {
            options.no_gui = true;
        } else if (arg == "--decode" && i + 2 < argc) {
            options.decode_frame = std::atoi(argv[++i]);
            options.decode_output = argv[++i];
            options.no_gui = true;
        } else if (arg == "--dump-syntax" && i + 1 < argc) {
            options.dump_syntax_output = argv[++i];
            options.no_gui = true;
        } else if (arg == "--syntax-frame" && i + 1 < argc) {
            options.dump_syntax_frame = std::atoi(argv[++i]);
            options.no_gui = true;
        } else if (arg == "--dump-blocks" && i + 2 < argc) {
            options.dump_blocks_frame = std::atoi(argv[++i]);
            options.dump_blocks_output = argv[++i];
            options.no_gui = true;
        } else if (arg == "--dump-plane" && i + 3 < argc) {
            options.dump_plane_mode = argv[++i];
            options.dump_plane_frame = std::atoi(argv[++i]);
            options.dump_plane_output = argv[++i];
            options.no_gui = true;
        } else if (arg == "--bench" && i + 1 < argc) {
            options.bench_frames = std::atoi(argv[++i]);
            options.no_gui = true;
        } else if (arg == "--info") {
            options.no_gui = true;
            options.print_info = true;
        } else if (arg == "--raw-format" && i + 1 < argc) {
            options.raw_format = parsePixelFormat(argv[++i]);
        } else if (arg == "--width" && i + 1 < argc) {
            options.raw_width = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            options.raw_height = std::atoi(argv[++i]);
        } else if (arg == "--fps" && i + 1 < argc) {
            const std::string value = argv[++i];
            const std::size_t slash = value.find('/');
            if (slash != std::string::npos) {
                options.raw_fps_num = std::atoi(value.substr(0, slash).c_str());
                options.raw_fps_den = std::atoi(value.substr(slash + 1).c_str());
            } else {
                options.raw_fps_num = std::atoi(value.c_str());
                options.raw_fps_den = 1;
            }
        } else if (!arg.empty() && arg[0] != '-') {
            options.input = arg;
        }
    }
    return options;
}

bool writePpm(const std::string& path, const vide::DecodedImage& image) {
    if (!image.valid() || image.format != vide::PixelFormat::RGBA8888) {
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "P6\n" << image.width << " " << image.height << "\n255\n";
    for (int y = 0; y < image.height; ++y) {
        const uint8_t* row = image.data.data() + static_cast<std::size_t>(y) * image.width * 4;
        for (int x = 0; x < image.width; ++x) {
            const char rgb[3] = {static_cast<char>(row[x * 4 + 0]),
                                 static_cast<char>(row[x * 4 + 1]),
                                 static_cast<char>(row[x * 4 + 2])};
            out.write(rgb, 3);
        }
    }
    return out.good();
}

int runCli(const CliOptions& options) {
    vide::Session session;
    const auto bench_start = std::chrono::steady_clock::now();
    vide::Status status = session.open(options.input, toOpenOptions(options));
    if (!status.ok()) {
        std::fprintf(stderr, "error: %s\n", status.message().c_str());
        return 2;
    }
    vide::CodecSession* codec = session.codec();
    if (codec == nullptr) {
        std::fprintf(stderr, "error: no codec session\n");
        return 2;
    }

    const vide::MediaInfo& info = session.info();
    std::printf("file        : %s\n", options.input.c_str());
    std::printf("codec       : %s\n", vide::toString(info.codec));
    std::printf("container   : %s\n", info.container.c_str());
    std::printf("resolution  : %dx%d\n", info.width, info.height);
    std::printf("pixel format: %s (%s)\n", vide::toString(info.format),
                vide::toString(info.bit_depth));
    std::printf("frame rate  : %d/%d\n", info.fps_num, info.fps_den);
    std::printf("frame count : %lld\n", static_cast<long long>(info.frame_count));

    if (!options.decode_output.empty()) {
        // Preview decode is always FFmpeg/raw, so it never waits for a helper.
        auto result = codec->preview(static_cast<uint32_t>(options.decode_frame));
        if (!result.ok()) {
            std::fprintf(stderr, "decode error: %s\n", result.status().message().c_str());
            return 3;
        }
        if (!writePpm(options.decode_output, result.value())) {
            std::fprintf(stderr, "failed to write %s\n", options.decode_output.c_str());
            return 4;
        }
        std::printf("wrote frame %d to %s\n", options.decode_frame,
                    options.decode_output.c_str());
    }

    // Helper codecs produce frame syntax in the pack; wait until it exists.
    auto wait_for_frame = [&](uint32_t target) {
        if (!codec->hasBlockSupport()) {
            return;
        }
        int waited_ms = 0;
        const int timeout_ms = 300000;
        while (codec->framesReady() <= target &&
               codec->state() != vide::SessionState::Ready &&
               codec->state() != vide::SessionState::Failed && waited_ms < timeout_ms) {
            session.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            waited_ms += 20;
        }
    };

    if (!options.dump_syntax_output.empty()) {
        wait_for_frame(static_cast<uint32_t>(options.dump_syntax_frame));
        auto result = codec->syntax(static_cast<uint32_t>(options.dump_syntax_frame));
        if (!result.ok()) {
            std::fprintf(stderr, "syntax error: %s\n", result.status().message().c_str());
            return 5;
        }
        std::ofstream out(options.dump_syntax_output, std::ios::binary);
        if (!out) {
            std::fprintf(stderr, "failed to write %s\n", options.dump_syntax_output.c_str());
            return 6;
        }
        out << vide::syntaxTreeToJson(result.value());
        std::printf("wrote syntax tree to %s\n", options.dump_syntax_output.c_str());
    }

    if (!options.dump_blocks_output.empty()) {
        const uint32_t target = static_cast<uint32_t>(options.dump_blocks_frame);
        // Helper codecs wait for the frame in the pack; MV fallback codecs wait
        // for the worker to produce the export_mvs grid.
        wait_for_frame(target);
        auto result = codec->blocks(target);
        int mv_waited = 0;
        while (!result.ok() && codec->hasMotionInfo() && codec->hasBlockSupport() == false &&
               mv_waited < 30000) {
            session.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            mv_waited += 20;
            result = codec->blocks(target);
        }
        if (!result.ok()) {
            std::fprintf(stderr, "block error: %s\n", result.status().message().c_str());
            return 7;
        }
        std::ofstream out(options.dump_blocks_output, std::ios::binary);
        if (!out) {
            std::fprintf(stderr, "failed to write %s\n", options.dump_blocks_output.c_str());
            return 8;
        }
        const vide::BlockGrid& grid = result.value();
        out << "id,x,y,w,h,depth,type,qp,qp_source,bits,bit_offset,bit_length,slice_index,ref0,"
               "mv0x,mv0y,ref1,mv1x,mv1y\n";
        for (const vide::Block& block : grid.blocks) {
            out << block.id << ',' << block.rect.x << ',' << block.rect.y << ','
                << block.rect.w << ',' << block.rect.h << ',' << block.depth << ','
                << vide::toString(block.type) << ',' << block.qp << ','
                << vide::toString(block.qp_source) << ',' << block.bits << ','
                << block.bit_offset << ',' << block.bit_length << ',' << block.slice_index << ','
                << static_cast<int>(block.ref_idx[0]) << ',' << block.mv[0][0] << ','
                << block.mv[0][1] << ',' << static_cast<int>(block.ref_idx[1]) << ','
                << block.mv[1][0] << ',' << block.mv[1][1] << '\n';
        }
        std::printf("wrote %zu blocks for frame %d to %s\n", grid.blocks.size(),
                    options.dump_blocks_frame, options.dump_blocks_output.c_str());
    }

    if (!options.dump_plane_output.empty()) {
        const uint32_t target = static_cast<uint32_t>(options.dump_plane_frame);
        wait_for_frame(target);
        vide::Result<vide::DecodedImage> result =
            (options.dump_plane_mode == "pred")
                ? codec->pred(target)
                : (options.dump_plane_mode == "resi") ? codec->resi(target)
                                                      : codec->reco(target);
        if (!result.ok()) {
            std::fprintf(stderr, "plane error: %s\n", result.status().message().c_str());
            return 9;
        }
        if (!writePpm(options.dump_plane_output, result.value())) {
            std::fprintf(stderr, "failed to write %s\n", options.dump_plane_output.c_str());
            return 10;
        }
        std::printf("wrote frame %d %s plane to %s\n", options.dump_plane_frame,
                    options.dump_plane_mode.c_str(), options.dump_plane_output.c_str());
    }

    if (options.bench_frames > 0) {
        wait_for_frame(0);
        // Open + helper decode is reflected in the wall time up to the first
        // ready frame; then time cached random access.
        const auto open_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - bench_start)
                                 .count();
        const uint64_t frames_ready = codec->framesReady();

        double total_read_ms = 0.0;
        int reads = 0;
        const int span = frames_ready > 0 ? static_cast<int>(frames_ready) : 1;
        for (int i = 0; i < options.bench_frames; ++i) {
            const uint32_t target = static_cast<uint32_t>(i % span);
            const auto t0 = std::chrono::steady_clock::now();
            vide::Result<vide::BlockGrid> grid = codec->blocks(target);
            if (!grid.ok()) {
                break;
            }
            total_read_ms += std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count() /
                             1000.0;
            ++reads;
        }
        std::printf("bench       : open+decode %.0f ms | frames ready %llu\n",
                    static_cast<double>(open_ms),
                    static_cast<unsigned long long>(frames_ready));
        if (reads > 0) {
            std::printf("bench       : %d cached block reads, avg %.2f ms/read\n", reads,
                        total_read_ms / reads);
        } else {
            std::printf("bench       : no cached reads (block info unavailable)\n");
        }
        return 0;
    }

    return 0;
}

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

// Full-viewport host window that owns the docking space and the menu bar.
void drawHostAndDockspace(bool& layout_built, bool& reset_layout, bool& request_exit) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("VidEHost", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    (void)request_exit;  // the menu bar was removed; exit is via the window close

    const ImGuiID dockspace_id = ImGui::GetID("VidEDockSpace");

    if (reset_layout) {
        layout_built = false;
        reset_layout = false;
    }
    if (!layout_built) {
        vide::buildDefaultLayout(dockspace_id);
        layout_built = true;
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f),
                     ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

int runGui(const vide::OpenOptions& open_options) {
#if defined(_WIN32)
    // The GUI is a console-subsystem binary; detach the console so no stray
    // terminal window stays open next to the app window.
    if (HWND console = GetConsoleWindow()) {
        (void)console;
        FreeConsole();
    }
#endif
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() == GLFW_FALSE) {
        std::fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1600, 950, "VidE - Video Bitstream Explorer",
                                          nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "vide_layout.ini";
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    vide::Workspace workspace;
    vide::Renderer renderer;

    vide::ToolbarPanel toolbar;
    vide::SyntaxPanel syntax;
    vide::FramePanel frame;
    vide::ImagePanel picture(renderer);
    vide::ThumbnailPanel thumbnails(renderer);
    vide::CtuPanel ctu(renderer);
    vide::BitstreamPanel bitstream;

    std::vector<vide::IPanel*> panels = {&toolbar, &syntax, &frame, &picture, &thumbnails,
                                         &ctu, &bitstream};

    if (!open_options.path.empty()) {
        workspace.open(open_options.path, open_options);
    }

    bool layout_built = false;
    double last_time = glfwGetTime();

    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwPollEvents();

        const double now = glfwGetTime();
        double delta = now - last_time;
        last_time = now;
        if (delta > 0.25) {
            delta = 0.25;  // avoid huge catch-up after a stall
        }
        workspace.poll();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        bool reset_layout = false;
        bool request_exit = false;
        drawHostAndDockspace(layout_built, reset_layout, request_exit);
        if (request_exit) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        for (vide::IPanel* panel : panels) {
            panel->draw(workspace);
        }

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.09f, 0.10f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Release GPU resources while the OpenGL context is still current.
    renderer.clear();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const CliOptions options = parseArgs(argc, argv);
    if (options.show_help) {
        printUsage();
    }

    if (options.no_gui) {
        if (options.input.empty()) {
            printUsage();
            return 1;
        }
        return runCli(options);
    }

    return runGui(toOpenOptions(options));
}
