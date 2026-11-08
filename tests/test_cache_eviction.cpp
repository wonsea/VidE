// Unit tests for CacheStore capacity management (C-5).
//
// The real limit is 40 GB, so rather than push gigabytes we exercise the
// eviction policy on tiny dummy packs with a small limit. test_main.cpp points
// VIDE_TMP_DIR at an isolated directory before any test runs, so this never
// touches the user's real ./vide_cache.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/cache_store.h"
#include "test_framework.h"

using namespace vide;

namespace {

void writePack(const std::filesystem::path& path, std::size_t bytes) {
    std::ofstream out(path, std::ios::binary);
    const std::vector<char> data(bytes, 'x');
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
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

int runCacheEvictionTests() {
    namespace fs = std::filesystem;
    CacheStore& cache = CacheStore::instance();
    const fs::path root = cache.root();
    std::error_code ec;
    fs::create_directories(root, ec);

    // --- evictOffline: LRU offline eviction down to 90%, never in-use -------
    clearPacks(root);
    writePack(root / "inuse.vpk", 100);
    writePack(root / "off1.vpk", 100);
    writePack(root / "off2.vpk", 100);
    cache.markInUse("inuse.vpk");
    cache.setMaxTotalBytes(250);  // 300 > 250, target = 225
    const bool within = cache.enforceLimit();
    CHECK(within);
    CHECK(cache.totalBytes() <= 225);
    CHECK(fs::exists(root / "inuse.vpk"));  // in-use is never evicted
    // 300 - one 100-byte pack = 200 <= 225, so exactly one offline pack goes.
    int remaining = 0;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".vpk") {
            ++remaining;
        }
    }
    CHECK(remaining == 2);
    cache.markOffline("inuse.vpk");

    // --- Plan B: only in-use packs remain and still over limit -> false ------
    clearPacks(root);
    writePack(root / "big.vpk", 300);
    cache.markInUse("big.vpk");
    cache.setMaxTotalBytes(100);
    const bool plan_b = cache.enforceLimit();
    CHECK(!plan_b);                     // caller must abort the analysis
    CHECK(fs::exists(root / "big.vpk"));  // in-use survives
    cache.markOffline("big.vpk");

    // --- clearOffline: frees offline, keeps in-use --------------------------
    clearPacks(root);
    writePack(root / "keep.vpk", 50);
    writePack(root / "drop.vpk", 50);
    cache.markInUse("keep.vpk");
    const uint64_t freed = cache.clearOffline();
    CHECK(freed == 50);
    CHECK(fs::exists(root / "keep.vpk"));
    CHECK(!fs::exists(root / "drop.vpk"));
    cache.markOffline("keep.vpk");

    // --- cleanStale: removes leftover .tmp / .lock --------------------------
    writePack(root / "temp.tmp", 10);
    writePack(root / "stale.lock", 10);
    cache.cleanStale();
    CHECK(!fs::exists(root / "temp.tmp"));
    CHECK(!fs::exists(root / "stale.lock"));

    // Restore the default so this test cannot influence anything else.
    cache.setMaxTotalBytes(40ull * 1024 * 1024 * 1024);
    clearPacks(root);
    return test::report("cache eviction");
}
