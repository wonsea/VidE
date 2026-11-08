# VidE 构建与运行指南

> 版本：v1.0
> 关联：[README.md](../README.md) · [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)

---

## 0. 从零构建（TL;DR）

```powershell
# Windows（有 winget 会自动装 MSYS2；否则先手动装一次 MSYS2）
powershell -ExecutionPolicy Bypass -File scripts\build-all.ps1 -RunTests
```
```bash
# Ubuntu / Debian（需要 sudo）
bash scripts/build-all.sh --test
```

产物：`build/bin/`（开发版）、`build/dist/`（可直接运行的分发包）。
首次构建会依次：检查/安装依赖 → 下载并构建 LGPL FFmpeg → 构建 HM/VTM/libaom/libvpx → 编译 VidE → 打包。
约 15–40 分钟，磁盘 ≥ 6GB，需联网（仅首次下载 FFmpeg 源码 ~12MB）。

> **验证状态**：Windows 已用**干净源码树**完整验证（编译 + `ctest` 2/2 + `build/dist` 可运行）；
> Ubuntu 脚本与 CI 已就绪，尚未在真机 Ubuntu 验证（推送 tag 后 GitHub Actions 自动验证）。

---

## 1. 依赖

| 依赖 | 版本 | 获取方式 |
|---|---|---|
| CMake | ≥ 3.20 | MSYS2 `mingw-w64-x86_64-cmake` |
| Ninja | 任意 | MSYS2 `mingw-w64-x86_64-ninja` |
| MinGW-w64 GCC | ≥ 11（当前 16.2） | MSYS2 `mingw-w64-x86_64-gcc` |
| GLFW | ≥ 3.3 | MSYS2 `mingw-w64-x86_64-glfw` |
| FFmpeg | **8.1.3（构建时下载 + 源码构建，LGPL shared）** | `scripts/build-ffmpeg.ps1`（Win）/ `build-ffmpeg.sh`（Linux）；产物 `build/deps/ffmpeg`，SHA-256 校验，不入 git |
| libvpx | ≥ 1.13（就地构建，产物拷到 `build/deps/vpx/libvpx.a`） | `mingw-w64-x86_64-libvpx` / `scripts/build-vpx.sh` |
| libaom | **内置 `external/libaom`（pre-OBU）** | AV1 helper 链接 `build/deps/aom/libaom.a`（`scripts/build-aom.ps1`）；MSYS2 的 `aom` 仅供 AV1 头语法解析，不用于 helper |
| pkgconf | 任意 | MSYS2 `mingw-w64-x86_64-pkgconf` |
| Dear ImGui | docking | 已置于 `external/imgui` |
| HM | 16.22 | 已置于 `external/HM` |
| VTM | 19.2 | 已置于 `external/VTM` |

> Windows **必须装 MSYS2**（FFmpeg/libvpx 依赖 POSIX shell + autotools）。首次使用运行一次：
> `powershell -ExecutionPolicy Bypass -File scripts\bootstrap-msys2.ps1`（会检查并安装以上 MSYS2 包；
> **不装** `mingw-w64-x86_64-ffmpeg`，因为它是 GPL 构建）。
> Linux/Ubuntu：`bash scripts/bootstrap-ubuntu.sh`。

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja mingw-w64-x86_64-glfw mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-aom mingw-w64-x86_64-libvpx \
  mingw-w64-x86_64-yasm mingw-w64-x86_64-nasm \
  make tar xz curl git
