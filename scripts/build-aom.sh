#!/usr/bin/env bash
# Builds libaom (AV1) for VidE's AV1 helper. libaom is built out-of-tree into
# build/deps/aom (unified build layout); the resulting libaom.a is used directly
# from there by the main build.
#
# CONFIG_AV1_ENCODER=1 also builds aomenc/aomdec so compatible test streams can
# be produced (this libaom is pre-OBU; see external/patches/README.md).
#
# Usage:  bash scripts/build-aom.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
src="$root/external/libaom"
build="$root/build/deps/aom"

if [ ! -f "$src/CMakeLists.txt" ]; then
    echo "libaom source not found at $src" >&2
    exit 1
fi

echo "Configuring libaom in $build ..."
cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DAOM_TARGET_CPU=x86_64 -DCONFIG_AV1_ENCODER=1 \
    -DCONFIG_UNIT_TESTS=0 -DENABLE_TESTS=0 -DENABLE_EXAMPLES=0 -DENABLE_TOOLS=0 -DENABLE_DOCS=0

echo "Building libaom ..."
cmake --build "$build" --target aom aomenc aomdec -j 8

if [ ! -f "$build/libaom.a" ]; then
    echo "libaom.a was not produced in $build" >&2
    exit 1
fi
echo "libaom ready: $build/libaom.a"
