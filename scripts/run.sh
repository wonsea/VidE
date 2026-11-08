#!/usr/bin/env bash
# Runs a VidE binary from build/bin with the helper paths set and build/bin on
# PATH, so the reference helpers are found next to the app.
#
# Usage:
#   bash scripts/run.sh                       # GUI
#   bash scripts/run.sh --info clip.h265
#   bash scripts/run.sh --dump-blocks 0 blocks.csv clip.h265
#   bash scripts/run.sh -tool vide_tests
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
bin="$root/build/bin"

tool="vide"
if [ "${1:-}" = "-tool" ]; then
    tool="${2:?usage: run.sh -tool <name> [args...]}"
    shift 2
fi

exe="$bin/$tool"
if [ ! -x "$exe" ]; then
    echo "Binary not found: $exe. Build first (see docs/03_build_guide.md)." >&2
    exit 1
fi

export PATH="$bin:$PATH"
export VIDE_HEVC_HELPER_PATH="$bin/vide_hevc_helper"
export VIDE_VVC_HELPER_PATH="$bin/vide_vvc_helper"
export VIDE_AVC_HELPER_PATH="$bin/vide_avc_helper"
export VIDE_VP9_HELPER_PATH="$bin/vide_vp9_helper"
export VIDE_AV1_HELPER_PATH="$bin/vide_av1_helper"

echo "Running $exe $*"
exec "$exe" "$@"
