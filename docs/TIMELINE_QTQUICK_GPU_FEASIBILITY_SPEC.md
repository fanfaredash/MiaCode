Status: Draft implementation-oriented RFC

# Timeline Qt Quick + GPU Implementation Spec

This document describes how MiaCode should implement a future Timeline migration from the current QWidget/QPainter path to a Qt Quick + GPU-rendered path.

This is not a product-direction debate. The premise is that the project has already decided to move the entire bottom-tabs cluster to Qt Quick. The purpose of this document is to freeze the current Timeline semantics, define the new Quick host/runtime policy that Timeline should share with Preview, describe the GPU-side rendering strategy in implementation terms, and document the focus/shortcut/theme/tab-host risks that must be handled as part of the rewrite.

Current source references used while building this spec:

- `src/timeline/TimelineView.h`
- `src/timeline/TimelineView.cpp`
- `src/timeline/TimelineView.Core.cpp`
- `src/timeline/TimelineView.Paint.cpp`
- `src/timeline/TimelineView.Interaction.cpp`
- `src/timeline/TimelineQuickModel.h`
- `src/timeline/TimelineQuickModel.cpp`
- `src/timeline/TimelineRenderData.h`
- `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp`
- `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrapFinalize.cpp`
- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- `src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp`
- `src/app/mainwindow/sections/window/MainWindow.WindowInteraction.cpp`
- `src/app/mainwindow/sections/window/MainWindow.WindowShell.cpp`
- `src/app/mainwindow/sections/document/MainWindow.DocumentUi.cpp`
- `src/app/mainwindow/sections/document/MainWindow.DocumentEditorState.cpp`
- `src/app/mainwindow/sections/validation/MainWindow.ValidationFlow.cpp`
- `src/app/mainwindow/sections/validation/MainWindow.ValidationRuntime.cpp`
- `src/editor/PlainCodeEditor.h`
- `src/editor/PlainCodeEditor.cpp`
- `src/app/main.cpp`
- `src/app/quick_shell/QuickShellPreviewSurfacePolicy.h`
- `src/app/quick_shell/QuickShellStyleBridge.h`
- `src/app/quick_shell/QuickShellStyleBridge.cpp`
- `src/app/quick_shell/QuickShellBootstrap.cpp`
- `src/app/quick_shell/QuickShellController.cpp`
- `src/app/quick_shell/qml/QuickShellMain.qml`
- `src/app/ui/UiTheme.h`
- `src/app/ui/UiTheme.cpp`
- `docs/PREVIEW_NATIVE_WINDOW_DEVELOPMENT_PLAN_SPEC.md`
- `docs/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`
- `docs/QT_QUICK_FULL_FRONTEND_MIGRATION_PLAN.md`

## 1. Scope And Intent

### In scope

- Freeze the current Timeline semantic contract before the renderer rewrite starts.
- Define the current ownership boundaries between Timeline data, Timeline rendering, editor synchronization, preview synchronization, validation/Muri tabs, and window-shell input routing.
- Define the shared Quick runtime/backend policy that Timeline should use together with Preview.
- Describe the Timeline GPU rendering strategy in implementation terms: layer order, sprite/batch construction, cache lifetime, invalidation triggers, text strategy, and color/theme handling.
- Document the host, focus, shortcut, modality, activation, and shared-tab-host risks introduced by a Quick Timeline.
- Recommend concrete code entry points, file boundaries, configuration abstractions, and migration phases.

### Out of scope

- Implementing the rewrite in this document.
- Replacing `TimelineQuickModel` parsing semantics.
- Replacing preview playback, audio, or slow-refresh ownership.
- Replacing the current editor widget with a Quick editor.
- Replacing the current tool dialogs with Quick dialogs.

### Working rule

- The bottom-tabs cluster is now a Qt Quick target by decision.
- The QWidget Timeline remains the semantic reference implementation until the parity checklist is green.
- The Timeline rewrite must not be treated as an isolated renderer swap; it is part of a Quick bottom-tabs host that also owns Validation and Muri tabs.

## 2. Current Ownership And Host Boundaries

