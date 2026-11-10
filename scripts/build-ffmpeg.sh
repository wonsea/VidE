#!/usr/bin/env bash
# Builds a pinned, LGPL-only FFmpeg (shared libraries) for VidE.
#
# The source tarball is *not* committed to git. It is downloaded at build time
# from ffmpeg.org and verified against a fixed SHA-256 (version management), then
# built out-of-tree into build/deps/ffmpeg:
#
#   build/deps/ffmpeg-src/ffmpeg-<ver>/   source (cached after first build)
#   build/deps/ffmpeg/{include,lib,bin}  install prefix
#
# LGPL (not GPL) keeps VidE MIT-compatible and allows bundling the av*.dll/.so
# with a release (dynamic linking, user can replace them).
#
# Offline: set VIDE_FFMPEG_TARBALL=/path/to/ffmpeg-<ver>.tar.xz to reuse a local
# tarball instead of downloading.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"

ff_version="8.1.3"
ff_tar="ffmpeg-${ff_version}.tar.xz"
ff_url="https://ffmpeg.org/releases/${ff_tar}"
ff_sha256="7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3"

src_root="$root/build/deps/ffmpeg-src"
src="$src_root/ffmpeg-$ff_version"
out="$root/build/deps/ffmpeg"

# ---- platform ---------------------------------------------------------------
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        ff_target="mingw32"; ff_arch="--arch=x86_64"
        export PATH=/mingw64/bin:/usr/bin:$PATH
        ;;
    Darwin) ff_target="darwin"; ff_arch="" ;;
    *)      ff_target="linux";  ff_arch="" ;;
esac

# ---- dependencies -----------------------------------------------------------
need() { command -v "$1" >/dev/null 2>&1 || { echo "missing required tool: $1" >&2; exit 1; }; }
need tar
need make

# FFmpeg 8.x's x86inc.asm needs NASM >= 2.16; Ubuntu 20.04 ships 2.14.02,
# which fails on libavcodec/x86/vp8dsp.asm. Fail early with actionable advice.
if command -v nasm >/dev/null 2>&1; then
    nasm_version="$(nasm -v | awk '{print $3}')"
    nasm_major="${nasm_version%%.*}"
    nasm_rest="${nasm_version#*.}"
    nasm_minor="${nasm_rest%%.*}"
    if [ "$nasm_major" -lt 2 ] || { [ "$nasm_major" -eq 2 ] && [ "$nasm_minor" -lt 16 ]; }; then
        echo "nasm $nasm_version is too old for FFmpeg $ff_version (need >= 2.16)." >&2
        echo "Install a newer NASM (https://www.nasm.us/) and put it on PATH." >&2
        exit 1
    fi
fi

if command -v nproc >/dev/null 2>&1; then jobs="$(nproc)"; else jobs=8; fi

# ---- fetch + verify ---------------------------------------------------------
mkdir -p "$src_root"
tarball="${VIDE_FFMPEG_TARBALL:-$src_root/$ff_tar}"
if [ ! -f "$tarball" ]; then
    # Convenience: a local copy next to the repo root.
    for cand in "$root/$ff_tar" "$root/external/$ff_tar"; do
        if [ -f "$cand" ]; then tarball="$cand"; break; fi
    done
fi
if [ ! -f "$tarball" ]; then
    echo "Downloading $ff_url ..."
    if command -v curl >/dev/null 2>&1; then
        curl -L --fail --retry 3 -o "$src_root/$ff_tar" "$ff_url"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$src_root/$ff_tar" "$ff_url"
    else
        echo "need curl or wget to download FFmpeg" >&2
        exit 1
    fi
    tarball="$src_root/$ff_tar"
fi

if command -v sha256sum >/dev/null 2>&1; then
    actual="$(sha256sum "$tarball" | awk '{print $1}')"
elif command -v shasum >/dev/null 2>&1; then
    actual="$(shasum -a 256 "$tarball" | awk '{print $1}')"
else
    echo "need sha256sum or shasum to verify the FFmpeg tarball" >&2
    exit 1
fi
actual="$(printf '%s' "$actual" | tr 'A-Z' 'a-z')"
if [ "$actual" != "$ff_sha256" ]; then
    echo "FFmpeg tarball checksum mismatch!" >&2
    echo "  expected: $ff_sha256" >&2
    echo "  actual:   $actual" >&2
    echo "  file:     $tarball" >&2
    exit 1
fi

# ---- extract (cached) -------------------------------------------------------
if [ ! -f "$src/configure" ]; then
    need xz
    echo "Extracting $tarball ..."
    xz -dc "$tarball" | tar -xf - -C "$src_root"
fi

# ---- configure + build (out-of-tree) ---------------------------------------
# Defensive: some extraction paths drop the executable bit.
chmod +x "$src/configure" 2>/dev/null || true
mkdir -p "$out"
cd "$out"

"$src/configure" \
    --prefix="$out" \
    --target-os="$ff_target" $ff_arch \
    --enable-shared --disable-static \
    --disable-autodetect \
    --disable-programs --disable-doc \
    --disable-avdevice --disable-avfilter --disable-network \
    --disable-encoders --disable-hwaccels \
    --disable-gpl --disable-nonfree \
    --enable-swscale --enable-swresample \
    --extra-cflags="-O2"

make -j"$jobs"
make install

echo "FFmpeg $ff_version (LGPL, shared) ready -> $out"
