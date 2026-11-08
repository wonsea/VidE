# Third-party patches

VidE keeps upstream sources under `external/` as unmodified as possible. The few
changes that are required are documented here. Each patch is intentionally
minimal and isolated.

## HM (HEVC Test Model) 16.22

Location: `external/HM` (obtained from the gitlab.com mirror of the HM reference
software; upstream is https://vcgit.hhi.fraunhofer.de/jvet/HM).

### Patch 1 — `CMakeLists.txt`

- **Change**: removed `warnings-as-errors` from the GCC `bb_enable_warnings(...)`
  call.
- **Reason**: GCC 16 emits new `-Warray-bounds` diagnostics in
  `TComLoopFilter.cpp`, which `-Werror` turns into build errors. No source
  behaviour changes.

### Patch 2 — `source/Lib/TLibDecoder/TDecCu.cpp` + `VideBlockTrace.{h,cpp}`

- **Added**: `source/Lib/TLibDecoder/VideBlockTrace.h` and `VideBlockTrace.cpp`.
- **Change**: `TDecCu::xFinishDecodeCU` now calls `vide_hm::traceCu(...)` with the
  leaf coding unit's position, size, depth, prediction mode, intra flag, QP and
  inter direction.
- **Reason**: HM does not expose CU geometry through a public API. This hook is
  the only source of HEVC block-level information for VidE.
- **Zero-cost by default**: `traceCu` does nothing unless the environment
  variable `VIDE_BLOCK_TRACE` names a writable file. The CSV header is
  `poc,x,y,w,h,depth,predmode,isIntra,qp,interDir`.

The added files are compiled automatically because HM's `TLibDecoder` CMake
target globs `*.cpp` in that directory. (Superseded by Patch 3: block data is
now read directly from the decoder structures; no trace file is used.)

### Patch 3 — `VideCapture.{h,cpp}` + `TDecCu.cpp` (prediction / residual capture)

- **Added**: `source/Lib/TLibDecoder/VideCapture.{h,cpp}`.
- **Change**: `TDecCu::xReconInter` and `TDecCu::xIntraRecBlk` copy the CU/TU
  prediction and residual into picture-sized side buffers (keyed by `TComPic*`).
- **Reason**: HM's decoder keeps prediction/residual only in transient per-CU
  buffers and leaves the picture-level prediction/residual buffers null (`TDecSlice`
  calls `setPicYuvPred(0)` / `setPicYuvResi(0)`). The hook makes them available
  to `vide_hevc_helper` for the prediction/residual views (ADR D7).
- **Isolation**: the hook only reads existing buffers and stores copies; it does
  not change decoding behaviour. Compiled automatically (TLibDecoder globs `*.cpp`).

### Patch 4 — `VideCapture.{h,cpp}` + `TDecBinCoderCABAC.cpp` + `TDecCu.cpp` (per-CU bits)

- **Change**: every CABAC read (`decodeBin`/`decodeBinEP`/`decodeBinsEP`/
  `decodeAlignedBinsEP`/`decodeBinTrm`) calls `vide_hm::addBins(n)`;
  `TDecCu::xDecodeCU` calls `markCuStart` and `xFinishDecodeCU` calls
  `recordCuBits`, giving each CU a bin-count delta.
- **Reason**: HM's decoder does not record per-CU bit counts (`m_uiTotalBits` is
  only used by the RD encoder). The hook provides the `bits` column block info.
- **Isolation**: only increments counters; decoding is unchanged.
- **Rebuild**: run `scripts/build-hm.ps1` after changing this hook.