| Area | Current owner | Responsibility today | Why it matters to the Quick rewrite |
|---|---|---|---|
| Fast Timeline parse and anchor model | `TimelineQuickModel` in `src/timeline/TimelineQuickModel.*` | Incremental text-to-timeline parsing, cursor anchors, preview-follow spans, nearest-note lookup, visible-line snapshot data | This remains the semantic authority. A Quick renderer consumes its outputs rather than replacing it. |
| Render snapshot types | `src/timeline/TimelineRenderData.h` | `TimelineRenderSnapshot`, per-line note/beat data, note flags, visible note helpers, prefix-max note visibility helpers | This is the stable parse/render boundary. The Quick path should consume this shape or a derived scene state built from it. |
| Widget Timeline rendering | `TimelineView` in `src/timeline/TimelineView.*` | QPainter rendering, zoom presets, waveform strip, header controls, playhead/cursor visuals, drag/wheel interactions, Muri dots | This is the reference renderer and interaction surface. Every Quick behavior must be checked against it. |
| Main window Timeline fast path | `MainWindow::applyTimelineQuickChange()` and `refreshTimelineQuickModelFromCurrentText()` in `MainWindow.PreviewTimelineFlow.cpp` | Push editor text changes into `TimelineQuickModel`, update `TimelineView` immediately, refresh preview slider range | The Quick Timeline must stay on the same fast path so typing latency does not regress. |
| Main window Timeline slow path | `requestTimelineSlowRefresh()`, `dispatchTimelineSlowRefresh()`, `scheduleTimelineAnalysisRefresh()` in `MainWindow.PreviewTimelineFlow.cpp` | Full parser refresh, preview note-marker publication, validation/Muri rebuild scheduling | The renderer rewrite must not absorb this work. It stays as business logic outside the renderer. |
| Timeline-to-preview command routing | `MainWindow.FrameBootstrap.cpp` | Connect Timeline signals to seek, pause, follow-toggle, and editor/preview synchronization | The Quick Timeline must expose the same semantic signals or be adapted to the same interface. |
| Preview playback-to-Timeline updates | `MainWindow.TimelinePlayback.cpp` | Drive `R`, `L`, preview follow, playback entry, paused seek visual state, preview slider and stats coupling | The Quick Timeline must still receive playhead/entry updates from this layer. |
| Shared bottom-tabs host | `bottomTabs_` setup in `MainWindow.FrameBootstrap.cpp`, page switching in `MainWindow.DocumentUi.cpp` | Host `Timeline`, `Syntax Check`, and `Muri Check`; hide/show with page mode; restore Timeline as current when entering chart mode | The Timeline rewrite sits inside a shared host boundary. Tab selection, visibility, and resize behavior are not Timeline-local. |
| Validation and Muri issue tabs | `MainWindow.ValidationFlow.cpp` and `MainWindow.ValidationRuntime.cpp` | Issue list rendering, activation, context menu actions, tab visibility changes, current-index restore, wrapped-row relayout | These tabs now belong to the same Quick bottom-tabs cluster target and must be planned together. |
| Window-shell focus and shortcut routing | `MainWindow.WindowInteraction.cpp` and `MainWindow.WindowShell.cpp` | Preview key scope, slider key routing, editor `Ctrl+click`, text-focus restore, quick-shell shortcut forwarding | Changing the Timeline surface changes who owns focus and which layer sees key events first. |
| Editor-side caret behavior | `PlainCodeEditor` in `src/editor/PlainCodeEditor.*` | Cursor display, follow cursor application, undo/redo shortcut override, focus visual state | Timeline follow and editor cursor sync depend on these rules surviving the host change. |
| Preview surface-mode policy | `src/app/quick_shell/QuickShellPreviewSurfacePolicy.h` | Current quick-shell preview chooses separate surface only for video media | This is the current concrete example of module-level Quick surface policy and should inform Timeline policy design. |
| Quick host palette/metrics bridge | `QuickShellStyleBridge` in `src/app/quick_shell/QuickShellStyleBridge.*` | Publishes palette and metrics maps to Quick UI, refreshes on theme/font/palette/window changes | This is the existing Quick-side theme and metrics bridge that Timeline/Validation/Muri should reuse rather than duplicate. |
| Application startup and graphics setup | `src/app/main.cpp` | `Qt::AA_DontCreateNativeWidgetSiblings`, optional `QSG_RENDER_LOOP=basic`, graphics API selection for CLI export paths | The shared policy for Preview and Timeline must match the real startup behavior rather than outdated assumptions. |

Current absence worth calling out:

- There is no active Quick bottom-tabs host model in the repository.
- There is no active Quick issue-list model for Validation/Muri.
- There is no current Quick Timeline bridge implementation to revive.

The implementation should therefore treat the bottom-tabs move as a new Quick host cluster, not as a partial restoration of an old bridge layer.

## 3. Current Semantic Contract

### 3.1 Runtime coordinates and UI markers

- `L` is the preview playback entry second, stored on the Timeline side as `TimelineView::playbackEntrySeconds_`.
- `R` is the runtime preview/playhead second, stored as `TimelineView::playheadSeconds_`.
- `C` is the editor/timeline cursor anchor second, stored as `TimelineView::cursorSeconds_`.
- `TimelineView` visually renders all three:
  - `L` as the top triangle playback-entry marker.
  - `R` as the playhead line.
  - `C` as the cursor line.
- `TimelineView::focusPlayhead()` and `focusCursor()` only choose which semantic target the viewport centers around; they do not redefine `L`, `R`, or `C`.
- During preview playback, `MainWindow::flushQtPreviewTimelinePosition()` keeps `R` visually centered in the Timeline viewport.
- During paused positioning, `R` follows explicit caller intent: some callers request center-view, some only update the marker.

### 3.2 Current Timeline complete semantics table

