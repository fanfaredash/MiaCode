# Cross-Chain Linkage

Use this file before changing behavior that crosses parser, preview, audio, export, or tooling boundaries.

## 1. Edit To Parse To Timeline To Preview

Primary chain:

1. editor `contentsChange` or an explicit full refresh entry such as `scheduleTimelineRefresh`
2. `MainWindow::applyTimelineQuickChange` / `MainWindow::refreshTimelineQuickModelFromCurrentText`
3. `TimelineQuickModel::applyContentsChange` or `TimelineQuickModel::rebuildFromText`
4. `TimelineView::setTimelineData`
5. `MainWindow::requestTimelineSlowRefresh`
6. `SimaiNativeParser::parseForTimeline`
7. `buildTimelinePreviewRefreshState`
8. latest preview snapshot publication plus `MainWindow::applyLatestTimelinePreviewStateToPausedPreview` when playback is paused
9. `MainWindow::scheduleTimelineAnalysisRefresh`
10. `buildTimelineAnalysisRefreshResult`
11. `MainWindow::applyDeferredAnalysisUiUpdates`
12. `PreviewRuntime::setNoteMarkers`

Implication:

- A parser change is rarely parser-only.
- A new note property or timing rule usually needs timeline, preview, audio, export, and Muri review.
- Slide/tap head-material flags such as `$`, `$$`, `@`, `?`, and `!` are mirrored data: keep `SimaiNativeParser`, `TimelineQuickModel`, `PreviewSkinSelectors`, timeline icons, and chart-transform token preservation aligned in the same patch.
- Timeline note-head art selection should mirror preview base/overlay precedence: break or each chooses the base icon first, and EX overlays on top of that base instead of replacing break/each state in the timeline.
- `TimelineQuickModel` is now the owner of comma-only `C` anchor lookup for editor cursor sync, header/timeline `R -> C` jumps, and playback follow.
- Timeline beat-grid semantics are mirrored between `SimaiNativeParser` and `TimelineQuickModel`: every comma remains a beat line, while measure lines are generated on an independent meter timeline. The current meter now comes from shared `SimaiTimingMetadata` (`&whole_time_signature=`), inline `|| x/y` comments restart that meter timeline at the exact comment position, `{beats}` only changes comma spacing, and `(BPM)` changes restart the independent measure-line timeline at the BPM-change position.
- Guide-layer state should group each-guide connectors by parser-derived `eachGroupId` when available; do not merge backtick-separated groups just because their `marker.second` matches.
- Timeline note sprite stacking is intentionally preview-mirrored for overlapping markers: `TimelineView::paintEvent` keeps slide/wifi tracks behind note heads, uses the preview-style descending-`second` stack for tap/hold/slide/wifi heads, and then draws touch above that stack with touch-hold above touch. If preview object-layer order changes, review `src/timeline/TimelineView.Paint.cpp`, `src/preview/scene/PreviewLayerOrder.h`, and `src/preview/quick_scene/*` together.
- Same-second slide/head/track/motion stacking is now shared by `src/preview/scene/PreviewMarkerDrawOrder.*` plus the prepared `drawOrder` in `PreviewPreparedSceneCache`. If you change who sits “on top” for overlapping slides, update the helper and review `PreviewHeadLayerState.cpp`, `PreviewTrackLayerState.cpp`, `PreviewSlideMotionLayerState.cpp`, and the related preview specs together instead of patching one layer locally.
- The slide stacking direction is now runtime state, not a hardwired preview-only branch: `PreviewFrameState::render.slideEarlierSecondAndTextOnTop` is seeded from the common default constant, persisted by main-window render settings, and serialized through `VideoExportTask` / `VideoExportSnapshot` so the export worker sees the same DX-vs-FiNALE choice as the live preview.
- On-screen preview and export now flow through `PreviewRuntime` / `PreviewQuickExportSession` plus the active layers in `src/preview/quick_scene/*`. Shared assets now come from `PreviewSceneAssetLoader` and `PreviewSceneAssetRepository`. If you change preview setters, frame pacing hooks, layer data contracts, or export-session ownership, review both `src/preview/runtime/*` and `src/preview/quick_scene/*` in the same patch.
- Realtime QSG/HUD/DComp render paths must consume `PreviewRuntime::frameStateSnapshot()` instead of `PreviewRuntime::frameState()`. `frameState_` is GUI-side mutable builder state; render-side code should take one shared immutable snapshot pointer per paint/snapshot build and keep that pointer alive while reading `QString`, `QVector`, media, asset, and marker fields. HUD and center-display object stats must read the value-only `PreviewFrameState::hudStatsSnapshot` prepared before publication/export ticks, not call `PreviewProgressStatsCache::hudStatsAt()` on the render thread. `PreviewQuickExportSession` and cover-render helpers may still bind their own local frame-state objects because those are not shared live `PreviewRuntime` state, but they must refresh the HUD stats snapshot before rendering a frame.
- Preview-time background media is still selected once from `MainWindow::FrontendHostMode`: widget shell keeps the internal-layer `PreviewMediaController` path for both images and videos, while `--quickshell-beta` keeps `PreviewStageMediaHost` as the background-media owner for both images and videos. Quickshell presentation now splits after that host choice: images stay on the inline `QuickShellPreviewSurface.qml` item, while videos move into `QuickShellPreviewCompositeSurface` so the external `VideoOutput` stack presents from its own `QQuickView`. Do not reintroduce media-type-based switching between the widget-shell and quickshell host owners.
- The realtime and export Quick scene roots now also share `PreviewPreparedSceneCache`-driven note windows. If you change note-driven layer inputs, visible-window timing, or scene-content revision invalidation, review `src/preview/scene/PreviewPreparedSceneCache.*`, `src/preview/quick_scene/PreviewQuickSceneRoot.*`, and the affected `PreviewQuick*Layer` wrappers together.
- Runtime and export layer order are both owned by `PreviewQuickSceneRoot` plus `PreviewLayerOrder.h`. If you change layer ordering or add a new visible layer, review `src/preview/scene/PreviewLayerOrder.h`, `src/preview/quick_scene/*`, and `src/tools/video_export/VideoExportQuickRenderBackend.*` together.
- Firework overlay visuals now depend on the custom `PreviewQuickJudgeFireworkLayer` material path rather than pie-sector geometry plus sprite overlays. If you change firework timing curves, additive blending, source texture use, hole-mask math, or stage clipping, review `src/preview/scene/PreviewJudgeFireworkLayerState.*`, `src/preview/quick_scene/PreviewQuickJudgeFireworkLayer.*`, `src/preview/quick_scene/shaders/PreviewFireworkMaterial.*`, and the historical `PreviewCanvas` reference behavior together. The legacy contract is a playfield-centered judgment-ring clip, not a second local clip around the trigger point.
- While preview playback is running, slow-refresh note-marker updates still feed the latest validation and Muri worker inputs, but preview audio/canvas/object stats stay on the frozen play-start snapshot until playback stops; validation and Muri panel/decorations may defer their visible UI apply until playback returns to a paused state.
- When preview is paused or idle, Muri analysis should still publish fresh overlay state into preview/timeline as soon as the aligned report lands, but the full bottom-tab Muri list may stay lazily materialized until the `Muri` tab is actually visible. Keep summary chips and overlay alignment up to date even when the hidden tab content is stale on purpose.
- Preview object stats now share one `PreviewProgressStatsCache` across realtime HUD, the main-window side stats card, and export HUD rendering. Hold-family played counts and score-style progress must use judge/end timing in every consumer; if you touch the cache, keep that timing aligned with the HUD's finale/deluxe progression.
- Analysis-only setting changes such as Muri render mode, the static tap-on-slide threshold, or touch-exclusion rules for multi-touch should prefer reusing the latest preview snapshot and cached parse result instead of forcing another full slow refresh.
- Preview play/resume must use the latest in-memory field state, not a forced disk save. If slow refresh is still behind `timelineRevision_`, playback start may synchronously rebuild a preview-only note-marker snapshot once before audio/video start so the next resume does not wait for validation or Muri workers.
- Realtime preview startup is now split: chart canvas, background track, and SFX form the strong-sync group, while background video is weak-sync only. Playback must wait for the target canvas frame plus prepared audio/SFX state before commit, but stage media prepare / late-start must never block that commit.
- Preview startup should not force a synchronous quick-timeline `centerOnSecond(...)` pass before the strong-sync group commits. Seed the pending timeline second / playback-entry state and let the normal post-commit playback/timeline timers drive the first centered bridge update; otherwise the startup path can re-enter timeline scene building while audio/video startup state is still mid-transaction.
- Slow refresh may publish the preview-only note-marker snapshot before validation finishes. Resume-time preview freshness should not wait for the strict validation half of slow refresh.
- If preview play/resume is requested before the current revision's preview snapshot is ready, the request should wait in memory and auto-start once the preview snapshot for that same revision and difficulty lands. Validation completion must not gate that auto-start.
- Difficulty switching now intentionally clears the quick-timeline snapshot, preview note-marker snapshot, follow decorations, and pending bridge cursor/playhead state before the replacement refresh is queued on the next event-loop turn. After clearing, saved chart files must immediately re-sync the same chart path into the stage-media route so shared `bg.*` / `pv.mp4` media is not left detached while the new difficulty preview snapshot rebuilds, and the previous waveform / track duration must be restored so the preview slider upper bound keeps using the chart-vs-track maximum. Do not restore synchronous "switch field and rebuild everything in the same stack" behavior unless you re-check the stale-location risk across editor follow, preview startup, and Muri alignment.
- Preview follow is now editor-first and quick-model-driven: `TimelineQuickModel` precomputes stable follow buckets from parsed anchor timing plus shared follow spans, playback-time editor follow only consumes the current authoritative preview second against those buckets, and the timeline bridge remains a passive cursor/playhead mirror. While the Timeline tab is hidden, keep editor follow working from the quick model and cache bridge cursor/playhead updates until the Timeline tab is foreground again instead of reintroducing offscreen `centerOnSecond(...)` work.
- While preview follow is enabled during playback, do not keep pushing Timeline `C` from follow bindings. Let Timeline `R` stay the only high-cadence playback indicator, render `R` below `C`, and leave `C` at its last manual/editor-driven position until the user moves the cursor again after playback stops.
- Editor viewport hit-testing is shared between ordinary chart-editor clicks and main-window `Ctrl+click` preview jumps through `PlainCodeEditor::normalizedViewportHitPosition(...)`. If you change block-spacing or trailing-blank click behavior, review `src/editor/PlainCodeEditor.*` and `src/app/mainwindow/sections/window/MainWindow.WindowInteraction.cpp` together so editor cursor placement and preview jump targets keep the same `line / col -> second` anchor.
- Main-window preview left/right seeking is gated by text-input focus. When any text widget owns focus, unmodified `Left` / `Right` must stay with text editing and must clear any active preview held-seek state instead of letting preview continue underneath the editor. If you touch preview key routing, review `MainWindow.WindowInteraction.cpp`, `PlainCodeEditor.*`, and the protected-dialog shortcut policy together.
- In `quickshell-beta`, do not construct or attach the legacy `TimelineView` just to host Timeline state. `TimelineQuickStateBridge` must be able to run without a QWidget reference view there, and bottom-tabs Timeline routing/visibility must remain valid even when the Timeline tab is a pure quickshell logical page.
- Paused preview is intentionally silent for touch-hold sustain audio: `MainWindow::applyLatestTimelinePreviewStateToPausedPreview` must keep touch-hold voices paused, and only playback-start/resume paths should call `QtPreviewSfxRuntime::restoreTouchholdVoices`.
- Paused preview and timeline overlays must only apply a `MuriAnalysisReport` whose `noteMarkerSignature` matches the currently published preview snapshot. If slow refresh publishes new note markers before analysis finishes, clear/rehold the review overlay instead of mixing a stale report with fresh markers.

