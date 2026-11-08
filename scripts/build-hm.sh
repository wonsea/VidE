#!/usr/bin/env bash
# Builds the HM (HEVC Test Model) static libraries that VidE links to read
# block-level data straight from the decoder structures.
#
# Only one upstream change is applied: the CMake `warnings-as-errors` flag is
# dropped for GCC compatibility (see external/patches/README.md).
#
# All build output goes under build/deps/hm (unified build layout); the produced
# static libraries are copied to build/deps/hm/lib where the main build looks.
#
# Usage:  bash scripts/build-hm.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
src="$root/external/HM"
build="$root/build/deps/hm"
libout="$build/lib"

if [ ! -f "$src/CMakeLists.txt" ]; then
    echo "HM source not found at $src" >&2
    exit 1
fi

echo "Configuring HM in $build ..."
cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release

echo "Building HM static libraries ..."
cmake --build "$build" -j 8

if ! ls "$src"/lib/*/libTLib*.a >/dev/null 2>&1; then
    echo "HM static libraries were not produced" >&2
    exit 1
fi

# HM's BBuildEnv writes archives into <source>/lib/<toolchain>/...; copy them
# flat into the unified build directory so the main build finds a stable path.
rm -rf "$libout"
mkdir -p "$libout"
find "$src/lib" -name '*.a' -exec cp -f {} "$libout"/ \;
echo "HM libraries ready in $libout"