| Operation | Current owner(s) | Timeline-local effect | Cross-region effect | Contract that the Quick path must preserve |
|---|---|---|---|---|
| Chart text edit | `MainWindow.FrameBootstrap.cpp`, `MainWindow.PreviewTimelineFlow.cpp`, `TimelineQuickModel::applyContentsChange()` | Rebuild or incrementally patch Timeline snapshot, repaint Timeline immediately | Queue slow refresh, update preview slider range, optionally refresh follow-decoration scheduling | Typing must continue to hit the fast path first and must not wait for the slow parser or the GPU renderer. |
| Full Timeline rebuild from current text | `refreshTimelineQuickModelFromCurrentText()` | Replace Timeline snapshot from current editor text | Recompute slider range and keep preview-side slow refresh separate | Full rebuild semantics must remain identical to the widget path. |
| Slow refresh publish | `requestTimelineSlowRefresh()` and `dispatchTimelineSlowRefresh()` | No direct Timeline paint semantics beyond new note-marker snapshot availability | Publish preview note markers, schedule validation/Muri analysis, possibly auto-start pending preview playback | The rewrite must not change slow-refresh ownership or the latest-only snapshot contract. |
| Editor cursor move or normal editor click | `cursorPositionChanged` hookup, `syncTimelineToEditorCursor()` | Update `C` and optionally center Timeline on `C` when preview is paused | No forced preview seek; may update preview-follow decoration later | Normal editor movement changes `C`, not `R`. |
| Editor `Ctrl+LeftClick` release | `WindowSection::eventFilter()` | Set Timeline cursor marker to the resolved second and focus cursor | Stop preview if needed, seek preview to the second resolved from current editor location | This is an explicit `C -> R` bridge and must not be lost in the Quick rewrite. |
| Timeline header click | `TimelineView::mousePressEvent()`, `headerNavigateRequested`, `navigateTimelineToSecond()` | Move Timeline `C` to the resolved anchor second, focus playhead side | Seek preview to clicked second, move editor cursor to the anchor line/column, set status text | Header clicks are cross-region navigation commands, not passive markers. |
| Timeline `Ctrl/Meta + click` in the body | `TimelineView::mousePressEvent()` | Same as header-click behavior | Same as header-click behavior | Body modifier-click currently means navigate-by-time, not note picking. |
| Timeline drag scrub start/move/release | `TimelineView::mousePressEvent()/mouseMoveEvent()/mouseReleaseEvent()`, `centerNavigateRequested`, `timelineDragFinished` | Temporarily suppress playhead indicator, drag viewport, keep `R` at viewport center | Stop preview if it was playing, debounce preview seek during drag, commit final preview seek on release | Dragging Timeline edits runtime `R`, not editor `C`. |
| Timeline wheel pan | `TimelineView::wheelEvent()` | Horizontal pan, optionally re-center `R` before interaction, restore playhead indicator after interaction | Stop preview if needed, debounce preview seek via `centerNavigateRequested` | Plain wheel is a scrub/seek gesture, not zoom. |
| Timeline `Alt+wheel` zoom | `TimelineView::handleAltZoomWheel()` | Step through fixed zoom presets anchored at viewport center | No preview seek side effect by itself | Zoom semantics are discrete and preset-based. |
| Timeline `Space` while Timeline has focus | `TimelineView::keyPressEvent()` | No local state change beyond event acceptance | Emit preview play/pause request | A Quick Timeline must keep Timeline-local `Space` scoped to preview transport. |
| Timeline `Left/Right` while Timeline has focus | `TimelineView::keyPressEvent()`, held-scroll timer | Horizontal viewport movement with acceleration; does not mutate `C` | If preview was playing, interaction first stops playback through `timelineUserInteractionStarted` | Timeline arrows are viewport navigation, not playhead stepping. |
| Timeline follow toggle | `TimelineView` checkbox, `followPreviewToggled`, `syncEditorCursorToPreviewSecond()` | Toggle follow mode in the Timeline header | Persist preference, update editor follow decoration or cursor binding immediately | The Quick Timeline must preserve the same application-scoped follow-toggle semantics. |
| Preview slider press/move/release | `FrameBootstrapFinalize.cpp`, `WindowInteraction.cpp` | No local Timeline action | Stop held seek, stop preview playback on drag start, seek preview continuously, update Timeline `R` through preview positioning | Slider seek remains preview-owned; Timeline follows preview, not vice versa. |
| Preview playback start/resume | `startQtPreviewPlayback()` | Set `L`, clamp playhead upper bound, update Timeline `R`, focus playhead | Start preview SFX/media/runtime snapshot, seed playback clocks | Timeline playback entry and playhead behavior must stay tied to preview start. |
| Preview tick while playing | `onQtPreviewTickAtSecond()` and `flushQtPreviewTimelinePosition()` | Update Timeline `R` at fixed UI cadence and keep playhead centered | Drain preview SFX, sync stage media, queue editor follow updates when follow is enabled | The Quick path must preserve the current Timeline-side cadence. |
| Preview stop/pause/end | `stopQtPreviewPlayback()` | Freeze Timeline `R` at paused second, restore paused playhead upper bound, keep focus on playhead side | Apply latest paused preview snapshot, refresh stats, possibly apply deferred analysis UI | Pausing preview must not collapse `R` and `C` into one marker. |
| Paused preview follow decoration | `updatePreviewFollowDecorationForTimelineBlueLine()` | Does not move editor caret; keeps Timeline `C` aligned with the resolved anchor | Paint editor-side follow decoration and optionally ensure visibility | Paused follow is decoration-only, not a caret move. |
| Active preview follow | `syncEditorCursorToPreviewSecond()` and `PlainCodeEditor::applyPreviewFollowCursor()` | Update Timeline `C` from the resolved follow span | Move editor caret or selection to the latest valid anchor at or before preview second | Playing follow is cursor-binding behavior, not only decoration. |
| Explicit validation run | `MainWindow.ValidationRuntime.cpp` | May switch away from Timeline to the validation tab | Focus first validation issue and move editor caret | The shared bottom-tabs host must preserve tab switching initiated by non-Timeline logic. |
| Muri item activation | `MainWindow.ValidationFlow.cpp` | Keep the shared tab shell active while Timeline markers update from the Muri second | Jump editor to source, seek preview, set Timeline cursor marker, focus playhead | Timeline semantics are already coupled to the Muri tab. |

### 3.3 Shared bottom-tabs contract

- `bottomTabs_` currently hosts three peer surfaces:
  - `Timeline`
  - `Syntax Check`
  - `Muri Check`
- Switching into chart mode currently makes the Timeline tab visible and forces it current.
- Switching away from chart mode hides the whole bottom-tabs area and hides the validation tab.
- `bottomTabs_->currentChanged` currently triggers wrapped-row relayout scheduling for the Validation and Muri lists.
- Validation and Muri actions can temporarily move tab selection away from Timeline.
- Ignoring issue types from a context menu currently restores the previous tab index after refreshing issue lists.

