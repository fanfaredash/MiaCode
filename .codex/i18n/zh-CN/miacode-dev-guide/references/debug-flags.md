<!-- translation-source: .codex/skills/miacode-dev-guide/references/debug-flags.md -->
<!-- translation-source-hash: 60d4ad8013e225afcc94a46311479a5019052733c745ae7fc9150ac84e2a2e0e -->
<!-- 说明：这是中文镜像，不作为 Codex skill 入口加载。 -->

# 调试参数索引

这份文档用于集中记录全仓通用调试开关、启动计时日志、预览覆盖参数，以及仅导出链路使用的诊断环境变量。

## 1. 通用调试入口

- 公共 helper：
  - 文件：`src/common/DebugOptions.h`
  - 负责：truthy 环境变量解析、runtime debug CLI 别名、通用日志路径
- 运行时调试输出：
  - CLI 别名：`--debug-runtime`、`--miacode-debug`、`--enable-debug-output`
  - 入口：`MainWindow::configureRuntimeDebugOutput`
  - 效果：设置 `MIACODE_ENABLE_RUNTIME_DEBUG_OUTPUT=1`，并清空 `%TEMP%/miacode_runtime_debug.log`
  - 当前消费者：
    - `MainWindow::appendOutput`
    - `QtPreviewSfxRuntime::appendAudioDebugLog`
- 启动阶段计时：
  - 环境变量：`MIACODE_ENABLE_STARTUP_TIMING`
  - 兼容旧别名：`MAIMURI_ENABLE_STARTUP_TIMING`
  - 日志路径：`%TEMP%/miacode_startup_timing.log`
  - 当前写入方：
    - `src/app/main.cpp`
    - `src/app/mainwindow/MainWindow.cpp`
    - `src/preview/video/PreviewCanvas.cpp`

如果以后新增的是“全仓通用”的调试开关，优先接入 `src/common/DebugOptions.h`，不要再各处复制布尔解析。

## 2. 预览与运行时覆盖参数

- 旧版预览后端开关：
  - 环境变量：`MIACODE_ENABLE_PYGAME_PREVIEW`
  - 兼容旧别名：`MAIMURI_ENABLE_PYGAME_PREVIEW`
  - 所属：`src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp`
- 旧版预览 session 脚本路径覆盖：
  - 环境变量：`MIACODE_PREVIEW_SESSION_SCRIPT`
  - 兼容旧别名：`MAIMURI_PREVIEW_SESSION_SCRIPT`
  - 所属：`src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- 预览 SFX 目录覆盖：
  - 环境变量：`MIACODE_PREVIEW_SFX_DIR`
  - 兼容旧别名：`MAIMURI_PREVIEW_SFX_DIR`
  - 所属：`src/common/PreviewSfxAssets.h`
- 预览音轨路径覆盖：
  - 环境变量：`MIACODE_TRACK_PATH`
  - 兼容旧别名：`MAIMURI_TRACK_PATH`
  - 所属：`src/app/mainwindow/MainWindow.cpp`

这些参数虽然常用于调试，但仍然属于具体功能路径，应继续放在各自 owner 附近维护。

## 3. 编辑器性能分段日志

- 开关来源：
  - 与第 1 节相同的 runtime debug CLI 别名
- 日志路径：
  - `%TEMP%/miacode_runtime_debug.log`
- 当前编辑链路的性能标签：
  - `edit/metadata_perf`
  - `edit/muri_perf`
  - `edit/validation_perf`
  - `edit/validation_apply_perf`
- 主要 owner：
  - `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - `src/app/mainwindow/sections/validation/MainWindow.ValidationFlow.cpp`

这条日志主要用于定位“输入卡顿”一类问题。若后续继续扩展标签，尽量保持命名稳定，方便横向对比不同版本。

## 4. 仅导出链路使用的诊断与调参参数

这一组开关故意不并入通用 helper，因为它们只属于视频导出链路。

- 导出总日志：
  - `MIACODE_EXPORT_LOG_PATH`
- 重复帧 / 对比诊断：
  - `MIACODE_EXPORT_DIAG_REPEAT`
  - `MIACODE_EXPORT_DIAG_LOG_ALL_REPEATS`
  - `MIACODE_EXPORT_DIAG_MAX_LINES`
  - `MIACODE_EXPORT_DIAG_COMPARE_RENDER_PATHS`
  - `MIACODE_EXPORT_DIAG_COMPARE_RADIUS`
  - `MIACODE_EXPORT_DIAG_COMPARE_MAX_LINES`
  - `MIACODE_EXPORT_DIAG_COMPARE_LOG_THRESHOLD`
  - `MIACODE_EXPORT_DIAG_CROP_BOTTOM`
- 物件 / 管线追踪：
  - `MIACODE_EXPORT_DIAG_OBJECT_HASH`
  - `MIACODE_EXPORT_DIAG_OBJECT_DIFF_THRESHOLD`
  - `MIACODE_EXPORT_DIAG_OBJECT_TRACE`
  - `MIACODE_EXPORT_DIAG_OBJECT_TRACE_MAX_LINES`
  - `MIACODE_EXPORT_DIAG_PIPE_HASH`
  - `MIACODE_EXPORT_DIAG_PIPE_HASH_MAX_LINES`
  - `MIACODE_EXPORT_DIAG_RAW_DUMP_PATH`
- 导出渲染路径强制项：
  - `MIACODE_EXPORT_ENABLE_GPU_RENDER`
  - `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO`
  - `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO`
- 编码器探测与调参：
  - `MIACODE_EXPORT_SKIP_ENCODER_RUNTIME_PROBE`
  - `MIACODE_EXPORT_FORCE_ENCODER`
  - `MIACODE_EXPORT_ENCODER_MODE`
  - `MIACODE_EXPORT_ENCODER_THREADS`
  - `MIACODE_EXPORT_FILTER_THREADS`
  - `MIACODE_EXPORT_X264_PRESET`
  - `MIACODE_EXPORT_X264_CRF`
  - `MIACODE_EXPORT_X264_BFRAMES`
  - `MIACODE_EXPORT_X265_POOLS`
  - `MIACODE_EXPORT_X265_FRAME_THREADS`
  - `MIACODE_EXPORT_X265_LOOKAHEAD`
  - `MIACODE_EXPORT_X265_LOOKAHEAD_SLICES`
  - `MIACODE_EXPORT_X265_BFRAMES`
  - `MIACODE_EXPORT_X265_WPP`
  - `MIACODE_EXPORT_X265_PMODE`
  - `MIACODE_EXPORT_X265_PME`
- 主 owner：
  - `src/tools/video_export/VideoExportController.cpp`
- 会预置一组常见导出诊断参数的脚本：
  - `scripts/export_and_analyze_duplicates.py`

## 5. 与调试常配套的辅助工具

- 辅助可执行工具：
  - `miacode_muri_dump`
  - `simai_native_dump`
  - `soundtouch_probe`
- 常用分析脚本：
  - `scripts/analyze_ffmpeg_chain_variants.py`
  - `scripts/analyze_video_duplicate_frames.py`
  - `scripts/compare_log_vs_video_trajectory.py`
  - `scripts/export_and_analyze_duplicates.py`

当新增调试开关、修改日志路径，或某条功能链路的诊断参数已经多到不适合只靠 grep 时，都要同步更新这份索引。
