<!-- translation-source: .codex/skills/miacode-dev-guide/references/assets-and-tools.md -->
<!-- translation-source-hash: a6b3e4463bb433930719a30894d0d7d3f624f340bd684455294bf5aeb77d6098 -->
<!-- 说明：这是中文镜像，不作为 Codex skill 入口加载。 -->

# 资源与工具

这份文件用于记录资源查找规则、谱面目录约定、脚本、辅助可执行工具以及打包进去的外部工具。

## 1. 资源根与仓库资源区域

- 共享资源根解析：
  - 文件：`src/common/AssetPaths.h`
  - 函数：`findAssetRoot`、`assetPath`
- 仓库内主要资源区域：
  - `assets/skin`
  - `assets/SFX`
  - `assets/background`
  - `assets/noteguide`
  - `assets/generated`
  - `assets/fonts`
- Qt 资源：
  - `resources/app_icons.qrc`
  - `resources/fonts.qrc`
  - `resources/icons/*`

## 2. 谱面目录附近的运行时文件约定

当前约定：

- 谱面文本文件：`maidata.txt`
- 音乐轨：`track.mp3`
- 背景媒体候选：
  - `bg.mp4`
  - `pv.mp4`
  - `bg.jpg`
  - `bg.png`
  - `bg.jpeg`
- 项目渲染状态 sidecar：
  - `.miacode_render_settings.json`

这些约定一旦变化，代码和这份文档都要一起更新。

## 3. 资源消费者

- 皮肤纹理：
  - 消费者：`PreviewCanvas`
  - 入口：`MainWindow::resolvePreviewSkinDir`、`PreviewCanvas::setSkinDirectory`
- SFX 音效：
  - 消费者：`QtPreviewSfxRuntime`、导出侧 SFX 混音
  - 入口：`miacode::preview_sfx::resolveSfxDirectory`
- 背景 outline 与辅助背景图：
  - 消费者：预览与导出的 overlay 合成
  - 当前引用包括 `background/outline.png` 与 `background/outline_2.png`
- 生成的 slide 数据：
  - 存放在 `assets/generated`
  - 应把它当作运行时输入数据，而不是普通装饰资源

## 4. SFX 命名约定

当前 canonical 映射定义在 `src/common/PreviewSfxAssets.h`。

重要 kind 包括：

- `answer`
- `judge`
- `judge_break`
- `slide`
- `break`
- `ex`
- `touch`
- `touchhold`
- `firework`

不要随意改音效文件名；预览和导出两边都依赖这些约定。

## 5. 构建与打包脚本

- Windows 构建/打包：
  - `scripts/build-win.ps1`
  - `scripts/package-win.ps1`
- macOS 构建/打包：
  - `scripts/build-macos.sh`
  - `scripts/package-mac.sh`
- ffmpeg 准备：
  - `scripts/ensure-windows-ffmpeg.ps1`
  - `scripts/ensure-macos-ffmpeg.sh`
- 脚本文档：
  - `scripts/README.md`
  - `scripts/README_EN.md`

## 6. 分析与调试脚本

当前仓库内的辅助脚本包括：

- `scripts/analyze_ffmpeg_chain_variants.py`
- `scripts/analyze_video_duplicate_frames.py`
- `scripts/compare_log_vs_video_trajectory.py`
- `scripts/export_and_analyze_duplicates.py`
- `scripts/calc_hold_crop_ratio.py`

这些是开发辅助工具，不是运行时依赖。如果某个调试流程开始长期依赖其中某个脚本，要把它的用途持续记录在这里。

## 7. 开发辅助二进制

它们在 `CMakeLists.txt` 中由 `MIACODE_BUILD_DEV_TOOLS` 控制：

- `miacode_muri_dump`
- `simai_native_dump`
- `soundtouch_probe`
- `simai_parser_spec`
- `chart_batch_transform_spec`

一旦这些工具的作用范围变化，要同步更新这里以及相关的打包假设。

## 8. ffmpeg 打包契约

- 固定版本二进制说明在 `third_party/ffmpeg/README.md`。
- 导出运行时会从应用目录和仓库回退路径中解析 `ffmpeg`。
- 打包脚本会把固定版本的 `ffmpeg` 拷贝进发行产物。

如果升级 ffmpeg，要一起检查：

- `third_party/ffmpeg/README.md`
- `scripts/ensure-windows-ffmpeg.ps1`
- `scripts/ensure-macos-ffmpeg.sh`
- 打包脚本
- 所有提到版本假设的导出文档

## 9. 在这些情况下更新本文件

- 新增了资源目录
- 文件名约定变化
- 新增了必须打包的外部二进制
- 某个辅助脚本变成日常维护流程的一部分
- 代码里的资源查找顺序发生变化