```

> **许可**：VidE 本体为 MIT（见根目录 `LICENSE`）。FFmpeg 由本项目**从固定版本源码构建为 LGPL 共享库**
> （`--disable-gpl --disable-nonfree`），以**动态链接**方式使用，故不影响 MIT，且可随发布包捆绑
> `av*.dll/.so`（用户可替换）。切勿改用 GPL 构建（含 x264/x265）。详见
> [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md)。

---

## 2. 构建参考软件静态库（helper 依赖）

所有第三方构建产物统一放在 **`build/deps/`**（`build/deps/ffmpeg`、`build/deps/hm`、`build/deps/vtm`、
`build/deps/aom`、`build/deps/vpx`），与主工程 `build/` 同一根，跨平台布局一致。

```powershell
# FFmpeg（固定版本下载 + SHA-256 校验 + LGPL 共享库构建）
powershell -ExecutionPolicy Bypass -File scripts\build-ffmpeg.ps1  # → build/deps/ffmpeg/{include,lib,bin}
# HEVC / VVC / AV1（CMake 工程）
powershell -ExecutionPolicy Bypass -File scripts\build-hm.ps1    # → build/deps/hm/lib/.../libTLib*.a
powershell -ExecutionPolicy Bypass -File scripts\build-vtm.ps1   # → build/deps/vtm/lib/.../lib*.a
powershell -ExecutionPolicy Bypass -File scripts\build-aom.ps1   # → build/deps/aom/libaom.a（含 aomenc/aomdec）
# VP9（libvpx 仅支持就地 make，构建后拷贝）
bash -l scripts/build-vpx.sh                                     # → build/deps/vpx/libvpx.a
```

FFmpeg 源码**不随仓库提交**：首次构建从 `ffmpeg.org` 下载 `ffmpeg-8.1.3.tar.xz` 并校验 SHA-256，
缓存在 `build/deps/ffmpeg-src/`。离线时可用 `-Tarball <path>`（Win）或 `VIDE_FFMPEG_TARBALL=<path>`（*nix）指定本地包。
`--enable-shared --disable-static --disable-gpl` 产出自包含的 LGPL 共享库。

`CONFIG_AV1_ENCODER=1` 以便同时构建 `aomenc`（生成与解码器匹配的测试流）。
HM/VTM 的 CMake 会把归档写回其源码树 `lib/`，脚本随后拷贝到 `build/deps/<x>/lib/`（主构建只看该处）。

> **重要**：VTM 必须以 `ENABLE_TRACING=OFF` 构建，且与 helper 编译定义一致，否则解码崩溃；
> 另需 `KEEP_PRED_AND_RESI_SIGNALS=1`（pred/resi）。HM/VTM/JM/libvpx/libaom 均含 VidE 钩子/兼容补丁
> （见 `external/patches/README.md`）；改动钩子后需重跑对应 `build-*`。

主构建会自动发现上述静态库并链接进对应 helper。

---

## 3. 配置与构建（Windows / MSYS2）

**一键构建**（自举：缺 MSYS2 用 winget 装，缺包自动 bootstrap；含三方库 FFmpeg/HM/VTM/libaom/libvpx，
并在最后打包 `build/dist/`）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-all.ps1            # 构建全部 + 打包 dist
powershell -ExecutionPolicy Bypass -File scripts\build-all.ps1 -RunTests  # 构建 + ctest + 打包
powershell -ExecutionPolicy Bypass -File scripts\build-all.ps1 -SkipDeps  # 复用已有 build/deps，仅构建主工程
powershell -ExecutionPolicy Bypass -File scripts\build-all.ps1 -NoPackage # 不打包 dist
powershell -ExecutionPolicy Bypass -File scripts\build-all.ps1 -NoBootstrap# 只检查依赖、不自动安装
```

也可单独运行 `scripts\bootstrap-msys2.ps1` 预装依赖。

Linux/macOS：`bash scripts/build-all.sh [--test] [--skip-deps] [--no-bootstrap]`
（Debian/Ubuntu 缺依赖会自动 `apt` 安装；`--no-bootstrap` 关闭）。

<details><summary>手动分步（可选）</summary>

```powershell
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=C:/msys64/mingw64 `
      -DVIDE_WITH_FFMPEG=ON -DVIDE_WITH_HEVC_HM=ON -DVIDE_WITH_VVC_VTM=ON `
      -DVIDE_WITH_AVC_JM=ON -DVIDE_WITH_VP9=ON -DVIDE_WITH_AV1=ON
cmake --build build
```

</details>

产物（`build/bin/`）：`vide.exe`、`vide_hevc_helper.exe`、`vide_vvc_helper.exe`、`vide_avc_helper.exe`、
`vide_vp9_helper.exe`、`vide_av1_helper.exe`、`vide_tests.exe`、`vide_integration.exe`。

**可分发包**：`powershell -File scripts\package.ps1 -IncludeFfmpeg` → `build/dist/`（exe + helper + **LGPL FFmpeg DLL** + 许可证）。
`build-all.ps1` 默认就用 `-IncludeFfmpeg` 打包（FFmpeg 是我们自己构建的 LGPL 版本，可安全捆绑）。
若单独打包且不带 `-IncludeFfmpeg`，则目标机需自备 FFmpeg。
详见 [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md)。

---

