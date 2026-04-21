# 预览运行时与导出架构

## 范围

本文档是 MiaCode 在 Qt Quick 迁移之后，预览与导出渲染路径的当前事实来源。

它取代了旧的 `PreviewCanvas` / `PreviewGLRenderer` / `PreviewQuickItem` 分叉说明，只聚焦当前仍然活跃的代码路径。

## 当前后端立场

- 实时预览和无头导出都通过 Qt Quick scene graph layers 渲染。
- GUI 运行时与导出 worker 当前并不遵守完全相同的后端规则。
- 无头导出路径今天仍然明确绑定 OpenGL，因为它依赖 `QQuickRenderControl`、OpenGL 离屏上下文以及可选的 PBO 回读。
- 因此，预览/导出架构已经不再绑定被移除的 legacy renderer，但导出路径仍然有意依赖一个 OpenGL-backed 的 Qt Quick 运行时。

主要 owner 文件：

- `src/app/main.cpp`
- `src/preview/runtime/PreviewRuntime.h`
- `src/preview/runtime/PreviewRuntime.cpp`
- `src/preview/runtime/PreviewQuickExportSession.h`
- `src/preview/runtime/PreviewQuickExportSession.cpp`

## 共享运行时策略方向

Preview 现在已经具备了应用级与模块级 Quick 策略拆分的雏形：

- 应用启动与 Qt 全局行为位于 `src/app/main.cpp`
- 预览模块级的 surface 路由位于 `src/app/quick_shell/QuickShellPreviewSurfacePolicy.h`
- Quick 侧的 palette/metrics bridge 位于 `src/app/quick_shell/QuickShellStyleBridge.*`

如果项目后续引入共享的 `QuickRenderPolicyConfig` 以及按模块划分的 `QuickModuleRenderPolicy`，那么 Preview 与 Timeline 应从同一份配置中读取，而不是各自发明后端、surface-mode 和 fallback 选择规则。

当前 preview 特有的参考点：

- quick-shell preview 只有在 frontend 是 Quick shell 且 stage media 是视频时，才会启用 separate surface
- 其它情况下 preview 保持 inline 路径

这个现有 preview 选择策略对 Timeline 很有参考价值，因为它已经区分了：

- 应用级 startup/backend 规则
- 模块级 inline 与 separate-surface 决策
- 应在多个 Quick 模块间共享的 runtime diagnostics

## 共享运行时模型

实时预览与导出都消费同一套后端无关的 scene payload：

- `PreviewFrameState`
- `PreviewRenderLayerFlags`
- `PreviewSceneAssetRepository`
- `PreviewSceneAssetLoader`

职责拆分：

- `src/preview/scene/*`
  - 纯描述符、几何 helper、曲线与 layer-state builders
- `src/preview/quick_scene/*`
  - QSG / QQuick 渲染层与辅助节点构建器
- `src/preview/runtime/*`
  - 运行时宿主、资源 ownership、media/session bridging、无头导出 session
- `src/tools/video_export/*`
  - 导出编排、ffmpeg piping、snapshot boundary、无头 Quick export backend

## 实时预览链路

实时预览路径：

1. `MainWindow` 将 render settings、note markers、Muri state 和 playhead 发布给 `PreviewRuntime`
2. `PreviewRuntime` 拥有 live `PreviewFrameState` 与共享的 `PreviewSceneAssetRepository`
3. `PreviewQuickRuntimeSurface` 宿主真实的 `QQuickView`
4. `PreviewQuickSceneRoot` 消费 `PreviewFrameState` 并创建当前可见的场景栈
5. `PreviewQuickHudLayer` 在场景之上渲染 HUD 文本
6. `PreviewMediaController` 仍然是背景图片/视频播放的专用 media-thread owner
7. `QtPreviewSfxRuntime` 仍然是音频/SFX timeline 的专用 owner

重要含义：

- 渲染已经是 Qt Quick。
- media ownership 与 audio ownership 仍然有意独立于 scene rendering。
- play-start snapshot freeze 语义保留在 `MainWindow` / `PreviewRuntime` 层，而不是下放到 QML animation state。
- 实时预览启动仍然是非对称的：canvas + background track + SFX 是一个强同步组，而 background video 是弱同步组，可以晚一点出现，但必须重新锁回 audio clock。