This means the Quick Timeline rewrite is not only a renderer change. It is also a Quick bottom-tabs host decision.

## 4. Shared Quick Runtime Selection And Backend Policy

### 4.1 Goal

Timeline and Preview should share one Quick runtime/backend policy rather than each inventing its own startup and module-loading rules.

The current preview path already has the beginnings of such a split:

- application-level startup behavior lives in `src/app/main.cpp`
- module-level preview surface-mode choice currently lives in `QuickShellPreviewSurfacePolicy.h`
- Quick-side palette and metrics publishing already lives in `QuickShellStyleBridge`

The Timeline rewrite should build on that direction instead of duplicating it.

### 4.2 Proposed shared policy abstractions

The documentation should lock these names now so the implementation phase does not need to re-decide them:

- `QuickRenderPolicyConfig`
  - application-level
  - owns graphics API, render loop, native-sibling guard, startup diagnostics policy, and runtime policy logging
- `QuickModuleRenderPolicy`
  - module-level
  - owns `surfaceMode`, `allowSeparateSurface`, and `fallbackBehavior`
  - must cover at least `preview` and `timeline`

Recommended module policy shape:

- `surfaceMode`
  - `Inline`
  - `SeparateSurfaceDiagnostic`
- `allowSeparateSurface`
  - module-specific boolean gate
- `fallbackBehavior`
  - diagnostic or stability fallback description used by logs and tests

### 4.3 Application-level rules

- Graphics API is an application-level decision chosen before the relevant Quick runtime starts. Preview and Timeline must not each choose their own graphics backend independently.
- `Qt::AA_DontCreateNativeWidgetSiblings` remains an application-level startup rule and must continue to be logged.
- `QSG_RENDER_LOOP` behavior remains an application-level startup rule and must continue to be logged.
- The shared policy must record the actual GUI runtime backend separately from the CLI export/worker backend.

### 4.4 Module-level rules

- Timeline should default to the same-window inline Quick path.
- Timeline should not adopt Preview's video-specific separate-surface path as a normal default.
- If Timeline ever exposes a separate-surface mode, it should only exist as a diagnostic or fallback branch inside the shared module policy.
- Preview and Timeline should write policy decisions into the same startup/runtime diagnostics stream so backend/surface mismatches are visible in one place.

### 4.5 Current preview behavior that Timeline should reference

Current repo facts:

- `shouldUseSeparatePreviewSurface(bool quickShellFrontend, bool hasVideoMedia)` currently returns true only when the quick-shell frontend is active and the stage media is video.
- The current Quick shell therefore already distinguishes between inline and separate-surface behavior at a module level.
- `QuickShellStyleBridge` already publishes shared palette and metrics data into Quick UI.

Timeline should treat this as the upper-bound reference:

- default inline
- separate surface only when explicitly justified by module policy
- shared palette/metrics bridge
- shared startup/backend diagnostics

### 4.6 Program startup behavior

The implementation spec should treat startup sequencing as:

1. Read `QuickRenderPolicyConfig` before constructing the Quick GUI runtime.
2. Apply application-level startup guards such as:
   - native-sibling behavior
   - optional render-loop override
   - graphics API selection if policy requires it
3. Start the Quick host window.
4. Assign module-level policies for Preview and Timeline.
5. Log both the application-level and module-level policy decisions.

Recommended switchability rules:

- restart-required:
  - graphics API changes
  - render loop changes
  - startup native-sibling guard changes
- runtime-switchable:
  - Timeline inline/surface diagnostic mode, if the implementation provides it
  - theme-driven visual revisions
  - some cache policy toggles for diagnostics

## 5. Render/Dataflow Decomposition

### 5.1 What must stay where

| Layer | Keep / move | Required owner |
|---|---|---|
| Text parsing, cursor-anchor resolution, follow-span resolution, nearest-note lookup | Keep | `TimelineQuickModel` |
| Preview start/stop, paused seek, playback cadence, slider coupling, follow scheduling | Keep | `MainWindow.TimelinePlayback.cpp` and `MainWindow.PreviewTimelineFlow.cpp` |
| Editor caret moves and text-focus restore | Keep | `PlainCodeEditor` and `WindowSection` |
| Validation/Muri tab switching, tab visibility, current-index restore | Keep | `DocumentUi`, `ValidationFlow`, `ValidationRuntime`, and the shared tab orchestration |
| Timeline pixel-layout derivation that is renderer-agnostic | Move out of QWidget-only code | New pure C++ scene-state builder under `src/timeline/` |
| Actual Timeline drawing primitives | Replace | New QSG-based Timeline renderer |
| Bottom-tabs host shell | Replace | New Quick bottom-tabs host |
| Validation tab surface | Replace | New Quick issue-list surface |
| Muri tab surface | Replace | New Quick issue-list surface |
| Tab selection, tab visibility, and Quick-side focus handoff | Replace, but keep business ownership in C++ | New Quick bottom-tabs host adapter |

### 5.2 Recommended architecture

The recommended implementation shape is:

1. Keep `TimelineQuickModel` as the semantic model.
2. Add a renderer-facing state builder that converts current model outputs plus view state into a pure `TimelineSceneState`.
3. Implement a `TimelineQuickItem` that consumes that scene state and emits the same semantic signals as `TimelineView`.
4. Introduce a `BottomTabsQuickHost` concept that owns:
   - Timeline tab content
   - Validation tab content
   - Muri tab content
   - current-index, visibility, and focus handoff rules