## 4. 运行

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run.ps1              # GUI
powershell -ExecutionPolicy Bypass -File scripts\run.ps1 --info clip.h265
powershell -ExecutionPolicy Bypass -File scripts\run.ps1 -tool vide_tests
# 手动：$env:PATH="C:\msys64\mingw64\bin;"+$env:PATH; .\build\bin\vide.exe
```

界面要点：
- 工具栏：**Add...**（可连续打开多路，R6）/ **Remove...**（仅移出列表，缓存保留）/ 流选择器 /
  叠加开关（Borders/Type/QP/Bits/MV）/ **Cache manager...**（缓存管理）。主窗口无菜单栏。
- 裸流：Add 对话框勾选 **Raw video (YUV/RGB)**，选格式并填宽高。
- 底部缩略图逐个点亮（FFmpeg 异步）；**每路一行**，单一横向滚动条；缩略图下方显示
  `d<POC> c<编码序> <I/P/B/IDR>`。点击缩略图选帧。
- 中央 **View** 下拉切换 **Reconstructed / Prediction / Residual**（**五个视频 codec 均支持**）。
- 左侧上方 **Frame**（帧级信息 + DPB 参考表）；左侧下方语法面板按结构分 Tab（SPS/PPS/Header/…；容器 wrapper 不显示）；
  中央图像下方 **Bitstream**（NAL 列表 + 十六进制转储 + 选中块字节高亮）；右侧上 **CTU View**（可点击选 CU），
  右侧下 **Block Info**（Block Info 表 + `<块组> syntax` 两个 Tab）。
- 首次打开某码流时 helper 后台顺序解码写缓存；完成后（或缓存命中）立即出大图与块信息。

### helper 路径解析
`vide.exe` 查找同名 helper exe（本程序目录/PATH），可用 `VIDE_<NAME>_PATH`（如 `VIDE_HEVC_HELPER_PATH`）覆盖。

---

## 5. 命令行（无界面）

```powershell
.\build\bin\vide.exe --info path\to\clip.mp4
.\build\bin\vide.exe --decode 10 frame10.ppm path\to\clip.mp4
.\build\bin\vide.exe --dump-syntax syntax.json path\to\clip.mp4
.\build\bin\vide.exe --dump-syntax syntax.json --syntax-frame 2 path\to\clip.mp4
.\build\bin\vide.exe --dump-blocks 0 blocks0.csv path\to\clip.h265   # 含 qp_source/ref/mv/bits/bit_offset/bit_length/slice_index
.\build\bin\vide.exe --dump-plane reco 0 reco.ppm path\to\clip.h265  # reco/pred/resi（五 codec）
.\build\bin\vide.exe --dump-plane pred 0 pred.ppm path\to\clip.h265
.\build\bin\vide.exe --dump-plane resi 0 resi.ppm path\to\clip.h265
.\build\bin\vide.exe --bench 100 path\to\clip.h265                   # open+decode 墙钟 + 缓存块读
.\build\bin\vide.exe --raw-format I420 --width 1920 --height 1080 --fps 30 raw.yuv

# 直接运行 helper（调试）
.\build\bin\vide_hevc_helper.exe stream.265 out.vpk
```

---

## 6. 缓存

- 位置：默认为**可执行文件同目录 `vide_cache/`**；覆盖顺序 `$VIDE_CACHE_DIR` > `$VIDE_TMP_DIR/vide_cache` > exe 目录 > 系统 temp。
- 文件：`<codec>_<指纹hash>.vpk`（单文件 v6：`Header | FrameData | Index | Footer`）。
- 指纹：`size + mtime` 快速键 + 全量 xxHash；命中且 `complete` 且版本一致 → 秒开。
- 写入中可读：helper 每写一帧即 `flush`；主进程 `VpkReader::refresh()` 增量读取。
- 容量：默认 40GB，**只淘汰 offline**；超限按 LRU 淘汰至 90%，只剩在用仍超限则中止该次解析。
- 管理：工具栏 **Cache manager...** 表格（codec/大小/时间/in-use）；过滤 + 逐个删 / 删离线 / 删全部
  （删在用或全部前会先关闭已打开码流以释放文件句柄）。
- 升级提示：`kVpkVersion` 变更会使旧 pack 失效并在下次打开时重建（重新解码），属正常。

---

## 7. 临时文件

- 抽取的 ES 写于缓存目录（`es_<pid>_<id>.265/.266/.ivf`），`Session::close` 时删除。
- scratch / 默认缓存基目录：`<repo>/tmp`（`$VIDE_TMP_DIR` 可覆盖）。

---

## 8. 构建选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `VIDE_BUILD_TESTS` | ON | 构建测试套件 |
| `VIDE_WITH_FFMPEG` | ON | FFmpeg 适配器（缩略图 / ES）；链接 `build/deps/ffmpeg`（需 `build-ffmpeg.*`） |
| `VIDE_WITH_HEVC_HM` | ON | HEVC helper（需 `build-hm.ps1`） |
| `VIDE_WITH_VVC_VTM` | ON | VVC helper（需 `build-vtm.ps1`） |
| `VIDE_WITH_AVC_JM` | ON | AVC/JM helper |
| `VIDE_WITH_VP9` | ON | VP9/libvpx helper |
| `VIDE_WITH_AV1` | OFF | AV1 helper / 头解析（链接 `build/deps/aom/libaom.a`；本项目 `build/` 用 ON 配置） |
| `VIDE_WITH_IMAGE` | OFF | JPEG/PNG（头语法已内建，此项预留） |
| `VIDE_DEPS_DIR` | `<repo>/build/deps` | 第三方构建产物统一目录（可覆盖） |

> CMake 会缓存选项值；切换到已有 `build/` 时需显式传参覆盖，或删 `CMakeCache.txt`。

---

## 9. Linux / macOS

```sh
bash scripts/bootstrap-ubuntu.sh   # 首次：apt 安装构建依赖（Debian/Ubuntu）
bash scripts/build-ffmpeg.sh       # FFmpeg 8.1.3 → build/deps/ffmpeg（下载+SHA256+LGPL shared）
bash scripts/build-hm.sh           # HM  → build/deps/hm/lib/**/libTLib*.a
bash scripts/build-vtm.sh          # VTM → build/deps/vtm/lib/**  （ENABLE_TRACING=OFF + 保留 pred/resi）
bash scripts/build-aom.sh          # libaom → build/deps/aom/libaom.a
bash scripts/build-vpx.sh          # libvpx → build/deps/vpx/libvpx.a

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