## 实时图层 owner

当前可见图层栈由以下部分拥有：

- `src/preview/quick_scene/PreviewQuickSceneRoot.*`
- `src/preview/scene/PreviewLayerOrder.h`

场景由独立 layer 组成，而不是一个单体 renderer：

- stage background
- playfield backdrop
- guide
- track
- slide motion
- judge effect
- touch judge
- heads
- touch
- touch-hold
- chart-review judge overlay
- Maimuri DX judge overlay
- Muri pad overlay
- Muri action overlay
- judge firework overlay
- HUD

每一类视觉内容都应先改动其 `scene/Preview*LayerState.*` builder，再改动对应的 `quick_scene/PreviewQuick*Layer.*` renderer。

### Judge firework 兼容性

当前 firework renderer 走的是 custom-material 路径，相关文件：

- `src/preview/scene/PreviewJudgeFireworkLayerState.*`
- `src/preview/quick_scene/PreviewQuickJudgeFireworkLayer.*`
- `src/preview/quick_scene/shaders/PreviewFireworkMaterial.*`

兼容性说明：

- firework 生命周期调参的参考行为是 `v0.3.7-dev5` 版本中 commit `50c1a55ddcdd7e8aec2f574d63579674b1fe03ee` 的实现。
- 对应 legacy owner 是 `src/preview/video/PreviewCanvas.cpp` 中的 firework curves，以及 `src/preview/video/PreviewCanvas.Objects.cpp` 中的 draw order 与 clip 语义。
- 后续某次 quick-preview restore commit `0d6dd1d` 曾重新引入 zero-start color-ball ramps，使 firework 刚生成时中间球体偏小，当前 Quick 路径不应保留这个回归。
- firework 的可见区域受 playfield-centered judgment-ring interior 限制，且必须与 legacy `PreviewCanvas` 的 stage clip 语义对齐，不能再套一层局部圆形或矩形 clip。

## 导出链路

无头导出路径：

1. `MainWindow` 构建 `VideoExportSnapshot`
2. 导出 worker 重建 `VideoExportTask`
3. `buildVideoExportAudioRenderPlan(...)` 把导出期的 BGM 放置、scheduled SFX playback 与合并后的 touchhold sustain 收敛成一份离线音频计划
4. `VideoExportAudioBackend::renderMixedTrackToWav(...)` 渲染单条 `export_audio.wav`；Windows 走 `BassExportAudioBackend`，非 Windows 保留 `LegacyExportAudioBackend` 作为 non-parity fallback
5. `VideoExportController` 拥有 ffmpeg 进程、raw-frame pipe、时序诊断与导出循环
6. `VideoExportQuickRenderBackend` 拥有 `PreviewSceneAssetRepository`、`PreviewFrameState` 和 `PreviewQuickExportSession`
7. `PreviewQuickExportSession` 创建无头 `QQuickRenderControl` 场景，并将 `PreviewQuickSceneRoot` 渲染到离屏 framebuffer
8. 最终 RGBA frame 被打包并通过 raw video pipe 流入 ffmpeg，而预混好的 `export_audio.wav` 直接参与 mux，不再追加 BGM/SFX `amix` 阶段

worker 日志说明：

- 当没有显式 log-dir override 时，导出 worker 会默认把共享日志目录绑定到 snapshot chart 对应的 project-local `.miacode/logs/`
- 这样 worker 侧导出日志与 fatal 日志就会和当前 chart 对齐，而不是退回到可执行文件目录下的调试日志

重要约束：

- 导出复用与实时预览相同的 Quick scene 和 layer-state builders
- 导出复用与实时预览相同的 `PreviewSfxTimeline` 调度语义，但混音执行层现在通过 `VideoExportAudioRenderPlan` 与 `VideoExportAudioBackend` 收口，不再在 controller 里手写一条 BGM/SFX 混音链
- 导出不会复用 live preview window，也不会共享 legacy renderer
- 导出 overlay 渲染由 `kPreviewExportOverlayRenderLayers` 选择，不存在第二套手工维护的 draw list