5. Keep `MainWindow` as the business owner for:
   - tab selection intent
   - validation/Muri publication
   - editor/preview linkage
   - playback/follow semantics

### 5.3 Recommended implementation entry points

The first code changes should start here:

- `src/timeline/TimelineSceneState.h/.cpp`
  - renderer-agnostic Timeline scene payload
- `src/timeline/TimelineSceneStateBuilder.h/.cpp`
  - builds `TimelineSceneState` from:
    - `TimelineRenderSnapshot`
    - waveform data
    - playhead/cursor/entry seconds
    - zoom level
    - viewport size
    - Muri marker ids
    - theme revision
    - interaction state
- `src/timeline/quick/TimelineQuickItem.h/.cpp`
  - `QQuickItem`-based Timeline surface
- `src/timeline/quick/TimelineQuickTextureCache.h/.cpp`
  - texture/atlas cache for Timeline assets
- `src/timeline/quick/TimelineQuickGridLayer.*`
- `src/timeline/quick/TimelineQuickWaveformLayer.*`
- `src/timeline/quick/TimelineQuickHeaderLayer.*`
- `src/timeline/quick/TimelineQuickNotesLayer.*`
- `src/timeline/quick/TimelineQuickOverlayLayer.*`
- `src/app/quick_shell/BottomTabsQuickHost.qml` or equivalent
  - Quick tab host for Timeline / Validation / Muri
- `src/app/quick_shell/ValidationIssueListQuickView.*` and `MuriIssueListQuickView.*`, or a shared issue-list Quick surface with mode-specific shaping

### 5.4 Required public/semantic interface

The Quick Timeline should intentionally mirror the current widget Timeline interface so the business layer can stay stable:

- `setTimelineData(const TimelineRenderSnapshot&)`
- `setWaveformData(...)`
- `clear()`
- `setPlaybackEntrySeconds(double)`
- `setPlayheadUpperLimitSeconds(double)`
- `setPlayheadSeconds(double, bool centerView)`
- `setCursorSeconds(double, bool centerView)`
- `focusPlayhead(bool centerView)`
- `focusCursor(bool centerView)`
- `setShowSlideTracks(bool)`
- `setMuriAnalysisReport(const MuriAnalysisReport&)`
- `setFollowPreviewEnabled(bool)`
- `zoomScale() const`
- signals:
  - `playheadChanged(double)`
  - `headerNavigateRequested(double)`
  - `centerNavigateRequested(double)`
  - `timelineDragStarted()`
  - `timelineDragFinished(double)`
  - `timelineUserInteractionStarted()`
  - `followPreviewToggled(bool)`
  - `previewPlayPauseRequested()`

The Quick bottom-tabs host should expose host-level semantics, not business logic:

- `currentTabId`
- `setCurrentTabId(...)`
- `setTimelineTabVisible(bool)`
- `setValidationTabVisible(bool)`
- `setMuriTabVisible(bool)`
- `restorePreviousCurrentTabAfterRefresh(...)`

### 5.5 What must not move into QML state

The following must stay in C++ business orchestration rather than ad-hoc QML animation or state logic:

- stopping preview when Timeline interaction begins
- preview seek debouncing during drag
- playback entry updates on preview start/resume
- paused-preview follow decoration vs active-preview follow cursor movement
- slow-refresh vs fast-refresh ownership
- validation/Muri publication and current-tab restore rules
- editor caret movement and selection restoration
- text-focus restore on activation changes

## 6. GPU Rendering Strategy

### 6.1 Layer order

Recommended Timeline layer order:

1. background / panel fill
2. lane rows
3. waveform
4. grid minor
5. grid major
6. header markers and labels
7. firework underlay spans
8. slide/wifi track layer
9. tap/hold/slide/wifi head layer
10. touch layer
11. touch-hold overlay layer
12. Muri marker overlay
13. `L / C / R` markers and drag-center overlay

Rules:

- Layers whose content is position-only should prefer lightweight geometry or instance updates.
- Layers whose semantics are affected by theme-only changes should support color/material refresh without unnecessary cache rebuilds.
- Marker overlays should be isolated from the expensive note-content layers so playhead/cursor updates can stay cheap.

### 6.2 Sprite strategy

The current Timeline asset scale is small enough to treat as a modest sprite family within one skin lifecycle.

Implications:

- normal note heads should use one atlas or a very small number of atlases
- EX/each/break variants can remain baked or pre-composed within that small atlas family
- touch and touch-hold families can also be included in the same small atlas system
- hold/touch-hold can continue using texture-based cap/body slicing if that remains simpler than procedural geometry
- slide/wifi tracks should not pre-bake a large rotation matrix of track textures for every angle

Recommended split:

- sprite-backed:
  - note heads
  - touch/touch-hold art
  - header marker triangle, if desired
- geometry or instance-transform backed:
  - grid lines
  - waveform bars
  - slide/wifi tracks
  - playhead/cursor/entry markers
  - Muri dots
  - firework underlay spans

### 6.3 Batch strategy

Recommended batch keys:

- layer
- texture/atlas identity
- blend mode
- material family
- theme revision if theme is baked into the texture cache

Recommended batch families:

- instance or geometry batch:
  - waveform bars
  - grid lines
  - playhead/cursor/entry markers
  - Muri dots
  - firework underlay spans
- sprite batch:
  - note heads
  - touch/touch-hold sprites
  - any texture-backed hold caps/bodies
- transform-based batch:
  - slide/wifi tracks if texture-backed but angle-transformed at draw time

Why this is appropriate for Timeline:

- the asset family is small
- stable per-frame texture identity is achievable
- Timeline does not need a large streaming texture system
- a small-atlas plus stable-batch strategy is a better fit than a complex video-style surface pipeline

