#!/usr/bin/env bash
# Builds the VTM (VVC Test Model) reference decoder for VidE.
#
# VidE reads block/syntax data directly from VTM's structures and keeps
# picture-sized prediction/residual buffers (KEEP_PRED_AND_RESI_SIGNALS) so
# vide_vvc_helper can export reco/pred/resi images. ENABLE_TRACING must be OFF to
# match the helper's compile definitions. See external/patches/README.md.
#
# All build output goes under build/deps/vtm (unified build layout); the produced
# static libraries are copied to build/deps/vtm/lib where the main build looks.
#
# Usage:  bash scripts/build-vtm.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
src="$root/external/VTM"
build="$root/build/deps/vtm"
libout="$build/lib"

if [ ! -f "$src/CMakeLists.txt" ]; then
    echo "VTM source not found at $src" >&2
    exit 1
fi

echo "Configuring VTM in $build ..."
cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_TRACING=OFF \
    -DENABLE_HIGH_BITDEPTH=ON \
    -DVIDE_DEFS=KEEP_PRED_AND_RESI_SIGNALS=1

echo "Building VTM static libraries ..."
cmake --build "$build" --target DecoderLib -j 8

if ! ls "$src"/lib/*/lib*.a >/dev/null 2>&1; then
    echo "VTM static libraries were not produced" >&2
    exit 1
fi

# VTM's BBuildEnv writes archives into <source>/lib/<toolchain>/...; copy them
# flat into the unified build directory so the main build finds a stable path.
rm -rf "$libout"
mkdir -p "$libout"
find "$src/lib" -name '*.a' -exec cp -f {} "$libout"/ \;
echo "VTM libraries ready in $libout"
