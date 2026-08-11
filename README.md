<p align="center">
  <img src="resources/icons/app.png" alt="MiaCode avatar" width="128">
</p>

# MiaCode

[中文](README.md) | [English](README_EN.md)

MiaCode 是一个面向 simai 谱面创作的桌面编辑器、预览器与导出工具。项目基于 Qt 6 / CMake 构建，核心工作流覆盖文本编辑、谱面校验、时间轴预览、实时播放、音视频同步辅助以及谱面视频导出。

## 功能概览

- simai 文本编辑、语法高亮、查找替换与多难度字段管理
- 谱面解析、语法校验、问题列表与跳转定位
- 原生时间轴视图，包含波形、缩放、播放线、光标线与谱面对象预览
- Qt Quick 实时预览与离屏导出管线
- tap、hold、slide、wifi、touch、touch-hold、mine、break touch 等对象预览
- BPM / offset / 播放延迟辅助检测
- Muri 检测与谱面诊断工具
- 谱面视频导出、片段导出、批量导出与 ZIP 打包辅助
- 本地资源、皮肤、音效、背景图片/视频与片头模板支持

## 自建内容

MiaCode 包含大量项目内实现，而不是只把外部工具拼在一起：

- simai 文档模型、解析、校验与批量变换逻辑
- 时间轴数据源、绘制与编辑器联动
- 预览场景状态、Qt Quick 渲染层与导出快照管线
- 音频预览、SFX 时间线、延迟检测与导出音频计划
- Muri 分析、谱面诊断和开发者辅助工具
- Windows 构建、依赖准备和打包脚本

MiaCode 自有源代码使用 MIT License；仓库整体、随仓库分发的资源和发布包定位为非商业使用。许可证边界见 [LICENSE_SCOPE.md](LICENSE_SCOPE.md)，第三方库、资源和参考项目的说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 构建

### 依赖

- CMake 3.21+
- C++20 编译器
- Qt 6.8+，需要 `Core`、`Gui`、`Widgets`、`Network`、`OpenGL`、`Qml`、`Quick`、`QuickControls2`、`ShaderTools`、`Multimedia`、`Svg`
- Windows：Visual Studio 2022 / MSVC；导出用 FFmpeg 和 QtAVPlayer 预览解码用 FFmpeg dev SDK 由脚本准备
- macOS：QtAVPlayer 预览解码需要 FFmpeg dev SDK；`package-mac.sh` 会优先使用
  `MIACODE_FFMPEG_DEV_DIR`，否则查找 Homebrew 的 `ffmpeg@6`（再回退到 `ffmpeg`）

更详细的打包说明见 [scripts/README.md](scripts/README.md)。

### Windows

推荐使用一键脚本自动安装 Qt、准备依赖、构建并打包：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1
```

如果本机已经安装 Qt，也可以使用 CMake preset。此路径需要先准备 FFmpeg 运行文件和 dev SDK：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\ffmpeg\ensure-windows-ffmpeg.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\ffmpeg\ensure-windows-ffmpeg-dev.ps1
cmake --preset vs2022-qt6
cmake --build --preset release
.\build\Release\MiaCode.exe
```

CMake preset 路径要求 Qt 能被 CMake 找到，或通过 `CMAKE_PREFIX_PATH` 指向 Qt 根目录。Windows 下还要求 `third_party/ffmpeg/windows/ffmpeg.exe` 与 `third_party/ffmpeg/windows/dev/` 已存在；上面两个脚本会下载固定版本并校验必需文件。macOS 本地配置则传入 `-DMIACODE_FFMPEG_DEV_DIR=<包含 include/ 和 lib/ 的 SDK 根目录>`，或使用 `package-mac.sh` 的 Homebrew 自动发现。

已有构建产物时，可以单独打包：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\package-win.ps1 -QtRoot <QtRoot>
```

## 仓库结构

- [src](src)：应用源码、核心模型、预览、音频、导出和工具实现
- [assets](assets)：运行资源、皮肤、音效、背景素材与生成数据
- [resources](resources)：Qt resource collection 与应用图标
- [scripts](scripts)：构建、依赖准备、打包和维护脚本
- [third_party](third_party)：随仓库 vendored 或引用的第三方依赖
- [docs](docs)：架构、调试、导出、时间轴和开源准备文档
- [samples](samples)：示例谱面和验收材料

## 发布

当前 release 包由维护者在本地使用脚本生成；废弃的 GitHub Actions 已移除。发布前检查项见 [docs/ops/RELEASE_CHECKLIST.md](docs/ops/RELEASE_CHECKLIST.md)，开源前剩余确认项见 [docs/ops/OPEN_SOURCE_CHECKLIST.md](docs/ops/OPEN_SOURCE_CHECKLIST.md)。

## 许可证与鸣谢

MiaCode 自有源代码使用 MIT License，见 [LICENSE](LICENSE)。仓库整体、随仓库分发的资源、打包产物和发布包定位为非商业使用；具体边界见 [LICENSE_SCOPE.md](LICENSE_SCOPE.md)。第三方库、字体、音效、图片、FFmpeg、BASS、Qt 以及参考实现可能有各自的许可证或分发限制，请以 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 为准。

感谢 [Minepig/MaiMuriDX](https://github.com/Minepig/MaiMuriDX) 等项目提供的 simai 解析、预览和工程实现参考。感谢 [gfdfdxc/maimai-transition](https://github.com/gfdfdxc/maimai-transition) 提供片头参考。感谢 [Majdata Net](https://majdata.net/) 提供社区谱面下载，感谢 [MaiViewer](https://www.maiviewer.net/) 提供官方谱面 simai 抄谱参考。

特别感谢 hitomi 老师无偿提供 MiaCode logo 绘制。

感谢内部测试时期给出建议、复现问题和协助调试的朋友们，名单见 [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md)。

## 更新日志

历史更新记录已移至 [CHANGELOG.md](CHANGELOG.md)。

## 社群

QQ 群：1095435375

<p align="center">
  <img src="resources/community/qq-group.png" alt="MiaCode QQ 群二维码" width="360">
</p>