## 2. `&first` And Timing Offset Chain

Current contract:

- `SimaiDocument` stores raw `first`.
- `MainWindow::parsedFirstSeconds` is the main getter.
- preview/export `first` now uses the finite parsed raw `&first` value directly; do not maintain a second inverted "effectiveFirst" concept
- `TimelineQuickModel` receives `first` on every fast-path rebuild or incremental edit apply.
- `buildTimelineSlowRefreshResult` shifts parser-produced beat/note markers by `first`.
- `MainWindow::applyLatencyDetectorOffset` writes raw `first` back into the document.
- `LatencyDetectorDialog` reads and writes the raw value rather than maintaining an inverted shadow value.
- Export task reconstruction uses parsed document text plus shifted markers again.

If you change `&first` semantics, review all of:

- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- `src/tools/latency/LatencyDetectorDialog.Analysis.cpp`
- `src/tools/video_export/VideoExportSnapshot.cpp`
- `docs/specs/preview/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`

## 3. Timing Metadata And Default Meter Chain

Current contract:

- `SimaiTimingMetadata` is the shared payload for parser, quick timeline, slow refresh, validation cache, normalization, export snapshot rebuild, and CLI/dev helpers.
- `MainWindow::currentTimingMetadata` reads live metadata text from the metadata editor when available, so unsaved `&whole_time_signature=` edits still affect validation and timeline refresh.
- `parsedLatencyMeterId` now reads the effective chart default meter from timing metadata for latency-detector defaults; latency detection still writes `&first` and `&wholebpm`, but it no longer writes meter metadata back into the chart.
- Any caller that uses `SimaiNativeParser::parseForTimeline` or `buildValidationReport` should pass timing metadata when document metadata is available, or fast/slow preview, export, and tooling timelines will drift.
- Quick-timeline token parsing should stay aligned with parser note legality for timeline-visible syntax, including bare zero-duration `h` holds and zero-duration touch-holds such as `Ch`, `A1h`, and `Ch[]`; if parser and quick-model note acceptance diverge, the editor timeline can silently drop notes that preview/export still keep.

