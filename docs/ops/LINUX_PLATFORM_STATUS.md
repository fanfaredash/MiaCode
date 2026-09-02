# Linux 平台交接

更新时间：2026-08-25

## 当前状态

- 主线：`dev`；Linux 集成分支：`dev-linux`。
- `dev-linux` 已通过 `d57e5a38` 合入 `origin/dev@045754e9`。
- GNOME 菜单修复已提交：`565a2d56`。
- 本次提交包含 QtAVPlayer 多平台统一、Linux VAAPI 接入及资源生命周期修正。
- 用户已验证 GNOME 菜单和 VMware 软件解码播放正常。
- AMD Radeon 680M、Mesa 26.2.1、xcb/EGL 环境已验证 VAAPI 硬件解码：PV 为单画面、颜色正常，播放期间无 `eglCreateImageKHR` 错误。
- VAAPI/EGL 修正已提交；导出混音已改为共享音效字节创建内存流，用户验证导出成功且行为几乎无差异。

## 实现约定

- Windows、macOS、Linux 共用 `PreviewStageMediaHost` 与 QtAVPlayer 播放控制。
- 平台桥接分别为 D3D11VA、VideoToolbox/Metal、VAAPI DRM/EGL。
- Linux 开发构建从本机 `pkg-config` 获取 FFmpeg、VAAPI、DRM、OpenGL 和 EGL。
- Linux 普通 GUI 确定使用 xcb 时选择 `xcb_egl`，使 Qt Quick OpenGL 上下文与 VAAPI DRM/EGL 桥接共享当前 `EGLDisplay`。
- 应用层不做解码失败后的软件重试。VAAPI 设备不可用时，QtAVPlayer/FFmpeg 使用普通软件解码器。
- VMware SVGA II 没有可用 VAAPI 视频设备，软件解码属于预期行为。
- 预览使用 FFmpeg 开发库；视频导出需要独立的 `ffmpeg` 和 `ffprobe` 命令行程序。

## 已完成

- GNOME 窗口菜单恢复显示和交互；KDE 原有行为保持正常。
- Linux QtAVPlayer 使用 VAAPI DRM/EGL 接入共享播放链路。
- 移除应用层自动软件解码重试。
- `vaExportSurfaceHandle()` 导出的 DRM 文件描述符在所有返回路径释放。
- Linux 视频帧改为每个 `VideoBuffer_EGL` 独立持有纹理，旧帧不再与后续帧共享同一组纹理。
- VAAPI 导出的 `drm_format_modifier` 通过 EGL DMA-BUF modifier 属性传入纹理导入，AMD 平铺表面不再被按线性布局采样。
- Qt 6 的 VAAPI `QVideoFrame` 声明为 `Format_NV12`，与 Y、UV 两张 RHI 纹理一致，不再把 R8 亮度平面当作 RGBA 纹理。
- Linux 普通 GUI 的 xcb 兼容路径自动选择 `xcb_egl`；导出 worker 原有 xcb/EGL 选择保持不变。
- 上述修正已完成 Release 增量编译，产物为 `build/MiaCode`。
- 修正 Qt 6.4 以上版本中 `PlanarVideoBuffer::m_rhi` 缺少声明的问题；Qt 6.11.2 Release 增量编译通过。

## 已实现：导出音频资源管理

### 已确认原因

- 修正前，`BassExportAudioBackend::renderMixedTrackToWav()` 为每个 SFX 和 touch-hold 事件调用一次 `BASS_StreamCreateFile(FALSE, path, ...)`。
- 修正前，所有 `ScheduledSource` 保留到整轨混音结束，文件描述符占用随事件总数线性增长。
- Linux 进程软限制为 1024 时，数百个事件流会耗尽文件描述符；随后 `Pcm16WavWriter` 打开 `export_audio.wav` 失败，界面显示 `failed to open export wav for writing`。
- 临时提高 `ulimit -n` 已由用户验证可恢复导出，但资源增长模式仍然存在。
- 修正前，音源流创建失败会记录 `audio_backend_source_skip` 后继续，最终错误位置晚于真实失败点。

### 实现方案

1. 在单次导出范围内按资源路径缓存 SFX 压缩文件字节，每种资源只读取一次。
2. 每个事件继续创建独立 BASS 解码流，改用 `BASS_FILE_MEM` 引用共享字节；每个 `ScheduledSource` 持有共享 `QByteArray`，直至对应事件流释放。
3. BGM 保持单个文件流，防止长音轨整体进入内存。
4. 保留现有 `BASS_Mixer_StreamAddChannelEx` 绝对时间调度、音量、最大时长和 touch-hold `sourceStartSecond` 行为。
5. 不使用 `BASS_FILE_MEMCOPY`，防止每个事件复制一份压缩数据。
6. 已存在的音源读取或创建失败时立即终止混音，并报告 tag、路径和 `BASS_ErrorGetCode()`；错误不再延迟到 WAV 打开阶段。

代码范围为 `src/tools/video_export/BassExportAudioBackend.cpp`。音频渲染计划、FFmpeg、导出页面和预览音频链路保持原状。