bash scripts/package-linux.sh      # → build/dist/VidE-linux-<arch>.tar.gz（含 LGPL FFmpeg .so）
bash scripts/run.sh --info clip.h265
```

依赖来自系统包（`build-essential`、`cmake`、`ninja-build`、`pkg-config`、`libglfw3-dev`、`libgl1-mesa-dev`、
`libaom-dev`、`nasm`、`yasm`、`curl`、`xz-utils`）。FFmpeg 由 `build-ffmpeg.sh` 从源码构建（**不需要**系统 `libav*-dev`）。
`scripts/build-all.sh` 会把上述步骤串起来一键构建。

---

## 10. 源码管理（Git）

```sh
git init -b main        # 已初始化
git add -A              # 只纳入源码；构建产物被 .gitignore 排除
git status
git commit -m "..."
```

- **忽略**：`/build`（含 `build/deps` 第三方构建）、`/tmp`、`/vide_cache`、CMake/编译产物（`*.o/*.a/*.exe/…`）、
  第三方**就地**构建产物（`external/HM/{bin,lib}`、`external/VTM/{bin,lib}`、`external/libvpx/{Makefile,config.*,
  libvpx*.a,vpx_config.*,build*}` 等）——见根 `.gitignore`。
- **提交**：`src/`、`tools/`、`tests/`、`scripts/`、`docs/`、`.github/`、根 `CMakeLists.txt`、`README.md`、
  `.gitignore`、`.gitattributes`，以及 **`external/`**（FFmpeg 除外的 HM/VTM/JM/libvpx/libaom/imgui 带钩子补丁源码；
  FFmpeg 源码在构建时下载，不入库）。
- **行尾**：`.gitattributes` 设 `* text=auto eol=lf`（脚本 `*.ps1/*.bat` 保留 CRLF），`external/** -diff`。
- **第三方更新**：`external/` 已 vendored（移除了嵌套 `.git`）；同步上游需手动比对并重打补丁
  （见 `external/patches/README.md`）。

---

## 11. 常见问题（排障）

- **Windows 没装 MSYS2 / 没有 winget**：手动安装 <https://www.msys2.org/>（默认 `C:\msys64`），
  再运行 `powershell -ExecutionPolicy Bypass -File scripts\bootstrap-msys2.ps1`。
- **winget 装完 MSYS2 仍报错**：重开一个终端再跑；必要时先 `C:\msys64\usr\bin\pacman.exe -Syu`。
- **pacman 签名 / keyring 报错**：
  `C:\msys64\usr\bin\bash.exe -lc "pacman-key --init && pacman-key --populate msys2 && pacman -Syu"`。
- **FFmpeg 下载失败 / 离线**：用 `-Tarball <路径>`（Windows）或 `VIDE_FFMPEG_TARBALL=<路径>`（*nix）
  指定本地 `ffmpeg-8.1.3.tar.xz`。
- **SHA-256 不匹配**：请使用官方 <https://ffmpeg.org/releases/> 的 8.1.3 包，勿用第三方重打包。
- **Ubuntu 缺依赖**：`bash scripts/bootstrap-ubuntu.sh`；或用 `--no-bootstrap` 时手动安装 §1 所列包。
- **CMake 缓存残留**（改过选项 / 切换过 FFmpeg 方式）：删除 `build/CMakeCache.txt` 或整个 `build/` 后重来。
- **libvpx `Permission denied`**：脚本已用 `sh ./configure` 规避（vendored configure 无执行位）。
- **路径含空格 / 中文**：建议放在纯英文路径；如遇异常请附日志提 issue。