If you change timing-metadata semantics, review all of:

- `src/simai/document/SimaiTimingMetadata.cpp`
- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- `src/app/mainwindow/sections/validation/MainWindow.ValidationFlow.cpp`
- `src/timeline/TimelineQuickModel.cpp`
- `src/timeline/TimelineSlowRefresh.cpp`
- `src/tools/video_export/VideoExportSnapshot.cpp`
- `src/tools/muri/MuriDump.cpp`

## 4. Runtime SFX And Export SFX Must Stay In Sync

Canonical sync pair:

- Runtime: `src/preview/audio/QtPreviewSfxRuntime.Timeline.cpp`, `QtPreviewSfxRuntime::configureTimeline`
- Export: `src/tools/video_export/VideoExportAudioRenderPlan.cpp`, `buildVideoExportAudioRenderPlan`; `src/tools/video_export/VideoExportAudioBackend.h`, `VideoExportAudioBackend::renderMixedTrackToWav`

Current runtime ownership note:

- `QtPreviewSfxRuntime` is now the stable facade owned by `MainWindow`, while concrete runtime behavior lives behind `src/preview/audio/PreviewAudioBackend.h`.
- `MiniaudioPreviewAudioBackend` now remains the non-Windows compatibility implementation and still owns the existing `QtPreviewSfxRuntime.*.cpp` split internals.
- `BassPreviewAudioBackend` now owns the Windows preview-time BASS transport path with no Windows-side miniaudio fallback: repo-local runtime DLL loading, the master mixer authority clock, preloaded note-SFX channels, background-track tempo control through `bass_fx`, and backend-side event draining. It also keeps a lightweight rolling scheduler by arming only the next mixer sync position at a time instead of relying on UI tick direct one-shots.

