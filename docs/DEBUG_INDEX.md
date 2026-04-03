# 调试索引

本文档是 MiaCode 调试模式、日志文件和诊断环境变量的统一索引。

## 1. 调试模式入口

- 推荐命令行参数：`--debug`
- Windows 发布包启动脚本：`Start_MiaCode_Debug.bat`

行为说明：

- `--debug` 会打开调试模式。
- 调试模式下，runtime、audio、export、startup timing、preview profile 等调试输出默认开启。
- 启动时会清空本次会话的 runtime、audio、export、preview profile 日志。
- fatal 日志单独保留，不会因为普通调试会话重置而丢失。

旧命令行别名 `--debug-runtime`、`--miacode-debug`、`--enable-debug-output` 不再作为推荐入口。

## 2. 日志分层

默认日志目录解析顺序：

1. 各 channel 的路径覆盖环境变量
2. `MIACODE_LOG_DIR`
3. 系统临时目录

当前日志层：

- Runtime：`miacode_runtime_debug.log`
  - 仅在 debug 模式下写入
  - 主要来源：`MainWindow`、preview runtime、validation 性能汇总
- Audio：`miacode_audio_debug.log`
  - 仅在 debug 模式下写入
  - 主要来源：`QtPreviewSfxRuntime`、`soundtouch_probe`
- Export：`miacode_video_export.log`
  - 仅在 debug 模式下写入
  - 主要来源：`VideoExportController`
- Startup timing：`miacode_startup_timing.log`
  - 仅在 debug 模式下写入
  - 默认开启，可按类别关闭
- Fatal：`miacode_fatal.log`
  - 始终允许写入
  - 用于驱动不可用、黑屏、预览/导出初始化失败、worker 异常终止等致命问题
- Preview profile：`miacode_preview_profile_summary.txt`
  - 仅在 debug 模式下写入
  - 用于预览 profiling 汇总

各 channel 路径覆盖环境变量：

- `MIACODE_RUNTIME_LOG_PATH`
- `MIACODE_AUDIO_LOG_PATH`
- `MIACODE_EXPORT_LOG_PATH`
- `MIACODE_STARTUP_LOG_PATH`
- `MIACODE_FATAL_LOG_PATH`
- `MIACODE_PREVIEW_PROFILE_PATH`

## 3. 通用调试环境变量

主开关：

- `MIACODE_LOG_DIR`
  - 未单独覆写路径时的统一日志目录

按类别关闭的环境变量：

- `MIACODE_DISABLE_RUNTIME_DEBUG_OUTPUT`
  - 关闭 runtime 调试输出
- `MIACODE_DISABLE_AUDIO_DEBUG_OUTPUT`
  - 关闭 audio 调试输出
- `MIACODE_DISABLE_EXPORT_DEBUG_OUTPUT`
  - 关闭 export 调试输出
- `MIACODE_DISABLE_PREVIEW_PROFILE_OUTPUT`
  - 关闭 preview profile 汇总文件输出
- `MIACODE_DISABLE_STARTUP_TIMING`
  - 关闭 startup timing 日志
- `MIACODE_DISABLE_GL_DEBUG_MESSAGES`
  - 关闭 OpenGL driver debug message 输出

规则：

- 这些 `MIACODE_DISABLE_*` 变量只在 debug 模式下生效。
- 不开 `--debug` 时，普通调试输出不会因为这些变量而被单独打开。
- fatal 日志不受这些 disable 开关影响。

## 4. 预览与运行时覆盖项

以下环境变量属于功能覆盖或路径覆盖，不属于通用 debug 输出关闭项：

- `MIACODE_ENABLE_PYGAME_PREVIEW`
  - 旧预览后端切换
- `MIACODE_PREVIEW_SESSION_SCRIPT`
  - 预览会话脚本覆盖
- `MIACODE_PREVIEW_SFX_DIR`
  - 预览 SFX 目录覆盖
- `MIACODE_TRACK_PATH`
  - 预览轨道路径覆盖

## 5. Runtime 性能标签

当前 editor 性能标签主要写入 runtime 日志：

- `edit/metadata_perf`
- `edit/muri_perf`
- `edit/validation_perf`
- `edit/validation_apply_perf`

高噪声的布局迭代日志和文件对话框轮询日志已经做了裁剪。调试模式默认更偏向保留摘要、状态变化和失败点，而不是逐轮中间态。

## 6. 导出诊断环境变量

主入口：`src/tools/video_export/VideoExportController.cpp`

这类环境变量不是通用“debug 输出关闭项”，而是导出诊断/比对/调优开关，保留原有用途。

重复帧与摘要诊断：

- `MIACODE_EXPORT_DIAG_REPEAT`
- `MIACODE_EXPORT_DIAG_LOG_ALL_REPEATS`
- `MIACODE_EXPORT_DIAG_CROP_BOTTOM`
- `MIACODE_EXPORT_DIAG_MAX_LINES`

对象层诊断：

- `MIACODE_EXPORT_DIAG_OBJECT_HASH`
- `MIACODE_EXPORT_DIAG_OBJECT_TRACE`
- `MIACODE_EXPORT_DIAG_OBJECT_TRACE_MAX_LINES`
- `MIACODE_EXPORT_DIAG_OBJECT_DIFF_THRESHOLD`

渲染路径对比：

- `MIACODE_EXPORT_DIAG_COMPARE_RENDER_PATHS`
- `MIACODE_EXPORT_DIAG_COMPARE_RADIUS`
- `MIACODE_EXPORT_DIAG_COMPARE_MAX_LINES`
- `MIACODE_EXPORT_DIAG_COMPARE_LOG_THRESHOLD`

Pipe 诊断：

