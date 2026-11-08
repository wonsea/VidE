#!/usr/bin/env bash
# Packages the Linux build into build/dist/VidE-linux-<arch>.tar.gz.
#
# The archive contains vide, the five reference helpers, and the license files.
# Runtime libraries (glfw3, OpenGL, FFmpeg) come from the system; see
# THIRD_PARTY_NOTICES.md and docs/03_build_guide.md.
#
# Usage:  bash scripts/package-linux.sh   (after scripts/build-all.sh)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
bin="$root/build/bin"
dist="$root/build/dist"
stage="$dist/VidE"

if [ ! -x "$bin/vide" ]; then
    echo "build/bin/vide not found; run scripts/build-all.sh first." >&2
    exit 1
fi

shopt -s nullglob
helpers=("$bin"/vide_*_helper)
shopt -u nullglob

rm -rf "$stage"
mkdir -p "$stage"
cp "$bin/vide" "$stage"/
if [ "${#helpers[@]}" -gt 0 ]; then
    cp "${helpers[@]}" "$stage"/
fi
cp "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$stage"/

# Bundle the pinned LGPL FFmpeg shared libraries (binaries have RPATH=$ORIGIN).
fflib="$root/build/deps/ffmpeg/lib"
if [ -d "$fflib" ]; then
    cp -a "$fflib"/libav*.so* "$fflib"/libsw*.so* "$stage"/ 2>/dev/null || true
fi
lgpl="$(find "$root/build/deps/ffmpeg-src" -name 'COPYING.LGPLv2.1' 2>/dev/null | head -n1 || true)"
if [ -n "$lgpl" ]; then
    cp "$lgpl" "$stage/FFMPEG-COPYING.LGPLv2.1.txt"
fi

arch="$(uname -m)"
tarball="$dist/VidE-linux-${arch}.tar.gz"
tar -czf "$tarball" -C "$dist" VidE
rm -rf "$stage"

echo "Packaged: $tarball"
echo "Runtime deps: glfw3, OpenGL and FFmpeg (LGPL recommended); see THIRD_PARTY_NOTICES.md."