Shared concerns:

- which note kinds emit `answer`, `judge`, `break`, `ex`, `touch`, `touchhold`, `firework`
- shared timing semantics now live in `src/common/PreviewTimingSettings.h` plus `src/common/PreviewSfxTiming.h`; `&first` currently uses the finite parsed raw document value directly, `audioOffset` is the whole-SFX chart-domain shift, `displayOffset` advances the `answer` / `judge` families, `answerOffset` stays answer-only, and `judgeOffset` stays judge-only
- `&first` no longer flows through a separate preview-global helper or repo-wide fixed lead-in layer; `PreviewSfxTiming.h` still keeps a fixed real-time `1/60 s` pre-trigger for `answer` plus tap/hold/touch-family `judge` SFX only, while slide / break-slide / firework / touchhold sustain timing should still be compared against the chart-domain shift without inheriting that extra note-category pre-trigger
- touch and touch-hold still emit `answer` when `isFirework` is set; firework is additive rather than replacing the hit-confirm sound
- ordinary hold tails emit both tail `answer` and tail `judge` events when `endSecond > second`; EX, break, and break-EX hold tails emit tail `answer` only to match MajdataEdit. Touch-hold tails stay answer-only for hit-confirm audio, while still keeping sustain start/stop span events and the firework tail event when applicable.
- head-star behavior for slide and wifi
- `hasHeadStar` gates only the pre-head object / head SFX / head judge path; `headlessImmediate` only changes the waiting-star visual ramp
- `sameHeadSlide` behavior
- `headEach` vs `slideEach`: `headEach` comes from synchronous note-head grouping, while `slideEach` must stay aligned between `SimaiNativeParser` and `TimelineQuickModel`. Multiple slide/wifi notes in the same slash each-group are yellow each tracks even when `#` / `##` timing signatures give them different `slideTraceSecond`; older slides outside that each-group must not inherit yellow solely because a later group reaches the same shoot moment.
- `trackBreak` vs `headBreak`
- touchhold span semantics
- same-second same-kind runtime/export SFX collapse to one playback using the strongest event gain, and every note-SFX kind is now latest-wins across time on both runtime preview and export; do not reintroduce per-kind overlap on only one side
- touchhold sustain is intentionally shared and non-stacking across overlapping spans in both runtime preview and export mixing
- break-slide tails emit an ordinary `break` event flagged as a break-slide tail break plus the `judge_break_slide` tail cheer; scheduling routes the flagged tail break through the internal `break_slide_tail_break` playback kind so the Break Slide volume row controls it while still using `break.wav`, and the checkbox can still filter that tail break before bucket collapse. Do not reintroduce the old `break_slide_break` event chain.
- firework timing offsets
- partial export timing: when the export request is not marked as full-range, export uses a 1.0-second preload; full-range exports use a 2.0-second lead-in. PV/BGM start from `timelineOriginSecond = segmentStart - preload`. Lead-in / preload frames clamp the renderer playhead to `segmentStart` (`qMax(0.0, ...)` for full-range) so the playfield and HUD render the chart frozen at segment-start time, not a black/transparent pre-roll; the HUD timestamp instead reads the unclamped `rawChartSecond` via `hudPlayheadSecondsOverride` so it counts up through the lead-in (e.g. `-2s → 0s` for full-range, `segmentStart-1s → segmentStart` for partial). The exported marker set is still filtered up front by `marker.second` within the simulated frame window `[timelineOriginSecond, R]`, and preview/export rendering, Muri overlays, and export SFX all consume that same filtered marker set
- `&clock_count=` is stored in `SimaiDocument::extraFields`, defaults to `4` when missing, is editable from both metadata "Other &xx Fields" and the latency settings page, and is consumed by export count-in scheduling. Export resolves BPM from `&wholebpm=` first and then the first inline `(BPM)`, and schedules `clock.wav` only for full-range exports starting at chart time `0` (mix time = full-export lead-in seconds, the HUD `00:00:000` instant) and then every quarter note, so the first tick lines up with the moment the chart begins playing rather than during the lead-in.
- same-second collapse, latest-wins scheduling, and partial-export answer clamping are shared in `src/common/PreviewSfxTimeline.h`; keep runtime `drainEvents(...)`, export render-plan building, and export backend rendering on that common path, and pass the same playback-rate / timing-settings inputs into both sides

