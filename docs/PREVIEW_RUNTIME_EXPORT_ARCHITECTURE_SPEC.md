# Preview Runtime And Export Architecture

## Scope

This document is the current source of truth for MiaCode's preview and export rendering path after the Qt Quick migration.

It replaces the old `PreviewCanvas` / `PreviewGLRenderer` / `PreviewQuickItem` split and focuses on the active code path only.

## Current Backend Stance

- Realtime preview and headless export both render through Qt Quick scene graph layers.
- The project currently forces Qt Quick to the OpenGL graphics API on desktop startup.
- The export session is intentionally OpenGL-specific today because it relies on `QQuickRenderControl` plus an OpenGL offscreen context and optional PBO readback.
- This means the architecture is no longer tied to the removed legacy renderer, but it is still intentionally tied to an OpenGL-backed Qt Quick runtime.

Primary owner files:

- `src/app/main.cpp`
- `src/preview/runtime/PreviewRuntime.h`
- `src/preview/runtime/PreviewRuntime.cpp`
- `src/preview/runtime/PreviewQuickExportSession.h`
- `src/preview/runtime/PreviewQuickExportSession.cpp`

## Shared Runtime Model

Both realtime preview and export consume the same backend-neutral scene payload:

- `PreviewFrameState`
- `PreviewRenderLayerFlags`
- `PreviewSceneAssetRepository`
- `PreviewSceneAssetLoader`

Ownership split:

- `src/preview/scene/*`
  - pure descriptors, geometry helpers, curves, and layer-state builders
- `src/preview/quick_scene/*`
  - QSG / QQuick rendering layers and helper node builders
- `src/preview/runtime/*`
  - runtime host, asset ownership, media/session bridging, headless export session
- `src/tools/video_export/*`
  - export orchestration, ffmpeg piping, snapshot boundary, headless Quick export backend

## Realtime Preview Chain

Realtime preview path:

1. `MainWindow` publishes render settings, note markers, Muri state, and playhead into `PreviewRuntime`.
2. `PreviewRuntime` owns the live `PreviewFrameState` and shared `PreviewSceneAssetRepository`.
3. `PreviewQuickRuntimeSurface` hosts the actual `QQuickView`.
4. `PreviewQuickSceneRoot` consumes `PreviewFrameState` and creates the active visual stack.
5. `PreviewQuickHudLayer` renders HUD text above the scene.
6. `PreviewMediaController` remains the dedicated media-thread owner for background image/video playback.
7. `QtPreviewSfxRuntime` remains the dedicated audio/SFX timeline owner.

Important implication:

- Rendering is now Qt Quick.
- Media ownership and audio ownership are still intentionally separate from scene rendering.
- The play-start snapshot freeze semantics are preserved at the `MainWindow` / `PreviewRuntime` level, not delegated to QML animation state.
- Realtime preview startup is intentionally asymmetric: canvas + background track + SFX are committed as one strong-sync group, while background video is weak-sync and may visibly start later so long as it re-locks to the audio clock after launch.

## Realtime Layer Ownership

The active visible layer stack is owned by:

- `src/preview/quick_scene/PreviewQuickSceneRoot.*`
- `src/preview/scene/PreviewLayerOrder.h`

The scene is assembled from dedicated layers rather than a monolithic renderer:

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

Each visual family should be edited through its `scene/Preview*LayerState.*` builder first, then its matching `quick_scene/PreviewQuick*Layer.*` renderer.

### Judge Firework Compatibility

The active firework renderer is the custom-material path under:

- `src/preview/scene/PreviewJudgeFireworkLayerState.*`
- `src/preview/quick_scene/PreviewQuickJudgeFireworkLayer.*`
- `src/preview/quick_scene/shaders/PreviewFireworkMaterial.*`

Compatibility note:

- The reference behavior for firework lifecycle tuning is the real `v0.3.7-dev5` implementation at commit `50c1a55ddcdd7e8aec2f574d63579674b1fe03ee`.
- The matching legacy owners are `src/preview/video/PreviewCanvas.cpp` for the firework curves and `src/preview/video/PreviewCanvas.Objects.cpp` for the draw order and clip semantics.
- A later quick-preview restore commit, `0d6dd1d`, reintroduced zero-start color-ball ramps that made the center ball appear too small when the effect first spawned. The active Quick path should not preserve that regression.
- The firework visible region is bounded by the playfield-centered judgment-ring interior, using the same `kLogicalDistanceEdge`-derived stage clip as the legacy `PreviewCanvas` layer. It must not apply a second local circle or rectangle clip around the trigger point.
- If you change firework shader math, keep the stage-bound clip in `PreviewQuickJudgeFireworkLayer` aligned with the historical `PreviewCanvas::drawJudgeEffectFireworkLayer` layer clip and keep the color-ball curves aligned with the legacy dev5 source unless there is an intentional product decision to retune them.

