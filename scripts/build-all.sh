#!/usr/bin/env bash
# One-step build for VidE (Linux / macOS; on Windows use build-all.ps1).
#
# Builds the third-party libraries (pinned LGPL FFmpeg, HM / VTM / libaom /
# libvpx) into build/deps/, then configures and builds the main project.
#
# FFmpeg source is downloaded and verified (SHA-256) at build time -- it is not
# committed to git. Everything else is vendored under external/.
#
# On Debian/Ubuntu, missing build dependencies are installed automatically via
# scripts/bootstrap-ubuntu.sh (needs sudo). Pass --no-bootstrap to disable.
#
# Usage:  bash scripts/build-all.sh [--skip-deps] [--test] [--no-bootstrap]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"

skip_deps=0
run_tests=0
no_bootstrap=0
for arg in "$@"; do
    case "$arg" in
        --skip-deps)    skip_deps=1 ;;
        --test)         run_tests=1 ;;
        --no-bootstrap) no_bootstrap=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

# ---- dependency check -------------------------------------------------------
missing_tools() {
    for t in cmake ninja g++ make nasm tar; do
        command -v "$t" >/dev/null 2>&1 || echo "$t"
    done
}

missing="$(missing_tools)"
if [ -n "$missing" ]; then
    if [ "$no_bootstrap" -eq 0 ] && command -v apt-get >/dev/null 2>&1; then
        echo "Missing build tools: $(echo "$missing" | tr '\n' ' ')"
        echo "Installing dependencies via scripts/bootstrap-ubuntu.sh ..."
        bash "$here/bootstrap-ubuntu.sh"
        missing="$(missing_tools)"
    fi
    if [ -n "$missing" ]; then
        echo "Missing build tools: $(echo "$missing" | tr '\n' ' ')" >&2
        echo "" >&2
        echo "Install the build dependencies first:" >&2
        echo "  bash scripts/bootstrap-ubuntu.sh    # Debian/Ubuntu" >&2
        echo "  (macOS: brew install cmake ninja nasm yasm glfw aom)" >&2
        exit 1
    fi
fi

if [ "$skip_deps" -eq 0 ]; then
    echo "==== [1/6] FFmpeg (LGPL shared) ===="; bash "$here/build-ffmpeg.sh"
    echo "==== [2/6] HM (HEVC) ===="         ; bash "$here/build-hm.sh"
    echo "==== [3/6] VTM (VVC) ===="          ; bash "$here/build-vtm.sh"
    echo "==== [4/6] libaom (AV1) ===="       ; bash "$here/build-aom.sh"
    echo "==== [5/6] libvpx (VP9) ===="       ; bash "$here/build-vpx.sh"
else
    echo "Skipping third-party deps (--skip-deps)."
fi

echo "==== [6/6] VidE (main) ===="
cmake -S "$root" -B "$root/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DVIDE_WITH_FFMPEG=ON -DVIDE_WITH_HEVC_HM=ON -DVIDE_WITH_VVC_VTM=ON \
    -DVIDE_WITH_AVC_JM=ON -DVIDE_WITH_VP9=ON -DVIDE_WITH_AV1=ON
cmake --build "$root/build"

if [ "$run_tests" -eq 1 ]; then
    ctest --test-dir "$root/build" --output-on-failure
fi

echo
echo "Build complete."
echo "  run (GUI):  bash scripts/run.sh"
echo "  package:    bash scripts/package-linux.sh   -> build/dist/VidE-linux-<arch>.tar.gz"