主要 owner 文件：

- `src/tools/video_export/VideoExportController.cpp`
- `src/tools/video_export/VideoExportAudioRenderPlan.cpp`
- `src/tools/video_export/BassExportAudioBackend.cpp`
- `src/tools/video_export/LegacyExportAudioBackend.cpp`
- `src/tools/video_export/VideoExportQuickRenderBackend.cpp`
- `src/tools/video_export/RawVideoPipeTransport.cpp`
- `src/preview/runtime/PreviewQuickExportSession.cpp`

## 离屏回读与 ffmpeg

当前导出循环支持：

- 将 Quick 场景无头渲染到离屏 OpenGL framebuffer
- 直接 framebuffer 回读
- 可选的双缓冲 PBO 回读
- 原始 RGBA 管道写入 ffmpeg

相关开关：

- `MIACODE_EXPORT_ENABLE_GPU_RENDER`
- `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO`
- `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO`

当前意图：

- 保留 ffmpeg raw-pipe pipeline 与背压诊断
- 保留 direct framebuffer readback 作为语义上的后备路径
- 当 Quick export session 证明 PBO 路径可安全启用时，仍将其作为默认导出路径
- 在进入 PBO 路径前必须做 capability probing：当前 export context、`extraFunctions()`、pixel-pack-buffer 支持、`glMapBufferRange` 支持，以及一个小型 map/unmap smoke probe
- 若 `renderFramePboStep()` 发生 soft failure，应重置 session 的 PBO 状态，并在同一次导出里切回 direct readback
- 若 worker 因 `CrashExit` 退出，且当前尝试仍请求了 offscreen PBO readback，则对同一 snapshot 用 `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1` 自动重试一次；正常业务失败或用户取消不应重试
- teardown 时如果 `makeCurrent()` 失败，可以跳过显式 PBO/FBO/Quick GL cleanup，让 worker 进程退出时回收资源
- 不要为了恢复导出性能而重新引入已移除的 legacy renderer

## 已移除的旧路径

以下组件已不再属于当前活跃的 preview/export 架构，不应再被视为未来扩展点：

- `PreviewCanvas`
- `PreviewGLRenderer`
- `PreviewQuickItem`
- `PreviewQuickLayerRenderNode`

如果现在需要调整行为：

- 不要在 `src/preview/video/` 下新增新逻辑，唯一例外仍然是 `PreviewMediaController`
- 不要重建 painter fallback bridge
- 不要为导出再增加第二套 scene implementation

## 打包契约

Windows 包必须包含 Qt Quick runtime，而不仅是旧 widget 栈。

必须包含的 runtime family：

- `Qt6Core`
- `Qt6Gui`
- `Qt6Widgets`
- `Qt6Multimedia`
- `Qt6Network`
- `Qt6OpenGL`
- `Qt6Quick`
- `Qt6Qml`
- `Qt6QmlMeta`
- `Qt6QmlModels`
- `Qt6QmlWorkerScript`
- `Qt6Svg`

必须包含的 runtime 内容：

- Qt plugin 目录，例如 `platforms/`、`imageformats/`、`multimedia/`
- 部署后的 `qml/` 模块
- 固定版本的 `ffmpeg`
- runtime assets

Windows 打包脚本还会拒绝已经过时的 legacy runtime DLL，例如 `Qt6OpenGLWidgets.dll`。

## 修改检查清单

当修改 preview/export 行为时，同时检查：

- `src/preview/scene/*`
- `src/preview/quick_scene/*`
- `src/preview/runtime/*`
- `src/tools/video_export/*`
- `.codex/skills/miacode-dev-guide/references/feature-index.md`
- `.codex/skills/miacode-dev-guide/references/cross-chain-linkage.md`
- `.codex/skills/miacode-dev-guide/references/assets-and-tools.md`
- `.codex/skills/miacode-dev-guide/references/debug-flags.md`

如果代码和文档冲突，以代码为准；一旦 owner、runtime 或 export linkage 发生变化，应在同一个 patch 中同步更新本文档。
