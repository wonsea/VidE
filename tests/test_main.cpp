#include <cstdlib>
#include <filesystem>
#include <string>

#include "test_framework.h"

namespace {

// Point VidE's scratch/cache root at an isolated directory so tests never touch
// the user's real ./vide_cache (and so CacheStore's capacity tests are safe).
void isolateTemporaryDirectory() {
    std::error_code ec;
    const std::filesystem::path base =
        std::filesystem::temp_directory_path(ec) / "vide_tests_root";
    const std::filesystem::path tmp = base / "tmp";
    std::filesystem::create_directories(tmp, ec);
    const std::string path = tmp.string();
#ifdef _WIN32
    _putenv_s("VIDE_TMP_DIR", path.c_str());
#else
    setenv("VIDE_TMP_DIR", path.c_str(), 1);
#endif
}

}  // namespace

int main() {
    isolateTemporaryDirectory();

    int failures = 0;
    failures += runStatusTests();
    failures += runSyntaxTests();
    failures += runBitReaderTests();
    failures += runSyntaxBuilderTests();
    failures += runXxHashTests();
    failures += runVpkRoundTripTests();
    failures += runVpkFileTests();
    failures += runVpkCorruptTests();
    failures += runBlockSyntaxTests();
    failures += runCacheEvictionTests();
    failures += runStressTests();
    failures += runBitstreamIndexTests();

    if (failures == 0) {
        std::printf("All tests passed\n");
        return 0;
    }
    std::printf("%d test failure(s)\n", failures);
    return 1;
}