### 6.4 Cache lifetime and invalidation

The implementation must explicitly document and wire these invalidation triggers:

- skin change
- theme change
- graphics backend change
- device pixel ratio change
- font change
- metrics change
- zoom bucket change
- surface mode change

Expected invalidation classes:

- color/material update only:
  - theme-only changes for pure geometry layers
  - playhead/cursor/entry marker color updates
- atlas rebuild required:
  - skin change
  - theme change if note art is theme-baked
  - DPR change if atlas resolution is DPR-sensitive
- scene-state rebuild required:
  - zoom bucket change
  - viewport size change
  - timeline snapshot change
  - waveform visible-range change
- full renderer/context reset:
  - graphics backend change
  - surface mode transition that invalidates the current texture cache or window-bound resources

### 6.5 Text and label strategy

- Timeline header labels must keep their layout policy on the CPU side.
- Quick rendering should only consume already-laid-out label positions and strings.
- Validation/Muri issue-list text in Quick tabs should use one explicit strategy:
  - Quick text items for maintainability, or
  - text textures if profiling later proves the need

The first implementation should bias toward maintainable Quick text for issue tabs unless measurements prove that text rendering dominates the frame.

## 7. CPU Optimization Inventory And GPU Mapping

| Current optimization | Current owner | Current CPU role | Keep CPU-side | Re-express on GPU | Drop literal widget form | Notes |
|---|---|---|---|---|---|---|
| Incremental text apply and bounded reparse | `TimelineQuickModel::applyContentsChange()` | Reparse only the affected suffix and stop when parse state converges | Yes | No | Yes | Mandatory for typing latency. |
| Cursor-anchor caches by line and by absolute second | `segmentStarts`, `cursorAnchorsBySecond_` | Fast `C` lookup from editor position or preview second | Yes | No | No | Semantic state, not render cache. |
| Follow-selection range cache | `rebuildFollowSelectionRanges()` | Avoid repeatedly trimming the same segment for overlay selection | Yes | No | No | Needed for paused follow parity. |
| Follow-selection span cache | `rebuildFollowSelectionSpans()` | Precompute visible follow spans and caret endpoints | Yes | No | No | Needed for active follow parity. |
| Prefix-max note visual end arrays | `noteVisualEndPrefixMaxWithSlideTracks`, `...WithoutSlideTracks` | Skip lines whose note visuals cannot intersect the viewport | Yes | Indirectly through smaller scene state | Yes | One of the most valuable culling optimizations. |
| Visible line range search | `visibleLineRange()` and `timelineRenderVisibleNoteLineRange()` | Bound per-frame line and note work | Yes | Indirectly through smaller GPU work submission | Yes | Still useful with QSG. |
| Waveform multi-resolution data and visible-column selection | waveform helpers | Avoid dense waveform work at every zoom level | Yes | Yes, render only the chosen slice | Yes | GPU should not upload full waveform data blindly. |
| Partial marker strip repaint | `updateTimelineMarkerStrip()` | Repaint only playhead/cursor strips if scroll does not change | No | Yes, as overlay revision / cheap layer update | Yes | Preserve the cheap-update goal, not the widget API. |
| Icon transform cache | `transformedIconCache_` | Avoid repeated scale/rotate/mirror work on `QPixmap` | Partially | Yes, as atlas/texture cache | Yes | Separate base atlas from theme-baked cache. |
| Hold-cap/body slice cache | `holdPixmapPartsCache_` | Avoid re-slicing hold bodies every paint | Partially | Yes, if hold stays texture-based | Yes | If holds become procedural, this cache can disappear. |
| Composite icon prebuild for EX/touch/touch-hold variants | `loadNoteIcons()` | Collapse layered asset composition ahead of paint | Partially | Yes, at atlas build time | Yes | Still useful with a small sprite family. |
| Header label collapse and spacing logic | `visibleHeaderLineLabels()` | Avoid overlapping labels and reduce clutter | Yes | No | No | CPU-side layout policy should remain authoritative. |
| Leading/trailing centering padding | `leadingCenteringPadding()`, `trailingCenteringPadding()` | Keep zero and max seconds centerable | Yes | No | No | Core view math. |
| Playhead indicator suppression/restore timer | `suppressPlayheadIndicatorForInteraction()` | Reduce visual conflict during drag/wheel scrub | Yes, as interaction state | Yes, in overlay visibility | Yes | Preserve behavior, not widget-specific machinery. |
| Wrapped row relayout in issue tabs | `scheduleWrappedListRelayout()` and `relayoutWrappedListRows()` | Keep Validation/Muri rows correct on resize and tab changes | Keep equivalent sizing policy | Yes, as Quick tab host layout contract | Yes | Not a Timeline renderer optimization, but a bottom-tabs Quick-host constraint. |

## 8. Color And Theme Management

### 8.1 Required owner

Timeline color management should be centralized into a single dedicated owner:

- `src/common/TimelineThemeConfig.h`

This file should become the source of truth for Timeline color tokens and closely related visual constants.

### 8.2 Minimum token groups

At minimum, the centralized Timeline theme config should own:

- window / header / sidebar / base
- lane even / lane odd
- border / axis / grid major / grid minor
- label / text secondary
- waveform
- playhead / cursor / entry marker
- Muri marker
- firework band palette

### 8.3 Rules

