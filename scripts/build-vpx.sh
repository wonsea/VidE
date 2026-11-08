#!/usr/bin/env bash
# Builds libvpx for VidE's VP9 helper (static, decoder only).
#
# libvpx builds in-tree (its build system does not support out-of-tree builds),
# so the resulting static library is copied to build/deps/vpx for the unified
# build layout the main CMake expects.
set -e

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
vpx="$root/external/libvpx"
out="$root/build/deps/vpx"

# libvpx's --target is platform-specific; pick it automatically so the same
# script works on Windows (MSYS2/MinGW), Linux and macOS.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        vpx_target="x86_64-win64-gcc"
        export PATH=/mingw64/bin:/usr/bin:$PATH
        ;;
    Darwin)
        if [ "$(uname -m)" = "arm64" ]; then
            vpx_target="arm64-darwin20-gcc"
        else
            vpx_target="x86_64-darwin20-gcc"
        fi
        ;;
    *)
        vpx_target="x86_64-linux-gcc"
        ;;
esac

cd "$vpx"
# Invoke via sh: the vendored configure is committed without the executable bit,
# so `./configure` fails on a fresh Linux clone. `sh ./configure` is mode-agnostic.
sh ./configure \
    --target="$vpx_target" \
    --disable-unit-tests --disable-examples --disable-tools --disable-docs \
    --enable-vp9-highbitdepth --disable-vp8 --disable-webm-io --disable-libyuv
# VidE patch: drop warnings-as-errors (GCC 16 emits new diagnostics).
grep -rl -- '-Werror' . --include=Makefile --include='*.mk' 2>/dev/null | xargs -r sed -i 's/-Werror//g'
grep -rl -- '-Werror' build/make 2>/dev/null | xargs -r sed -i 's/-Werror//g'
make -j8

mkdir -p "$out"
cp -f libvpx.a "$out/libvpx.a"
echo "libvpx build complete -> $out/libvpx.a"
