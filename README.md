# VidE

VidE 是一款开源的视频码流分析工具：解析并可视化
编码码流的完整语法、块级信息（类型 / QP / bits / 运动矢量），以及重建 / 预测 / 残差图像。

- **视频编码**：AVC (H.264)、HEVC (H.265)、VVC (H.266)、VP9、AV1
- **图像**：JPEG、PNG
- **原始视频**：I420、NV12、NV21、YUV422P、YUV400、YUV444P、P010、P012、P016、RGB888

## 文档

| 文档 | 内容 |
|---|---|
| [`docs/03_build_guide.md`](docs/03_build_guide.md) | **构建与运行指南** |
| [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) | 第三方组件与许可 |

## 前置条件

| 平台 | 需要 |
|---|---|
| Windows | Windows 10 1809+ / PowerShell 5.1。有 **winget** 时脚本自动安装 MSYS2；否则先手动装一次 [MSYS2](https://www.msys2.org/) |
| Ubuntu / Debian | 可 `sudo`（脚本用 `apt` 安装依赖） |
| 通用 | 首次构建需联网（从 ffmpeg.org 下载源码 ~12MB）；磁盘 ≥ 6GB；首次约 15–40 分钟 |

> 依赖检查 + 自动安装由 `build-all` 完成；关闭：Windows `-NoBootstrap`，Linux `--no-bootstrap`。
>
> **验证状态**：Windows 已在本机用**干净源码树**完整验证（编译 + `ctest` 2/2 + `build/dist` 可运行）；
> Ubuntu 脚本与 CI 已就绪，**尚未在真机 Ubuntu 验证**（推送 tag 后由 GitHub Actions 自动验证）。

## 快速开始（一步构建）

第三方参考软件源码已随仓库 vendored 在 `external/`（**无需下载**）。FFmpeg 例外：构建时从 ffmpeg.org
**下载固定版本（8.1.3）并校验 SHA-256**，构建为 LGPL 共享库（不入库）。

**Windows**（需 MSYS2，详见 [`docs/03_build_guide.md`](docs/03_build_guide.md) §1）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-all.ps1 -RunTests
```

`build-all.ps1` **默认自举**：缺 MSYS2 会用 winget 安装，缺 MSYS2 包会自动跑 `bootstrap-msys2.ps1`；
`-NoBootstrap` 可关闭（仅检查并提示）。也可先手动 `scripts\bootstrap-msys2.ps1`。

**Linux / Ubuntu**：

```bash
bash scripts/build-all.sh --test
```

`build-all.sh` 在 Debian/Ubuntu 上**缺依赖会自动 `apt` 安装**（走 `bootstrap-ubuntu.sh`，需 sudo）；
`--no-bootstrap` 可关闭。也可先手动 `bash scripts/bootstrap-ubuntu.sh`。

产物：

| 路径 | 内容 |
|---|---|
| `build/deps/{ffmpeg,hm,vtm,aom,vpx}` | 第三方库（FFmpeg 为 LGPL 共享库；其余静态库） |
| `build/bin/` | `vide.exe` + `vide_<codec>_helper.exe` + 测试 |
| `build/dist/` | **可直接运行的分发包**（`vide.exe` + helper + 依赖 DLL + LGPL FFmpeg，双击即用） |

```powershell
.\build\dist\vide.exe                                             # 直接运行分发版
powershell -ExecutionPolicy Bypass -File scripts\run.ps1          # 运行开发版
```

> `build-all.ps1` 默认最后一步会打包出 `build/dist\`；加 `-NoPackage` 可跳过。

Linux：`bash scripts/build-all.sh` 后会得到 `build/bin/vide`；`bash scripts/package-linux.sh` 产出
`build/dist/VidE-linux-<arch>.tar.gz`。

<details>
<summary>手动分步构建（可选，与 build-all 等价）</summary>

```powershell
# 1) 参考软件静态库（产物统一在 build/deps/）
powershell -ExecutionPolicy Bypass -File scripts\build-hm.ps1      # HM
powershell -ExecutionPolicy Bypass -File scripts\build-vtm.ps1     # VTM
powershell -ExecutionPolicy Bypass -File scripts\build-aom.ps1     # libaom
bash -l scripts/build-vpx.sh                                        # libvpx

# 2) 主构建
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=C:/msys64/mingw64 `
      -DVIDE_WITH_FFMPEG=ON -DVIDE_WITH_HEVC_HM=ON -DVIDE_WITH_VVC_VTM=ON `
      -DVIDE_WITH_AVC_JM=ON -DVIDE_WITH_VP9=ON -DVIDE_WITH_AV1=ON
cmake --build build
```

</details>

## 用法

GUI：工具栏 **Add...** 打开码流（可多开；含 Raw）；**View** 切换重建 / 预测 / 残差；叠加开关
Borders/Type/QP/Bits/MV；点击缩略图选帧；**Cache manager...** 管理缓存。

CLI（完整参数见 `docs/03` §5）：

```powershell
vide.exe --info clip.mp4
vide.exe --dump-syntax syntax.json clip.mp4
vide.exe --dump-blocks 0 blocks0.csv clip.h265
vide.exe --dump-plane reco 0 reco.ppm clip.h265
vide.exe --bench 100 clip.h265
```

## 许可

VidE 本体以 **MIT License** 发布，见 [`LICENSE`](LICENSE)——可免费使用、修改、分发。

FFmpeg 以 **动态链接**方式使用（LGPL v2.1+），故不影响 VidE 的 MIT 授权；其余第三方组件均为宽松许可。
详见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。

## 反馈与 Issue

欢迎使用 VidE！遇到 bug、想要的新功能或有任何建议，都欢迎到
[Issues](https://github.com/wonsea/VidE/issues) 反馈，也欢迎直接提 Pull Request。

提交 issue 时建议附上：操作系统、VidE 版本 / commit、码流信息（codec / 分辨率）、复现步骤与日志。