If one side changes, inspect the other side in the same patch.

## 5. Background Media Resolution And Host Route Ownership

Current contract:

- Shared resolver: `miacode::chart_assets::resolveBackgroundMediaPath`
- Route coordinator: `src/app/mainwindow/sections/preview/MainWindow.PreviewStageMediaRoute.cpp`
- Shared preview-time audio clock getter: `MainWindow::currentPreviewAuthoritativeAudioClockSecond`
- Widget-shell preview route: `MainWindow::previewStageMediaRoute` selects `PreviewMediaController`, and `PreviewRuntime` keeps background media on the internal stage layer for both `bg.png` and `bg.mp4` / `pv.mp4`
- Quickshell-beta preview host route: `MainWindow::previewStageMediaRoute` selects `PreviewStageMediaHost` for both background images and videos
- Quickshell-beta presentation split: `QuickShellPreviewSurface.qml` keeps inline presentation for images and no-media states, while `QuickShellPreviewCompositeSurface` hosts `QuickShellPreviewSurface.qml` inside a dedicated `QQuickView` whenever the active quickshell media is video
- Export route: export stays separate from the preview host split and still consumes the shared resolver through `VideoExportController`; Windows export audio now renders a single mixed WAV through `BassExportAudioBackend`, while non-Windows keeps `LegacyExportAudioBackend` as a non-parity fallback