- Timeline colors should stop being assembled ad hoc inside `TimelineView.Paint.cpp`, `TimelineView.Core.cpp`, or future Quick layers.
- The Quick renderer should consume Timeline-specific tokens from one source rather than re-reading scattered `UiTheme::colors()` fields throughout multiple files.
- The shared Quick host theme bridge should propagate theme revisions into Timeline, Validation, and Muri tabs.
- For each token family, the implementation must document whether it is:
  - uniform/material-driven and safe for in-place updates, or
  - baked into a texture cache and therefore requires rebuild on theme change

## 9. Theme Switching Behavior

### 9.1 Source of truth

- The source of truth for application theme remains `UiTheme`.
- The source of truth for Quick-side palette and metrics propagation remains the Quick host bridge shape already exemplified by `QuickShellStyleBridge`.
- Timeline, Validation, and Muri Quick tabs should all consume the same palette/metrics revision stream.

### 9.2 Required behavior on theme change

- The current tab must not be lost.
- Focus must not be stolen by the theme change itself.
- Playback, follow state, selection state, and marker positions must remain intact.
- Pure color layers should prefer in-place material/color updates.
- Texture caches that are theme-baked must rebuild when the theme revision changes.
- Text, font, and metrics-sensitive caches should rebuild only when the triggering input actually changes.

### 9.3 Layer expectations

- pure geometry/color layers:
  - lane rows
  - grid
  - markers
  - Muri dots
  - firework underlay
  - should normally update through material/color revision only
- sprite-backed layers:
  - note heads
  - hold/touch-hold textures
  - any theme-baked icon atlas
  - may require atlas rebuild if theme is encoded into the textures
- text-bearing layers:
  - header labels
  - tab labels
  - issue-list text
  - must preserve current tab/focus state while refreshing their visuals

## 10. Host, Focus, Shortcut, And Modality Risk Analysis

### 10.1 Current focus and shortcut owners

- Timeline-local keys live in `TimelineView::keyPressEvent()` and `keyReleaseEvent()`.
- Timeline mouse semantics live in `TimelineView::mousePressEvent()`, `mouseMoveEvent()`, `mouseReleaseEvent()`, and `wheelEvent()`.
- Preview key scope for the widget shell lives in `WindowSection::eventFilter()`.
- Preview slider mouse and key routing also lives in `WindowSection::eventFilter()` plus `FrameBootstrapFinalize.cpp`.
- Text-focus restore across app activation lives in `WindowSection::rememberFocusedTextEditState()` and `restoreFocusedTextEditStateAttempt()`.
- Editor-side undo/redo shortcut override lives in `PlainCodeEditor::event()`.
- Quick-shell fullscreen and transport-level preview keys live in:
  - `QuickShellBootstrap::eventFilter()`
  - `QuickShellController`
  - `QuickShellMain.qml` focus scopes and `Shortcut` items

### 10.2 Risk matrix

| Concern | Current owner | Current behavior | Risk in a Quick bottom-tabs cluster | Required mitigation |
|---|---|---|---|---|
| Timeline gains focus on click | `TimelineView::mousePressEvent()` | Timeline becomes focused and immediately emits Timeline-specific actions | A Quick item may not receive keys if focus remains on a retained-native widget or on the surrounding host window | The Timeline Quick item must explicitly acquire active focus, and the host must route focus into it on Timeline clicks. |
| Preview key scope overlap | `WindowSection::eventFilter()` | Preview panel, preview slider, preview canvas, and fullscreen hosts consume `Space`, `F11`, `Esc`, `Left`, `Right` in specific scopes | A Quick bottom-tabs cluster can easily compete with preview transport and fullscreen scopes | Define one host-level precedence table before implementation. |
| Quick tab navigation vs Timeline keys | shared bottom-tabs host | Timeline currently does not share a Quick tab host | A Quick tab bar or tab host may intercept arrows, `Space`, or focus traversal before Timeline gets them | The Quick bottom-tabs host must define which keys are host-owned vs tab-content-owned. |
| Timeline `Space` | `TimelineView` | `Space` toggles preview play/pause only when Timeline has focus | Quick focus scopes can duplicate or shadow the same key | Keep one semantic owner: Timeline emits `previewPlayPauseRequested()`, playback remains host-owned. |
| Timeline `Left/Right` | `TimelineView` | Navigate Timeline viewport; if playback is running, Timeline interaction first stops preview | Quick host may interpret arrows as global preview seek or tab navigation | The Timeline Quick item must capture arrows only while it has active focus and must still emit the interaction-start signal first. |
| Validation/Muri issue-list navigation | current `QListWidget` tabs | Issue lists navigate independently from Timeline | Quick issue-list tabs may compete with Timeline for arrows, Enter, Space, and current-tab navigation | The bottom-tabs host must define per-tab focus ownership and cross-tab key handoff. |
| Tab switch while preview/fullscreen shortcuts are active | current shared `QTabWidget` + widget shell | Tab switching and preview shortcuts coexist today through widget focus scopes | Quick tab switching may alter which surface owns `Space`, `F11`, `Esc`, `Left`, `Right` | Test current-tab, active focus, and shortcut owner transitions explicitly. |
| Preview slider `Left/Right` held seek | `WindowSection::eventFilter()` + held-seek helpers | Slider arrows step preview and start held seek timer | A Quick Timeline or Quick issue tab can accidentally inherit slider-like arrows if host routing is too broad | Keep slider, Timeline, and issue-list scopes distinct and test them together. |
| Editor `Ctrl+LeftClick` | `WindowSection::eventFilter()` | Stops preview if needed, seeks preview to editor-resolved second, updates Timeline cursor marker | Focus changes to Quick bottom-tabs content must not intercept or alter this editor release path | Leave this logic in the window/editor layer. |
| Active preview follow | `syncEditorCursorToPreviewSecond()` and `PlainCodeEditor::applyPreviewFollowCursor()` | Playback follow can move editor selection/caret and Timeline `C` without changing Timeline focus | A Quick Timeline can accidentally bind follow to tab focus or animation state | Follow remains a C++ bridge concern; Quick Timeline only renders `C` and emits follow-toggle changes. |
| Text-focus restore after activation or dialogs | `WindowSection::rememberFocusedTextEditState()` and `restoreFocusedTextEditStateAttempt()` | Text widgets regain focus and selection after activation unless another meaningful widget already claimed focus | A Quick bottom-tabs host can keep focus on the Quick window or tab bar and block editor restoration | Preserve the current focused-widget respect rules and test current-tab plus focus restore ordering. |
| Quick-shell global shortcuts | `QuickShellBootstrap`, `QuickShellController`, `WindowSection::shellHasShortcut()` | Quick shell forwards QAction shortcuts back to the native backend | New Quick tab content can shadow host-level shortcuts | Timeline/Validation/Muri keys stay local; app shortcuts keep using host-level forwarding. |
| Theme change while a Quick tab has focus | `QuickShellStyleBridge`-style host updates | Theme changes already refresh palette/metrics in Quick UI | A theme refresh can accidentally reset current tab, focus target, or caches | Theme revision handling must preserve current tab, focus, playback, follow, and selection state. |
| Native dialog and activation behavior | `main.cpp`, `WindowSection`, preview host docs | Preview path already needs `Qt::AA_DontCreateNativeWidgetSiblings`; dialogs and activation changes are sensitive | A Quick Timeline embedded via a new native child path could recreate the same class of bugs | The target architecture should be same-window Quick host composition, not another child-window embedding path. |

