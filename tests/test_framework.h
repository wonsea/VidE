// Minimal, dependency-free test helpers shared by the test translation units.
#pragma once

#include <cstdio>

namespace vide {
namespace test {

inline int g_failures = 0;

inline int report(const char* suite) {
    if (g_failures == 0) {
        std::printf("[PASS] %s\n", suite);
    } else {
        std::printf("[FAIL] %s (%d failures)\n", suite, g_failures);
    }
    return g_failures;
}

}  // namespace test
}  // namespace vide

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++::vide::test::g_failures;                                      \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

int runStatusTests();
int runSyntaxTests();
int runBitReaderTests();
int runSyntaxBuilderTests();
int runXxHashTests();
int runVpkRoundTripTests();
int runVpkFileTests();
int runVpkCorruptTests();
int runBlockSyntaxTests();
int runCacheEvictionTests();
int runStressTests();
int runBitstreamIndexTests();