Timing contract:

- Preview UI follow, export-dialog current second, and weak video late-start alignment must read `MainWindow::currentPreviewAuthoritativeAudioClockSecond` instead of branching between audio and `PreviewStageMediaHost::currentPlaybackSecond()`
- `PreviewStageMediaHost::currentPlaybackSecond()` and `clockDeltaSeconds()` remain video-local observability only; they are not shared SFX/BGM/UI authority clocks
- Realtime preview BGM rate control is backend-owned: Windows keeps the BASS/BASSmix path and defaults BGM rate changes to pitch-preserving BASS_FX tempo with the compact `40/15/8` window preset; `MIACODE_BASS_BGM_RATE_MODE=rate_transpose` switches Windows BGM to source-time-priority `BASS_ATTRIB_FREQ` rate transpose for A/B diagnosis. The non-Windows compatibility backend still uses the stretched `SoundTouch` data-source path for every playback rate, including `1.0x`; do not reintroduce another runtime BGM decoder path without reviewing preview clock ownership.
- Timeline waveform cache generation is also decoder-sensitive: on Windows, `src/common/WaveformCache.cpp` decodes through BASS so MP3 delay/padding matches preview BGM playback. If the Windows preview BGM decoder changes away from BASS, review waveform cache generation and bump the waveform cache schema if the time mapping changes.
- The non-Windows stretched runtime clock is not the data-source cursor. Runtime authoritative preview audio time there now anchors to `ma_engine_get_time_in_pcm_frames()` plus the chart-second start point recorded when background playback starts; `getCursorCallback()` may still expose a raw delivery cursor for diagnostics, but it is not the authority clock.
- Before the non-Windows stretched source has emitted its first output frames after seek/start, realtime preview should keep using the fallback elapsed clock so SFX do not lock to a pre-output stretched cursor.

Current filename convention:

- `bg.mp4`
- `pv.mp4`
- `bg.jpg`
- `bg.png`
- `bg.jpeg`

If you add or remove supported media names, keep preview and export aligned.
If you change preview-time background-media ownership or media lookup, review `MainWindow.*`, `sections/preview/MainWindow.PreviewStageMediaRoute.cpp`, `PreviewMediaController.*`, `PreviewStageMediaHost.*`, `PreviewStageMediaItem.qml`, `QuickShellPreviewCompositeSurface.*`, `QuickShellPreviewSurface.qml`, and the export path in the same patch.

## 6. Track Path Resolution Exists In Multiple Places

Current lookup owners:

- `MainWindow::resolveDefaultTrackPath`
- `MainWindow::resolveLatencyDetectorTrackPath`
- `QtPreviewSfxRuntime::resolveTrackPath`

Current convention:

- chart-directory sibling `track.mp3`
- optional environment override for some paths via `MIACODE_TRACK_PATH` on main-window export path

If you support new track filenames or lookup rules, update all relevant owners and `assets-and-tools.md`.

## 7. Skin And Asset Lookup Flows Into Both Preview And Export

Asset root:

- `miacode::assets::findAssetRoot`
- `miacode::assets::assetPath`
- Skin import opens `assets/skin`; built-in and user skins are sibling child directories and only complete core skins are listed
- Timeline note art follows the current preview skin directory via `MainWindow::applyPreviewSkinDirectoryToSurfaces` -> `TimelineQuickStateBridge::setSkinDirectory`; keep `TimelineView`, `TimelineQuickTextureCache`, `TimelineSceneStateBuilder`, and DComp `TimelineSpriteAssetCache` cache invalidation aligned when changing skin lookup
- Judge-line import opens `assets/background/outlines`; custom PNG selections must flow through realtime preview and export task/snapshot state together with the built-in `PreviewOutlineVariant` fallback