## Export Chain

Headless export path:

1. `MainWindow` builds a `VideoExportSnapshot`.
2. The export worker reconstructs a `VideoExportTask`.
3. `VideoExportController` owns the ffmpeg process, raw-frame pipe, timing diagnostics, and export loop.
4. `VideoExportQuickRenderBackend` owns a `PreviewSceneAssetRepository`, a `PreviewFrameState`, and a `PreviewQuickExportSession`.
5. `PreviewQuickExportSession` creates a headless `QQuickRenderControl` scene and renders `PreviewQuickSceneRoot` into an offscreen framebuffer.
6. The resulting RGBA frame is packed and streamed to ffmpeg through the raw video pipe.

Worker logging note:

- Export workers now bind their shared log directory to the snapshot chart's project-local `.miacode/logs/` directory when no explicit log-dir override is set.
- This keeps worker-side export and fatal logs aligned with the chart being exported instead of falling back to executable-local debug logs.

Important export-side constraints:

- Export uses the same Quick scene and layer-state builders as realtime preview.
- Export does not reuse a live preview window or share a legacy renderer.
- Export overlay rendering is selected by `kPreviewExportOverlayRenderLayers`; it is not a second hand-maintained draw list.

Primary owner files:

- `src/tools/video_export/VideoExportController.cpp`
- `src/tools/video_export/VideoExportQuickRenderBackend.cpp`
- `src/tools/video_export/RawVideoPipeTransport.cpp`
- `src/preview/runtime/PreviewQuickExportSession.cpp`

## Offscreen Readback And ffmpeg

The current export loop supports:

- headless Quick render to an offscreen OpenGL framebuffer
- direct framebuffer readback
- optional double-buffered PBO readback
- raw RGBA pipe write into ffmpeg

Relevant switches:

- `MIACODE_EXPORT_ENABLE_GPU_RENDER`
- `MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO`
- `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO`

Current intent:

- Keep the ffmpeg raw-pipe pipeline and backpressure instrumentation.
- Keep direct framebuffer readback as the semantic fallback path.
- Keep PBO readback as the default export path when the Quick export session proves it is safe to enable.
- Require capability probing before entering the PBO path: current export context, `extraFunctions()`, pixel-pack-buffer support, `glMapBufferRange` support, and a small map/unmap smoke probe.
- On `renderFramePboStep()` soft failure, reset the session PBO state and continue the same export through direct readback.
- On worker `CrashExit`, retry the same snapshot once with `MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO=1`; do not retry normal business failures or user cancellation.
- On teardown when `makeCurrent()` fails, skip explicit PBO/FBO/Quick GL cleanup and let worker-process exit reclaim those resources.
- Do not reintroduce the removed legacy renderer just to recover export performance.

## Removed Legacy Path

The following components are no longer part of the active preview/export architecture and should not be treated as future extension points:

- `PreviewCanvas`
- `PreviewGLRenderer`
- `PreviewQuickItem`
- `PreviewQuickLayerRenderNode`

If behavior needs to change now:

- do not add new logic under `src/preview/video/` except `PreviewMediaController`
- do not rebuild a fallback painter bridge
- do not add a second scene implementation for export

## Packaging Contract

Windows packages must contain the Qt Quick runtime, not just the old widget stack.

Required runtime families:

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

Required runtime content:

- Qt plugin folders such as `platforms/`, `imageformats/`, `multimedia/`
- deployed QML modules under `qml/`
- pinned `ffmpeg`
- runtime assets

The Windows packaging script also rejects stale legacy runtime DLLs such as `Qt6OpenGLWidgets.dll`.

## Change Checklist

When changing preview/export behavior, review all of:

- `src/preview/scene/*`
- `src/preview/quick_scene/*`
- `src/preview/runtime/*`
- `src/tools/video_export/*`
- `.codex/skills/miacode-dev-guide/references/feature-index.md`
- `.codex/skills/miacode-dev-guide/references/cross-chain-linkage.md`
- `.codex/skills/miacode-dev-guide/references/assets-and-tools.md`
- `.codex/skills/miacode-dev-guide/references/debug-flags.md`

If code and docs disagree, code wins. Update this file in the same patch that changes ownership or runtime/export linkage.
