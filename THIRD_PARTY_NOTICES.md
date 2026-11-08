# 第三方组件与许可 · Third-Party Notices

VidE 本体以 **MIT License** 发布（见根目录 [`LICENSE`](LICENSE)）。本文件列出 VidE 使用/随附的
第三方组件、其许可，以及 FFmpeg 的具体使用方式。分发本软件时请一并保留本文件与各组件许可文本。

---

## FFmpeg — LGPL v2.1 或更高（**动态链接**）

- **用途**：容器探测（`avformat`）、抽取基本流与 AV1 容器→IVF、缩略图/预览解码、`export_mvs` 运动矢量回退。
- **使用方式**：VidE **仅以动态链接**（运行库 DLL / `.so`）方式调用 FFmpeg；**不修改** FFmpeg，**不静态链接**。
- **版本与构建**：构建时由 `scripts/build-ffmpeg.{sh,ps1}` 从 **ffmpeg.org 下载固定版本**（当前 8.1.3），
  **SHA-256 校验**后用 `--enable-shared --disable-static --disable-gpl --disable-nonfree` 构建为 **LGPL 共享库**。
  源码**不提交到本仓库**（只缓存于 `build/deps/ffmpeg-src/`）。
- **许可**：因使用 LGPL 构建 + 动态链接 + 可替换的共享库，LGPL 不要求 VidE 本体开源，故 VidE 保持 MIT。
- **随发布包分发**：因是我们自建的 LGPL 版本，可安全随附 `av*.dll/.so`；已附上
  `FFMPEG-COPYING.LGPLv2.1.txt`，且用户可用自己构建的 FFmpeg 库替换（动态链接天然满足）。
- **注意**：切勿改用 `--enable-gpl` 构建（含 x264/x265/xvid 等，属 GPL），那会使分发物需要 GPL 合规。
  开发机上的系统 FFmpeg（如 MSYS2 包）多为 GPL，**仅用于对比调试，不随发布包分发**。

---

## 参考解码器（源码 vendored 于 `external/`）

| 组件 | codec | 许可 |
|---|---|---|
| [HM](https://hevc.hhi.fraunhofer.de/) | HEVC (H.265) | BSD 3-Clause（ITU/ISO/IEC）|
| [VTM](https://jvet.hhi.fraunhofer.de/) | VVC (H.266) | BSD 3-Clause（ITU/ISO/IEC）|
| [JM](https://www.itu.int/) | AVC (H.264) | ITU-T/ISO/IEC 参考软件许可（BSD 风格）|
| [libvpx](https://chromium.googlesource.com/webm/libvpx/) | VP9 | BSD 3-Clause（The WebM Project）|
| [libaom](https://aomedia.googlesource.com/aom/) | AV1 | BSD（Alliance for Open Media）|

> 这些许可是宽松（非 copyleft）的，可与 MIT 的 VidE 一起分发；各组件完整许可证文本见
> `external/<组件>/` 下的 `COPYING` / `LICENSE` 文件。

---

## 图形界面

| 组件 | 许可 |
|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui)（docking 分支） | MIT |
| [GLFW](https://www.glfw.org/)（系统依赖） | zlib/libpng |

---

## 完整许可文本位置

- FFmpeg（LGPL v2.1）：随 FFmpeg 运行库分发，或见 <https://ffmpeg.org/legal.html>。
- HM：`external/HM/COPYING`
- VTM：`external/VTM/COPYING`
- libvpx：`external/libvpx/LICENSE`
- libaom：`external/libaom/LICENSE`
- Dear ImGui：`external/imgui/LICENSE.txt`