实现后已完成 Release 增量编译。用户已在 Linux 环境验证高密度谱面导出成功，音效、时间调度和整体行为几乎无差异。

### 备选方案

- `BASS_SampleLoad` 加 `BASS_SampleGetChannel`：每种 SFX 只解码一次并共享 PCM，CPU 成本更低，但会扩大行为变化和解码后内存占用。
- 按时间窗口创建事件流并使用 mixer 自动释放：可同时限制流数量和文件描述符，调度与生命周期实现更复杂。
- 提高 `RLIMIT_NOFILE`：保留为用户侧临时处理，不作为正式修复。

### 验收条件

1. 默认 Linux 软限制 1024 下，高密度谱面可生成混合 WAV 并完成视频导出。
2. SFX 文件描述符数量按唯一资源数保持常量，不随事件总数增长。
3. BGM、普通 SFX、touch-hold 起始偏移、截断和音量与修正前高限制环境的输出一致。
4. 音源加载失败直接报告真实资源与 BASS 错误码。
5. Windows、Linux、macOS 共用同一内存流策略。

## 当前分支合入条件

1. 用户验证当前构建：播放、暂停、跳转、切换谱面和长时间播放正常。
2. 观察长时间播放期间视频帧相关文件描述符数量保持稳定。
3. 补充实体 Intel Linux 环境的 VAAPI 硬件解码验证；AMD Radeon 680M 已通过。
4. 记录 `dev` 与 `dev-linux` 的冷启动差值；启动回退必须有明确结论。
5. 同步最新 `origin/dev`，完成 Release 增量编译和 GNOME/KDE 回归检查。
6. 提交工作区并合入 `dev`。

## 合入主线后处理

这些修改会改变三平台共享代码，应在 `dev` 统一完成：

1. 精简 QtAVPlayer 目标和 FFmpeg 依赖，处理启动速度回退。
2. 停止预览不需要的音频、字幕解码。
3. 移除 AV1 固定 `libdav1d` 软件解码策略。
4. 删除旧 `QMediaPlayer` 分支、空接口和无效状态。
5. 统一记录实际启用的解码器，区分目标后端与运行结果。
6. 调整 Linux 视频导出查找顺序为环境变量、系统 `PATH`、程序本地位置。

## 后续能力

- Linux VAAPI P010/10-bit 支持。
- 实体 GPU 覆盖更多驱动和桌面组合。
- 视频导出环境诊断。

## 验证命令

Release 增量编译：

```bash
cmake --build build --target MiaCode --config Release --parallel 4
```

运行产物：`build/MiaCode`

调试模式下，预览后端记录位于谱面目录 `.miacode/logs/miacode_audio_debug.log`。
`media_backend` 当前记录目标后端，不能据此确认 VAAPI 已成功创建。

## 关键位置

- 构建接入：`CMakeLists.txt`
- 播放宿主：`src/preview/runtime/PreviewStageMediaHost*`
- Linux VAAPI 桥接：`third_party/QtAVPlayer/src/QtAVPlayer/qavhwdevice_vaapi_drm_egl.cpp`
- Qt 视频帧格式桥接：`third_party/QtAVPlayer/src/QtAVPlayer/qavvideoframe.cpp`
- Linux xcb/EGL 启动选择：`src/app/main.cpp`
- 导出音频混音：`src/tools/video_export/BassExportAudioBackend.cpp`
- 导出程序解析：`src/tools/video_export/VideoExportEncoder.cpp`
- 调试字段：`docs/ops/DEBUG_INDEX.md`

## 维护规则

每次完成同步、修复、编译或平台验证后，更新以下内容：

- `更新时间`
- `当前状态`
- `已完成`
- `当前分支合入条件`
- `更新记录`

## 更新记录

| 日期 | 结果 |
| --- | --- |
| 2026-08-24 | 建立 Linux 平台交接记录。 |
| 2026-08-24 | 修复 DRM 描述符生命周期，Release 增量编译通过。 |
| 2026-08-24 | 改为每帧独立 VAAPI/EGL 纹理，Release 增量编译通过并纳入当前提交；等待用户验证。 |
| 2026-08-24 | 修正 Qt 6.4 以上版本的 `PlanarVideoBuffer::m_rhi` 声明条件，Qt 6.11.2 Release 增量编译通过。 |
| 2026-08-24 | AMD Radeon 680M 下完成 xcb/EGL、DRM modifier 和 NV12 格式修正；VAAPI PV 单画面、颜色正常且无 EGL 导入错误。 |
| 2026-08-24 | 记录导出混音文件描述符耗尽原因、内存流推荐方案、备选方案与验收条件；代码尚未修改。 |
| 2026-08-24 | 导出 SFX 和 touch-hold 改用共享字节内存流，音源失败改为立即报告；Release 增量编译通过，等待用户验证。 |
| 2026-08-25 | 用户验证共享音效字节内存流方案可正常完成视频导出，行为几乎无差异；代码审查通过。 |