Preview-time consumers:

- `MainWindow::resolvePreviewSkinDir`
- `PreviewRuntime::setSkinDirectory`
- `miacode::preview_sfx::resolveSfxDirectory`

Export-time consumers:

- `MainWindow::buildVideoExportSnapshot`
- `VideoExportSnapshot::buildVideoExportTaskFromSnapshot`
- `VideoExportController::exportPreparedTask`

If skin or SFX lookup changes, review both preview and export.

## 8. Export Snapshot Boundary Is A Contract

Export worker boundary:

1. `MainWindow::buildVideoExportSnapshot`
2. `VideoExportSnapshot::toJson`
3. `runCliVideoExportWorker`
4. `VideoExportSnapshot::fromJson`
5. `buildVideoExportTaskFromSnapshot`
6. `VideoExportController::exportPreparedTask`

Implication:

- New export settings must be added on both serialization and deserialization sides.
- Shared preview/export timing settings now also cross this boundary through `VideoExportSnapshot::toJson/fromJson` and `buildVideoExportTaskFromSnapshot`; if you add or reinterpret timing offsets, update preview state persistence and export snapshot/task reconstruction together.
- Worker protocol changes must be reflected in both `main.cpp` and MainWindow worker-event handling.
- `snapshot.outputPath` should already be the final `.mp4` path by the time the worker starts; `MainWindow` resolves missing suffixes and duplicate-name fallbacks before launching the worker so completion UI and worker results can treat it as authoritative.
- Static Muri thresholds that affect analyzer timing, such as the tap-on-slide threshold, must also cross this boundary; otherwise preview-time diagnostics and export-time overlays will drift.

Cover export note:

- Cover export is not a video-export worker snapshot path, but its chart-frame stills are rendered from a `VideoExportTask` seed through `SceneFrameRenderer` and `PreviewQuickSceneRoot`. If chart-frame render settings or layer flags change, review `SceneFrameRenderer.*`, `CoverComposerView.*`, `CoverComposer.qml`, and video export's Quick render setup together.
- Cover Studio supports multiple chart-frame layers with only one live `PreviewQuickSceneRoot`: the active chart-frame is live, while other visible frames use cached stills refreshed before final cover export.

## 9. Shared Render State Flows Through Preview And Export

Wifi-specific note:

- `RenderMode::MaimuriDxStyle` wifi track erasure is not driven by static `wifiTrackAreaCheckpoints`.
- `MuriAnalyzer` reconstructs runtime lane progress in `MarkerMuriState::wifiLaneProgressSeconds`, mirrors judged lane areas in `MarkerMuriState::wifiLaneAreas`, and records the actual `C` release time in `MarkerMuriState::wifiPadCSecond`.
- `PreviewTrackLayerState` must trim the shared middle-track body by the slowest lane's current area index, using `wifiLaneProgressSeconds` first and `wifiLaneAreas` as a fallback if the progress array is unavailable.
- In `RenderMode::MaimuriDxStyle`, wifi track completion should stay erased after the runtime clear; do not repaint a full-track flash on top of the erased body. When `wifiNeedC` is enabled, the last area must still remain visible until `wifiPadCSecond`.

Shared render settings include:

- background brightness outer and inner
- layout square scale
- fixed outline playfield diameter ratio from `src/common/LayoutRingConfig.h`; preview and export should not diverge by re-detecting ring size from texture pixels
- outline variant / judge-line background overlay selection
- smooth brightness
- background scale mode (`fill`, `fit`, `square_fit`, and `inner_circle_fit_outer_fill`; `square_fit` means center the largest 1:1 square in the render canvas, keep the outside black, and fit-contain the full PV/BG inside that square; `inner_circle_fit_outer_fill` draws a fill-cropped outer PV/BG plus a fit-contained inner copy clipped to the layout-size circle)
- tap/touch flow speed (persisted as separate values with legacy single-speed fallback)
- chart-review judge overlay toggles for slide/wifi-family and tap/hold-family effects
- timestamp/object-stats HUD flags
- Muri render options