## 11. Recommended Implementation Direction

### 11.1 First principle

Start by separating renderer-agnostic Timeline scene state from the current QWidget renderer. Do not start by writing QML drawing code directly against editor/preview business state.

### 11.2 Recommended first code touches

1. Refactor `TimelineView`-only layout math into new pure helpers and `TimelineSceneState`.
2. Keep `TimelineQuickModel` untouched except for helper exposure needed by the scene-state builder.
3. Make the future `TimelineQuickItem` mirror `TimelineView`'s public setter/signal surface.
4. Introduce the shared `QuickRenderPolicyConfig` / `QuickModuleRenderPolicy` documentation and logging first.
5. Introduce a `BottomTabsQuickHost` skeleton and make current-tab semantics explicit before landing the full renderer swap.
6. Move Validation and Muri tabs as part of the same Quick bottom-tabs host rather than treating them as optional follow-up architecture.
7. Introduce `TimelineThemeConfig` before spreading Timeline colors into multiple Quick layers.

### 11.3 What not to do first

- Do not replace `TimelineQuickModel`.
- Do not port slow refresh or follow logic into QML.
- Do not use `QQuickPaintedItem` as the target architecture.
- Do not start by re-embedding a Quick Timeline into the current widget shell through a new native child window except for throwaway diagnostics.
- Do not leave Validation/Muri as an undocumented architecture gap once bottom-tabs Quick migration starts.
- Do not keep Timeline color policy scattered through multiple Quick layers.

## 12. Staged Migration Path

### Phase 0. Docs + shared policy model + bottom-tabs target freeze

- Freeze this RFC and the parity checklist.
- Introduce the documented shared Quick runtime/backend policy model.
- Freeze the Quick bottom-tabs cluster as the target architecture boundary.
- Add `TimelineThemeConfig` as the documented future owner for Timeline color tokens.

### Phase 1. Quick bottom-tabs host skeleton + tab-state parity

- Build the Quick bottom-tabs host shell.
- Preserve:
  - current tab visibility rules
  - chart-page Timeline default-current behavior
  - validation-driven tab switching
  - Muri-driven cross-region activation behavior
- Keep existing business owners in C++.

### Phase 2. Timeline Quick renderer

- Add `TimelineSceneState`, `TimelineSceneStateBuilder`, `TimelineQuickItem`, and Timeline QSG layers.
- Feed the Timeline Quick item from the same fast/slow business pipeline as the widget Timeline.
- Preserve `L / R / C`, follow, scrub, and zoom semantics.

### Phase 3. Validation/Muri Quick issue tabs

- Move Validation and Muri tab content into Quick issue-list surfaces under the same bottom-tabs host.
- Preserve issue activation, current-tab restore, and cross-region navigation semantics.

### Phase 4. Shared policy / theme / cache stabilization

- Wire shared Preview/Timeline runtime policy diagnostics.
- Finalize cache invalidation and theme revision behavior.
- Validate focus, shortcuts, dialogs, activation, and theme switching across the full Quick bottom-tabs cluster.

## 13. Acceptance Criteria

- The entire bottom-tabs cluster runs in Qt Quick:
  - Timeline
  - Validation
  - Muri
- `TimelineQuickModel` remains the authority for parsing, anchor lookup, preview-follow span resolution, and nearest-note lookup.
- The Quick Timeline preserves the current `L / R / C` meaning and visible marker behavior.
- Editor, Timeline, preview slider, preview playback, Validation, and Muri continue to drive one another through the same semantic bridges that exist today.
- Shared Preview/Timeline render policy is defined and observable through logs.
- Timeline color tokens are owned by one dedicated configuration source.
- Theme switching preserves:
  - current tab
  - active focus
  - playback state
  - follow state
  - selection state
  - visible markers
- CPU-side optimizations that bound parser cost and visible-range work remain intact.
- The host path does not reintroduce the same class of native child-window issues already documented for embedded Quick preview.
