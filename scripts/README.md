# Scripts

[中文](README.md) | [English](README_EN.md)

本目录只保留可公开复现的构建、发布、资产和诊断脚本。一次性分析脚本和旧 A/B 诊断入口保留为维护者本地工具，不进入公开跟踪。

## 目录结构

| 目录 | 内容 |
|---|---|
| `build/` | Windows/macOS/Linux 构建与打包入口 |
| `debug/` | Windows/macOS 调试/诊断启动入口 |
| `ffmpeg/` | FFmpeg 运行时、开发 SDK 获取脚本，以及 decode-only 裁剪工具链 |
| `assets/` | 资产生成和字体裁剪辅助脚本 |

## 构建与打包

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\package-win.ps1 -QtRoot <QtRoot>
```

`package-win.ps1` 默认使用 `build/`，会检查版本头和 `MiaCode.exe` 是否需要刷新；若可执行文件缺失、版本过期或时间戳落后，会自动构建 `MiaCode`。

macOS:

```bash
bash scripts/build/build-macos.sh
```

```bash
QT_ROOT="$HOME/Qt/6.10.2/macos" CMAKE_OSX_ARCHITECTURES=arm64 bash scripts/build/package-mac.sh
```

输出位于 `dist/`。指定单一 `arm64` 或 `x86_64` 架构时，打包流程会在
`macdeployqt` 后裁掉 Qt Framework/插件中的另一架构切片，并在重新签名前验证
包内所有 Mach-O 均包含且只包含目标架构。可设置
`MIACODE_THIN_MACOS_APP=OFF` 生成保留 Qt universal 二进制的对照包。

macOS 的 QtAVPlayer 预览解码还需要 FFmpeg dev SDK。先运行
`bash scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh`，以生成仓库本地的
`third_party/ffmpeg/macos/dev/` 固定 FFmpeg 6 SDK；打包仅复制其中必需的六个 dylib，
不会查找或复制 Homebrew 依赖。也可用 `MIACODE_FFMPEG_DEV_DIR` 显式指定兼容 SDK。

Linux AppImage（x86_64、Release、默认并发 8，需要 Docker 或 Podman）：

```bash
bash scripts/build/build-linux.sh
```

脚本每次清空 `dist/`，然后生成
`dist/MiaCode-v<version>-linux-x86_64/` 和同名 `tar.gz`。版本目录内的
`MiaCode.AppImage` 包含 Qt、C++ 运行库、XCB 输入法插件、固定版本 FFmpeg 和其他
运行时文件；README、许可证和声明文件与 AppImage 并列放置。脚本自动选择 Docker
或 Podman，也可通过 `MIACODE_CONTAINER_ENGINE` 指定。构建使用 Ubuntu 22.04 /
Qt 6.11.1 基线，增量编译、打包中间文件和下载缓存统一放在 `build/`。运行时使用
X11/XWayland，并依赖系统的 glibc 2.35+、图形驱动和 X11 运行库。

## 其他脚本

- `debug/Start_MiaCode_Debug.bat`：发布包内唯一 Windows 调试启动入口。
- `debug/Start_MiaCode_Debug.command`：发布包根目录内的 macOS 调试启动入口；双击后以 `--debug` 启动 `MiaCode.app`，并将日志写入发布包根目录的 `logs/`。
- `debug/Start_MiaCode_SoftwareVideoDecode.bat`、`debug/Start_MiaCode_QtPluginDiag.bat`：公开保留的支持诊断入口，不随 Windows 发布包分发。
- `ffmpeg/ensure-windows-ffmpeg.ps1`、`ffmpeg/ensure-macos-ffmpeg.sh`、`ffmpeg/ensure-linux-ffmpeg.sh`：获取导出用独立 `ffmpeg`。
- `ffmpeg/ensure-macos-ffmpeg-dev.sh`：构建 macOS QtAVPlayer 预览解码用的固定 FFmpeg 6 SDK。
- `ffmpeg/ensure-windows-ffmpeg-dev.ps1`：获取 Windows QtAVPlayer 预览解码开发 SDK。
- `ffmpeg/trim/`：构建 Windows decode-only FFmpeg dev SDK 的裁剪工具链。
- `assets/subset_hud_font.py`：HUD 字体子集生成，详见 `assets/README_font_subset.md`。
- `assets/gen_same_lane_v_slides.py`：生成同轨 V 型 slide 参考数据。