- `MIACODE_EXPORT_DIAG_PIPE_HASH`
- `MIACODE_EXPORT_DIAG_PIPE_HASH_MAX_LINES`

原始帧导出：

- `MIACODE_EXPORT_DIAG_RAW_DUMP_PATH`

默认规则：

- `MIACODE_EXPORT_DIAG_MAX_LINES` 是 compare 和 pipe-hash 的共享默认行数上限。
- `MIACODE_EXPORT_DIAG_OBJECT_TRACE_MAX_LINES` 默认取 `max(MIACODE_EXPORT_DIAG_MAX_LINES, 5000)`。
- 如果没有打开 `MIACODE_EXPORT_DIAG_REPEAT=1`，大部分逐帧导出诊断不会展开。

## 7. 导出后端与编码控制

渲染后端：

- `MIACODE_EXPORT_ENABLE_GPU_RENDER`
- `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO`
- `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO`

编码器选择：

- `MIACODE_EXPORT_SKIP_ENCODER_RUNTIME_PROBE`
- `MIACODE_EXPORT_FORCE_ENCODER`
- `MIACODE_EXPORT_ENCODER_MODE`
  - 可选：`balanced`、`compatibility`、`hardware`

线程和 x264 调优：

- `MIACODE_EXPORT_ENCODER_THREADS`
- `MIACODE_EXPORT_FILTER_THREADS`
- `MIACODE_EXPORT_X264_PRESET`
- `MIACODE_EXPORT_X264_CRF`
- `MIACODE_EXPORT_X264_BFRAMES`

冲突处理：

- `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1` 优先级高于 `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO=1`
- `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO=1` 会隐含请求 GPU render
- 编码器自动模式默认值为 `balanced`

## 8. 常见工作流

启动应用并进入 debug 模式：

- `MiaCode.exe --debug`

启动发布包并自动落地本地日志目录：

- 运行 `Start_MiaCode_Debug.bat`

导出视频并保留调试日志：

- `MiaCode.exe --debug --export-video ...`

关闭某一类调试输出：

- 例如关闭 startup timing：
  - `set MIACODE_DISABLE_STARTUP_TIMING=1`
- 例如关闭 OpenGL driver debug message：
  - `set MIACODE_DISABLE_GL_DEBUG_MESSAGES=1`

把日志统一输出到本地目录：

- `set MIACODE_LOG_DIR=<folder>`

## 9. 辅助脚本

- `scripts/Start_MiaCode_Debug.bat`
- `scripts/Start_MiaCode_Debug_CompareDump.bat`
  - 会被打包到 Windows release 根目录
- `scripts/export_and_analyze_duplicates.py`
  - 使用 `--debug` 启动导出
  - 预置一组常用重复帧诊断环境变量
- `scripts/analyze_video_duplicate_frames.py`
- `scripts/compare_log_vs_video_trajectory.py`
- `scripts/analyze_ffmpeg_chain_variants.py`

## Preview Video Compare

- `MIACODE_PREVIEW_DIAG_COMPARE_VIDEO_FALLBACK_EVERY`
  - Debug-only. Sample every Nth direct-upload preview video frame.
  - Compares GPU `glReadPixels` readback against a CPU `QVideoFrame::toImage()` fallback render.
  - Writes per-sample diff, signatures, and near-black stats to runtime log scope `preview_gl/video_compare`.
  - Example: `set MIACODE_PREVIEW_DIAG_COMPARE_VIDEO_FALLBACK_EVERY=60`

## Preview Present Compare And Dumps

- `MIACODE_PREVIEW_DIAG_COMPARE_PRESENT_EVERY`
  - Debug-only. Sample every Nth final preview `paintGL` present after the frame is fully drawn.
  - Compares the final GPU framebuffer against a cloned CPU-only `PreviewCanvas` render.
  - Writes per-sample diff, signatures, and near-black stats to runtime log scope `preview/present_compare`.
- `MIACODE_PREVIEW_DIAG_COMPARE_DUMP_FRAMES`
  - Debug-only. Enables PNG dumps for compare samples.
  - Saves `gpu.png`, `cpu.png`, and `abs_diff.png` for both `video_compare` and `present_compare`.
- `MIACODE_PREVIEW_DIAG_COMPARE_DUMP_MAX_SAMPLES`
  - Per compare stream dump cap. Default `8`; set `0` for no cap.
- `MIACODE_PREVIEW_DIAG_COMPARE_DUMP_DIR`
  - Optional dump root override.
  - Default root is under `MIACODE_LOG_DIR` when set, otherwise the system temp directory.
- Example:
  - `set MIACODE_PREVIEW_DIAG_COMPARE_PRESENT_EVERY=120`
  - `set MIACODE_PREVIEW_DIAG_COMPARE_DUMP_FRAMES=1`
  - `set MIACODE_PREVIEW_DIAG_COMPARE_DUMP_MAX_SAMPLES=4`

## 10. 代码锚点

- 调试开关解析：`src/common/DebugOptions.h`
- 共享日志路由：`src/common/DebugLog.h`、`src/common/DebugLog.cpp`
- 应用启动与 debug 入口：`src/app/main.cpp`
- 主 runtime 日志入口：`src/app/mainwindow/MainWindow.cpp`
- 预览 runtime 日志入口：
  - `src/preview/video/PreviewCanvas.cpp`
  - `src/preview/video/PreviewGLRenderer.cpp`
  - `src/preview/video/PreviewMediaController.cpp`
- Audio 日志入口：`src/preview/audio/QtPreviewSfxRuntime.cpp`
- Export 日志入口：`src/tools/video_export/VideoExportController.cpp`

当调试开关、日志路径、日志层级或发布调试流程发生变化时，请同步更新本文档。