Current Muri render-option sync points include `wifiNeedC` and `excludeTouchFromMultiTouch`; both must stay aligned across preview persistence, export snapshots, and any analyzer entry point that reconstructs runtime Muri results.

Muri warning/render note:

- `RenderMode::Native` chart-review overlays should stay parser-timed; do not retime them from Muri warning metadata.
- `RenderMode::MaimuriDxStyle` simple-note Muri overlays may still use analyzer timing metadata to switch the rendered simple effect between early-`GOOD` and early-`PERFECT`.

Owners:

- Persistent state: shared preview/render preferences are owned by `MainWindow::loadPortableState` and `MainWindow::savePortableState`; `MainWindow::loadProjectRenderState` / `saveProjectRenderState` are chart-folder metadata only and must not override those preferences on folder switch. Export-only dialog options stay under `miacode::video_export::loadDialogPreferences` / `saveDialogPreferences`.
- Preview application: `PreviewRuntime` setters, `PreviewSceneAssetRepository`, and `PreviewMediaController`
- Runtime host application: `PreviewRuntime`, cached-frame refresh, and `PreviewQuickRuntimeSurface`
- Quick layer application: `PreviewQuickSceneRoot`, `PreviewQuickStageBackgroundLayer`, `PreviewQuickBackdropLayer`, `PreviewQuickHudLayer`
- Export application: `MainWindow::buildVideoExportSnapshot`, `buildVideoExportTaskFromSnapshot`, `VideoExportController`

If you add a new render setting, wire preview persistence and export reconstruction together. Shared preview/export settings should stay canonical in the preview state; export-only choices such as resolution/FPS should persist through `VideoExportPreferences` without overriding those shared preview values on dialog open.

## 10. Parser Output Feeds Muri On Both Preview And Export Paths

Current flow:

- live preview path: `requestTimelineSlowRefresh` -> `SimaiNativeParser::parseForTimeline` -> `buildTimelinePreviewRefreshState` -> `scheduleTimelineAnalysisRefresh` -> `buildTimelineAnalysisRefreshResult`
- export path: `buildVideoExportTaskFromSnapshot` -> `MuriAnalyzer::analyze`
- full-chart normalization path: `MainWindow::onNormalizeWholeChart` -> `normalizeChartText` -> `SimaiNativeParser::buildValidationReport`

Implication:

- A marker-field change affects both live diagnostics and exported overlay behavior.
- Parser timing changes also affect whole-chart normalization output because the normalizer validates first and then rebuilds measures using the same metadata-driven timing defaults.

## 11. Common "Change Here, Check There" Pairs

- Change `SimaiNativeParser`:
  - Check `MainWindow.ValidationFlow.cpp`
  - Check `MainWindow.PreviewTimelineFlow.cpp`
  - Check `TimelineQuickModel.cpp`
  - Check `ChartNormalization.cpp`
  - Check `VideoExportSnapshot.cpp`
  - Check `MuriAnalyzer.cpp`
- Change preview SFX mapping:
  - Check `VideoExportAudioRenderPlan.cpp`
  - Check `VideoExportAudioBackend` implementations
  - Check `docs/specs/preview/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`
- Change export media rules:
  - Check `PreviewMediaController.cpp`
  - Check packaging or ffmpeg assumptions if format support changes
- Change preview timing constants:
  - Check `PreviewGameplayConfig.h`
  - Check `src/preview/scene/PreviewOpacityCurves.cpp`
  - Check `VideoExportController.cpp` diagnostics and timeline assumptions
- Change Muri static thresholds:
  - Check `MuriConfig.h`
  - Check any UI or settings entry that surfaces the threshold
  - Check `VideoExportSnapshot.cpp` and `VideoExportController.cpp` so export keeps the same analyzer threshold as preview
- Change Muri list anchoring or overlap dedupe:
  - Check `MuriPanelEntries.cpp`
  - Check `MainWindow.ValidationFlow.cpp`
  - Check `MuriSpec.cpp`

## Update This File When

- A behavior starts or stops being mirrored across two code paths.
- A new serialized export field is introduced.
- A duplicated lookup rule is centralized or split further.
- A timing rule starts affecting another subsystem that did not previously depend on it.
