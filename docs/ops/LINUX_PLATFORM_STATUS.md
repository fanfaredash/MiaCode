# Linux 平台交接

更新时间：2026-08-24

## 当前状态

- 主线：`dev`；Linux 集成分支：`dev-linux`。
- `dev-linux` 已通过 `d57e5a38` 合入 `origin/dev@045754e9`。
- GNOME 菜单修复已提交：`565a2d56`。
- 本次提交包含 QtAVPlayer 多平台统一、Linux VAAPI 接入及资源生命周期修正。
- 用户已验证 GNOME 菜单和 VMware 软件解码播放正常。
- 当前动作：用户运行最新构建，验证每帧独立纹理修正。

## 实现约定

- Windows、macOS、Linux 共用 `PreviewStageMediaHost` 与 QtAVPlayer 播放控制。
- 平台桥接分别为 D3D11VA、VideoToolbox/Metal、VAAPI DRM/EGL。
- Linux 开发构建从本机 `pkg-config` 获取 FFmpeg、VAAPI、DRM、OpenGL 和 EGL。
- 应用层不做解码失败后的软件重试。VAAPI 设备不可用时，QtAVPlayer/FFmpeg 使用普通软件解码器。
- VMware SVGA II 没有可用 VAAPI 视频设备，软件解码属于预期行为。
- 预览使用 FFmpeg 开发库；视频导出需要独立的 `ffmpeg` 和 `ffprobe` 命令行程序。

## 已完成

- GNOME 窗口菜单恢复显示和交互；KDE 原有行为保持正常。
- Linux QtAVPlayer 使用 VAAPI DRM/EGL 接入共享播放链路。
- 移除应用层自动软件解码重试。
- `vaExportSurfaceHandle()` 导出的 DRM 文件描述符在所有返回路径释放。
- Linux 视频帧改为每个 `VideoBuffer_EGL` 独立持有纹理，旧帧不再与后续帧共享同一组纹理。
- 上述两项修正已完成 Release 增量编译，产物为 `build/bin/MiaCode`。

## 当前分支合入条件

1. 用户验证当前构建：播放、暂停、跳转、切换谱面和长时间播放正常。
2. 观察长时间播放期间文件描述符数量保持稳定。
3. 在实体 Intel 或 AMD Linux 环境确认 VAAPI 硬件解码。
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

运行产物：`build/bin/MiaCode`

调试模式下，预览后端记录位于谱面目录 `.miacode/logs/miacode_audio_debug.log`。
`media_backend` 当前记录目标后端，不能据此确认 VAAPI 已成功创建。

## 关键位置

- 构建接入：`CMakeLists.txt`
- 播放宿主：`src/preview/runtime/PreviewStageMediaHost*`
- Linux VAAPI 桥接：`third_party/QtAVPlayer/src/QtAVPlayer/qavhwdevice_vaapi_drm_egl.cpp`
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
