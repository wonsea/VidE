#!/usr/bin/env bash
# Installs the build dependencies for VidE on Debian/Ubuntu so that a fresh
# clone can be built with one command:
#
#   bash scripts/bootstrap-ubuntu.sh
#   bash scripts/build-all.sh --test
#   bash scripts/run.sh
#
# FFmpeg is consumed as a *system* dependency (dynamically linked) on Linux;
# VidE itself stays MIT. See THIRD_PARTY_NOTICES.md.
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This script targets Debian/Ubuntu (apt-get not found)." >&2
    echo "Install the equivalent packages with your distro's package manager:" >&2
    echo "  cmake ninja-build pkg-config g++ libglfw3-dev libgl1-mesa-dev" >&2
    echo "  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev nasm yasm" >&2
    exit 1
fi

sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libglfw3-dev \
    libgl1-mesa-dev \
    libaom-dev \
    nasm yasm \
    perl \
    curl xz-utils

echo "VidE build dependencies installed (FFmpeg is downloaded and built by scripts/build-ffmpeg.sh)."
echo "Next:  bash scripts/build-all.sh --test"
