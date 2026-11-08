#!/usr/bin/env bash
export PATH=/mingw64/bin:/usr/bin:$PATH
cd /d/work/source/VidE/external/libvpx
make vp9/decoder/vp9_capture.c.o -j1 2>&1 | grep -E "error|Error|warning|Werror" | head -30
