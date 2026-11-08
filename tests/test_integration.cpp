// Headless integration tests for the session layer.
//
// These drive Workspace / Session / CodecSession directly (no GLFW, no ImGui)
// using raw-video inputs, covering the parts of R6 multi-stream, raw opening
// (C-9) and resource release (E-3) that would otherwise need manual GUI
// testing. They link vide_codec, so they are only built when FFmpeg is enabled.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_framework.h"

#include "codec/adapter.h"
#include "common/frame.h"
#include "common/status.h"
#include "common/types.h"
#include "core/session.h"
#include "core/workspace.h"

namespace {

// Writes a deterministic raw I420 clip: `frames` pictures of w x h at 8 bit.
std::string writeRawClip(const std::filesystem::path& dir, const std::string& name, int w, int h,
                         int frames) {
    const std::size_t frame_size =
        static_cast<std::size_t>(w) * h * 3 / 2;  // 4:2:0, 8-bit
    std::vector<uint8_t> data(frame_size * static_cast<std::size_t>(frames));
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
    }
    const std::filesystem::path path = dir / name;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return path.string();
}

vide::OpenOptions rawOptions(const std::string& path, int w, int h) {
    vide::OpenOptions options;
    options.path = path;
    options.raw_format = vide::PixelFormat::I420;
    options.raw_width = w;
    options.raw_height = h;
    options.raw_fps_num = 25;
    options.raw_fps_den = 1;
    return options;
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "vide_integration";
    fs::create_directories(dir, ec);

    const int w = 48;
    const int h = 32;
    const int frames = 5;
    const std::string clip_a = writeRawClip(dir, "clip_a.yuv", w, h, frames);
    const std::string clip_b = writeRawClip(dir, "clip_b.yuv", w, h, frames);

    // ---- Single raw open ------------------------------------------------
    {
        vide::Session session;
        vide::Status status = session.open(clip_a, rawOptions(clip_a, w, h));
        CHECK(status.ok());
        CHECK(session.hasDocument());
        CHECK(session.info().width == w);
        CHECK(session.info().height == h);
        CHECK(session.frameCount() == frames);
        CHECK(session.codec() != nullptr);

        auto preview = session.codec()->preview(0);
        CHECK(preview.ok());
        CHECK(preview.ok() && preview.value().width == w && preview.value().height == h);
        auto reco = session.codec()->reco(3);
        CHECK(reco.ok());
        // Raw block info is unavailable but must not crash.
        auto blocks = session.codec()->blocks(0);
        CHECK(!blocks.ok());
        session.poll();
        session.close();
        CHECK(!session.hasDocument());
    }

    // ---- Raw open without geometry must fail cleanly --------------------
    {
        vide::Session session;
        vide::OpenOptions bad;
        bad.path = clip_a;
        bad.raw_format = vide::PixelFormat::I420;  // no width/height provided
        vide::Status status = session.open(clip_a, bad);
        CHECK(!status.ok());
        CHECK(!session.hasDocument());
    }

    // ---- Nonexistent file fails cleanly ---------------------------------
    {
        vide::Session session;
        vide::Status status = session.open((dir / "does_not_exist.yuv").string(), {});
        CHECK(!status.ok());
    }

    // ---- R6: multiple streams, switch, remove ---------------------------
    {
        vide::Workspace ws;
        CHECK(ws.empty());

        vide::Status a = ws.open(clip_a, rawOptions(clip_a, w, h));
        vide::Status b = ws.open(clip_b, rawOptions(clip_b, w, h));
        CHECK(a.ok() && b.ok());
        CHECK(ws.count() == 2);
        CHECK(ws.activeIndex() == 1);
        CHECK(ws.active().hasDocument());
        CHECK(ws.at(0) != nullptr && ws.at(1) != nullptr);
        CHECK(ws.at(0)->path() == clip_a);
        CHECK(ws.at(1)->path() == clip_b);

        ws.setActive(0);
        CHECK(ws.activeIndex() == 0);
        CHECK(ws.active().path() == clip_a);

        // poll() advances every open stream without a display.
        ws.poll();

        ws.removeActive();
        CHECK(ws.count() == 1);
        CHECK(ws.activeIndex() == 0);
        CHECK(ws.active().path() == clip_b);

        ws.removeActive();
        CHECK(ws.empty());
        CHECK(ws.activeIndex() == -1);
        // active() on an empty workspace returns a scratch session.
        CHECK(!ws.active().hasDocument());
    }

    // ---- E-3: repeated open/close must release resources ----------------
    {
        vide::Session session;
        for (int i = 0; i < 100; ++i) {
            vide::Status status = session.open((i % 2 == 0) ? clip_a : clip_b,
                                               rawOptions((i % 2 == 0) ? clip_a : clip_b, w, h));
            CHECK(status.ok());
            auto image = session.codec()->preview(static_cast<uint32_t>(i % frames));
            CHECK(image.ok());
            session.close();
        }
    }

    // ---- R6: a bigger workspace - 6 streams, remove from the middle -----
    {
        std::vector<std::string> clips;
        for (int i = 0; i < 6; ++i) {
            clips.push_back(writeRawClip(dir, "clip_m" + std::to_string(i) + ".yuv", w, h, frames));
        }
        vide::Workspace ws;
        for (const std::string& clip : clips) {
            CHECK(ws.open(clip, rawOptions(clip, w, h)).ok());
        }
        CHECK(ws.count() == 6);
        CHECK(ws.activeIndex() == 5);

        // Remove a middle stream; active index and neighbours must stay sane.
        ws.removeAt(2);
        CHECK(ws.count() == 5);
        CHECK(ws.at(2) != nullptr);
        CHECK(ws.at(2)->path() == clips[3]);
        ws.poll();

        // Switch every stream in turn without a display.
        for (int i = 0; i < ws.count(); ++i) {
            ws.setActive(i);
            CHECK(ws.active().hasDocument());
            auto image = ws.active().codec()->preview(1);
            CHECK(image.ok());
        }

        ws.closeAll();
        CHECK(ws.empty());
        CHECK(ws.activeIndex() == -1);
    }

    // ---- Raw: open several formats and sizes ----------------------------
    {
        // YUV400 (luma only) is 1 byte/pixel, unlike the I420 helper above.
        const int gw = 64;
        const int gh = 48;
        const int gframes = 3;
        std::vector<uint8_t> gray_data(static_cast<std::size_t>(gw) * gh * gframes);
        for (std::size_t i = 0; i < gray_data.size(); ++i) {
            gray_data[i] = static_cast<uint8_t>((i * 5 + 3) & 0xFF);
        }
        const std::filesystem::path gray_path = dir / "gray.raw";
        std::ofstream gray_out(gray_path, std::ios::binary);
        gray_out.write(reinterpret_cast<const char*>(gray_data.data()),
                       static_cast<std::streamsize>(gray_data.size()));
        gray_out.close();

        vide::Session s;
        vide::OpenOptions o;
        o.path = gray_path.string();
        o.raw_format = vide::PixelFormat::YUV400;
        o.raw_width = gw;
        o.raw_height = gh;
        o.raw_fps_num = 30;
        o.raw_fps_den = 1;
        CHECK(s.open(gray_path.string(), o).ok());
        CHECK(s.info().width == gw && s.info().height == gh);
        CHECK(s.frameCount() == gframes);
        auto image = s.codec()->preview(2);
        CHECK(image.ok());
        s.close();
    }

    std::error_code rm_ec;
    fs::remove_all(dir, rm_ec);

    return vide::test::report("integration (session/raw/multi-stream)");
}
