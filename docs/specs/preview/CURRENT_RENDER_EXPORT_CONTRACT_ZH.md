---
lifecycle: stable-current
owner: src/preview
canonical_id: preview.render-export
last_verified: 2026-09-06
code_anchors: ["src/preview/runtime", "src/preview/quick_scene", "src/core/scene", "src/tools/video_export", "src/app/main.cpp"]
---

# 当前预览与导出渲染契约

## 当前路径

- 谱面场景数学与 layer/frame state 位于 `src/core/scene/`，不依赖 GPU 实现。
- 实时预览通过 `PreviewRuntime` 与 `src/preview/quick_scene/` 的 Qt Quick/QSG 场景绘制。
- 导出复用 QSG 场景、资源解析和渲染语义，以显式帧时间推进。
- Windows headless 导出保留 D3D11/QRhi 会话 `PreviewQuickD3D11ExportSession`，以及
  `PreviewQuickExportSession` 的 OpenGL 路线；后端选择见 `src/app/main.cpp` 与导出 backend。
  不把 QSG 场景路线与底层图形 API 混为一谈。
- 视频导出 worker 仍是当前导出链路。已删除的是旧 DComp 图表渲染器和外置实时预览 worker，
  不能因此删除现行导出进程或 D3D11/QRhi 支持。

## 共享契约

| 行为 | 单一复用入口与同步面 |
| --- | --- |
| 场景层级 | `PreviewLayerOrder.h`、PreviewQuickSceneRoot；渲染 slot 顺序与 layer mask 必须一起检查 |
| 同时刻物件排序 | PreviewMarkerDrawOrder 与 prepared scene cache；preview、timeline、export 核对相同语义 |
| timing 与动画 | core/scene 的纯时间函数；离线帧不依赖事件循环 Timer |
| 皮肤与素材 | scene selectors、AssetPaths、ChartAssetPaths、scene asset loader；预览/导出解析一致 |
| SFX | PreviewSfxTimeline / PreviewSfxTiming；运行时音频与 VideoExportAudioRenderPlan 使用相同事件语义 |
| 导出配置 | QML session / preferences → runtime snapshot → VideoExportSnapshot JSON → worker task |
| 封面合成 | QmlCoverExportWindow/Session 与 tools/cover_export；窗口拥有自己的 engine 和捕获资源 |

## 生命周期与验证

音频 worker、设备 lease、QML engine 和导出任务有独立生命周期；资源重建与异步回调须匹配当前身份。
不能为减少 Spec executable 而合并会互相污染全局状态或弱化链接依赖的边界规格。

规格入口见 [Spec 目录](../../tests/SPEC_CATALOG.md) 的 preview、video_export、timeline 域。
涉及后端与设备的行为须在实际平台验证，纯策略规格通过不等于 GPU/音频设备通过。

旧设计见 [历史预览架构](PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md) 和
[GPU/QRhi 方案](GPU_RENDERING_DEVICE_AND_QRHI_EXPORT_PLAN.md)，仅用于理解迁移背景。
