# Scripts

[中文](README.md) | [English](README_EN.md)

本目录只保留可公开复现的构建、发布、资产和诊断脚本。一次性分析脚本和旧 A/B 诊断入口保留为维护者本地工具，不进入公开跟踪。

## 目录结构

| 目录 | 内容 |
|---|---|
| `build/` | Windows/macOS 构建、打包与出包校验入口 |
| `debug/` | Windows/macOS 调试/诊断启动入口 |
| `ffmpeg/` | FFmpeg 运行时、开发 SDK 获取脚本，以及 decode-only 裁剪工具链 |
| `assets/` | 资产生成和字体裁剪辅助脚本 |

## 构建与打包

Windows:

```powershell
# MSVC x64：生成器由 vswhere 解析，需已装 Visual Studio 与 Windows SDK
# x64 默认用 ffmpeg/trim 从源码构建 decode-only 预览 SDK（体积从约 150 MB 降到约 20 MB，需 MSYS2）；
# 跳过裁剪加 -SkipTrim
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1 -Toolchain msvc -BuildDir build-msvc

# MSVC arm64：原生 arm64 主机，走 ARM64 平台参数与 Qt 的 arm64 包
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1 -Toolchain msvc-arm64 -BuildDir build-msvc-arm64

# MinGW + Ninja：Qt 用 mingw_64 版
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1 -Toolchain mingw -BuildDir build

# 构建已完成时单独打包（-Arch 与构建时一致）
powershell -ExecutionPolicy Bypass -File .\scripts\build\package-win.ps1 -QtRoot <QtRoot> -BuildDir <BuildDir> -Arch x64

# 出包后校验
powershell -ExecutionPolicy Bypass -File .\scripts\build\verify-win-package.ps1 -DistDir .\dist\MiaCode-v<version>-win64 -Arch x64
```

`build-win.ps1` 依次执行：Python 依赖（仅 py7zr）、导出用 `ffmpeg.exe`、预览用 FFmpeg
dev SDK（x64 走裁剪构建，`-SkipTrim` 关闭）、Qt 定位、CMake 配置与构建、调用 `package-win.ps1` 打包。两条 FFmpeg 链与打包
链都按 `-Toolchain` 推导出的目标架构选择各自的目录：`third_party\ffmpeg\windows\<win64|winarm64>\`
与 `third_party\bass\bin\<win64|winarm64>\`，包名分别为 `MiaCode-v<version>-win64` 与
`MiaCode-v<version>-winarm64`。arm64 不随包分发 `bass_aac.dll`（上游只提供 x86/x64 版本），
音频后端对缺失插件已有存在性检查。

Qt 定位顺序：`-QtRoot` → `C:\Qt\<版本>\<架构目录>` → `.qt\<版本>\<架构目录>` →
`QT_ROOT_DIR`/`Qt6_DIR` 环境变量 → 都未命中时由 `build/provision-qt.ps1` 从 Qt 仓库下载。
后者自适应上游两种仓库目录布局（6.11 前的扁平目录与 6.11 起的按架构分目录），每个归档按
发布方 `.sha1` 校验，不依赖 aqtinstall。

`package-win.ps1` 在需要时自动重建 `MiaCode` 与 `MiaCodeLauncher`，把当前工具链的 C++
运行库与裁切后的 FFmpeg 运行库放入 `app/`，按内容契约断言必需项与禁止项，产出 7z 归档；
单配置生成器下还会校验 `CMAKE_BUILD_TYPE` 与 `-Config` 一致。

`verify-win-package.ps1` 校验内容契约、FFmpeg 裁切一致性（包内 av*.dll 与 dev SDK 逐字节
哈希比对）、全包导入解析、启动冒烟与归档，任一失败退出码为 1。

预览用 FFmpeg SDK 可由 `ffmpeg/trim/build-trimmed-ffmpeg.ps1` 产出裁切版，替代
`ffmpeg/ensure-windows-ffmpeg-dev.ps1` 下载的全量 SDK。

Qt 版本与模块、三套工具链定义（含各自的目标架构、Qt 主机仓库树、MSVC 运行库架构子目录）、
包内容清单、归档格式都在 `build/windows-toolchain.psd1`，调整这些行为改该文件。不同工具链
使用各自的构建目录，生成器与目标架构不同无法共用。

macOS:

本地出包，复用机器上已装的 Qt、FFmpeg dev SDK 与导出用 `ffmpeg`：

```bash
bash scripts/build/build-macos-local.sh
```

CI 出包，先在干净环境安装 Qt 6.10.2 与两样 FFmpeg，再执行打包：

```bash
bash scripts/build/build-macos-ci.sh
```

两个入口都把 Release 构建与包装配交给 `package-mac.sh`；需要手工控制时可直接
调用它，例如 `QT_ROOT="$HOME/Qt/6.10.2/macos" bash scripts/build/package-mac.sh`。
CI 入口读 `QT_VERSION`、`QT_MODULES`、`MIACODE_PYTHON_VENV_DIR`；本地入口读
`QT_ROOT`（按 `QT_VERSION` 依次探测 `.qt/` 与 `$HOME/Qt/`）、`BUILD_DIR` 与
`MIACODE_FFMPEG_DEV_DIR`。设置 `MIACODE_PACKAGE_CHANNEL` 会在包名里插入渠道段。

输出位于 `dist/`。指定单一 `arm64` 或 `x86_64` 架构时，打包流程会在
`macdeployqt` 后裁掉 Qt Framework/插件中的另一架构切片，并在重新签名前验证
包内所有 Mach-O 均包含且只包含目标架构。可设置
`MIACODE_THIN_MACOS_APP=OFF` 生成保留 Qt universal 二进制的对照包。

macOS 的 QtAVPlayer 预览解码还需要 FFmpeg dev SDK，位于仓库本地的
`third_party/ffmpeg/macos/dev/`，用 `bash scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh`
生成（已存在且校验通过时直接复用）；打包仅复制其中必需的六个 dylib，
不会查找或复制 Homebrew 依赖。也可用 `MIACODE_FFMPEG_DEV_DIR` 显式指定兼容 SDK。

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
