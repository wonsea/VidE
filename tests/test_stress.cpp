// Offline memory / large-data stress tests (E-4).
//
// Headless only: exercises a large .vpk (multi-hundred-MB class payload), random
// access, and a long run of open/read/close cycles, asserting that resident
// memory stays bounded. Also churns the cache store with many packs to confirm
// the capacity limit converges. test_main.cpp isolates VIDE_TMP_DIR first.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "common/vpk.h"
#include "core/cache_store.h"
#include "test_framework.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

using namespace vide;

namespace {

DecodedImage makeReco(int w, int h, uint8_t value) {
    DecodedImage image;
    image.width = w;
    image.height = h;
    image.format = PixelFormat::RGBA8888;
    image.bit_depth = BitDepth::B8;
    image.data.assign(static_cast<std::size_t>(w) * h * 4, value);
    for (std::size_t i = 3; i < image.data.size(); i += 4) {
        image.data[i] = 255;
    }
    return image;
}

BlockGrid makeGrid(int w, int h, int qp) {
    BlockGrid grid;
    grid.width = w;
    grid.height = h;
    grid.ctu_size = 64;
    Block block;
    block.id = 0;
    block.rect = Rect{0, 0, 16, 16};
    block.type = BlockType::Intra;
    block.qp = qp;
    grid.blocks.push_back(block);
    return grid;
}

// Resident set size of the current process (bytes); 0 when unavailable.
std::size_t processRssBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<std::size_t>(pmc.WorkingSetSize);
    }
    return 0;
#else
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (f == nullptr) {
        return 0;
    }
    long total_pages = 0;
    long rss_pages = 0;
    const int n = std::fscanf(f, "%ld %ld", &total_pages, &rss_pages);
    std::fclose(f);
    (void)total_pages;
    return n == 2 ? static_cast<std::size_t>(rss_pages) * 4096u : 0;
#endif
}

void clearPacks(const std::filesystem::path& root) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".vpk") {
            std::error_code rm;
            std::filesystem::remove(entry.path(), rm);
        }
    }
}

}  // namespace

int runStressTests() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path path = fs::temp_directory_path() / "vide_stress_large.vpk";
    fs::remove(path, ec);

    const int w = 1280;
    const int h = 720;
    const uint32_t frames = 24;  // 24 * 1280*720*4 ~= 88 MB of reco payload

    // ---- Large-data pack: write, then random-access read --------------------
    {
        VpkWriter writer;
        SourceFingerprint fp;
        fp.size = 1;
        fp.mtime = 2;
        fp.hash = 3;
        fp.valid = true;
        CHECK(writer.begin(path.string(), CodecId::HEVC, fp, w, h, BitDepth::B8, 0).ok());
        for (uint32_t i = 0; i < frames; ++i) {
            FrameMeta meta;
            meta.index = i;
            meta.poc = static_cast<int32_t>(i);
            meta.is_keyframe = (i == 0);
            const DecodedImage reco = makeReco(w, h, static_cast<uint8_t>(i));
            const BlockGrid grid = makeGrid(w, h, 20 + static_cast<int>(i % 10));
            CHECK(writer.appendFrame(meta, nullptr, &grid, &reco).ok());
        }
        CHECK(writer.finish().ok());
    }

    const uint64_t file_size = fs::file_size(path, ec);
    CHECK(file_size > static_cast<uint64_t>(w) * h * 4 * frames / 2);

    {
        VpkReader reader;
        CHECK(reader.open(path.string()).ok());
        CHECK(reader.frameCount() == frames);
        const uint32_t picks[] = {0, 7, 23, 11};
        for (uint32_t i : picks) {
            auto reco = reader.reco(i);
            CHECK(reco.ok());
            if (reco.ok()) {
                CHECK(reco.value().data.size() == static_cast<std::size_t>(w) * h * 4);
                CHECK(reco.value().data[0] == static_cast<uint8_t>(i));
            }
            CHECK(reader.blocks(i).ok());
        }
    }

    // ---- Long-run memory bound: repeated open/read/close --------------------
    {
        // Warm-up pass so first-touch allocations are not counted as growth.
        {
            VpkReader reader;
            CHECK(reader.open(path.string()).ok());
            (void)reader.reco(0);
        }
        const std::size_t baseline = processRssBytes();
        const int passes = 200;
        for (int pass = 0; pass < passes; ++pass) {
            VpkReader reader;
            CHECK(reader.open(path.string()).ok());
            auto reco = reader.reco(static_cast<uint32_t>(pass % frames));
            CHECK(reco.ok());
        }
        const std::size_t after = processRssBytes();
        if (baseline > 0 && after > 0) {
            const long long growth = static_cast<long long>(after) - static_cast<long long>(baseline);
            std::printf("  rss %zu MB -> %zu MB over %d passes (growth %lld MB)\n", baseline >> 20,
                        after >> 20, passes, growth >> 20);
            CHECK(growth < 128LL * 1024 * 1024);
        }
    }

    fs::remove(path, ec);

    // ---- Cache churn: many packs stay under the converged limit -------------
    {
        CacheStore& cache = CacheStore::instance();
        const fs::path root = cache.root();
        fs::create_directories(root, ec);
        clearPacks(root);
        for (int i = 0; i < 200; ++i) {
            std::ofstream out(root / ("pack_" + std::to_string(i) + ".vpk"),
                              std::ios::binary | std::ios::trunc);
            const std::vector<char> data(4096, 'z');
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
        cache.setMaxTotalBytes(100 * 1024);
        bool within = true;
        for (int i = 0; i < 50; ++i) {
            cache.refresh();
            within = cache.enforceLimit() && within;
        }
        CHECK(within);
        CHECK(cache.totalBytes() <= static_cast<uint64_t>(100 * 1024) * 9 / 10);
        const uint64_t downsized = cache.totalBytes();
        CHECK(downsized > 0);

        cache.clearOffline();
        CHECK(cache.totalBytes() == 0);
        cache.setMaxTotalBytes(40ull * 1024 * 1024 * 1024);
        clearPacks(root);
    }

    return test::report("stress (large-data / long-run)");
}
