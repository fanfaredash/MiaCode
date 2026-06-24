# Cover Export UI Research

Date: 2026-06-24

## Scope

This document records the UI and technical-direction research for improving MiaCode's cover export workflow.

The requested feature direction is:

- support multiple chart-frame layers;
- improve chart-frame styling, including adjustable background brightness;
- redesign the cover export UI while keeping enough workspace for real composition work;
- prefer reusable open-source components only when they are modular, lightweight, and themeable.

Final user-priority order for external candidates:

1. High modularity: the project should allow selecting only useful components and combining them with MiaCode's chart-frame renderer.
2. Lightweight: avoid new runtime DLLs where possible.
3. Themeable: the UI should be easy to adapt to the current MiaCode theme.

## Current MiaCode Baseline

The existing cover export implementation already has a strong custom rendering core. The main goal should be to improve the workspace and layer-management UX, not replace the renderer.

Primary files:

- `src/tools/cover_export/ExportCoverDialog.{h,cpp}`: current modal cover dialog, control wiring, composition JSON import/export, chart-frame time picker, export entry.
- `src/tools/cover_export/CoverLayoutModel.{h,cpp}`: current layer model. It seeds a `card` layer and one fixed `chartFrame` layer.
- `src/tools/cover_export/CoverComposerView.{h,cpp}`: embeds `CoverComposer.qml` in a bare `QQuickWindow`, applies composer inputs, exports the same scene offscreen.
- `src/tools/cover_export/SceneFrameRenderer.{h,cpp}`: renders a single chart frame at arbitrary chart time through `PreviewQuickSceneRoot`.
- `src/intro/qml/CoverComposer.qml`: QML composition scene. It renders background, difficulty card, chart frame, drag handles, snap guides, and export/static paths.
- `src/app/mainwindow/sections/export/MainWindow.ExportFlow.cpp`: builds the seed `VideoExportTask`, opens `ExportCoverDialog`, and writes exported cover beside the chart.
- `src/tools/export_page/ExportLauncherPage.{h,cpp}`: export hub page. Its cover tab currently launches the cover composer dialog.

Important existing strengths:

- The cover composer already uses normalized layer geometry, so preview and export share one layout model.
- The export path uses the same QML scene as live preview, reducing preview/export drift.
- `SceneFrameRenderer::renderAt(seconds, sidePx)` already supports arbitrary chart-frame capture.
- The existing renderer can stay tightly coupled to MiaCode preview assets, skin loading, Muri overlays, and render settings.

Important existing limits:

- `CoverLayoutModel` currently assumes a single `chartFrame` key.
- `ExportCoverDialog` hard-codes single-frame controls and a single chart-frame checkbox/slider.
- `CoverComposerView` tracks only one live `PreviewQuickSceneRoot`; multiple live chart-frame scenes would require redesign. A safer design is to make only the selected chart-frame layer live and keep other chart frames as cached still images.
- Chart-frame inner background and brightness are global `CoverComposerInputs` fields instead of per-frame layer properties.
- `ExportCoverDialog.cpp` is already large and mixes window layout, model operations, import/export JSON, playback, frame capture, and presentation inputs.

## UI Direction

The cover editor should remain an independent large window/dialog, not an embedded export-page panel.

Rationale:

- Cover composition needs a large canvas and persistent side panels.
- The export hub's embedded area is too small for multi-layer composition.
- A standalone window can later become a separate executable without rewriting the editor body.

Recommended product shape:

```text
MiaCode main window
  -> Open Cover Studio
       -> independent QMainWindow/QDialog workspace
          central canvas: CoverComposerView / CoverComposer.qml
          right panel: layer list + layer inspector
          bottom/top panel: chart-frame time picker and export controls
```

The UI should treat chart frames as first-class layers, not as one toggle.

Suggested workspace:

```text
+---------------------------------------------------------------+
| Cover Studio   [Layout] [Save Layout] [Import]        [Export] |
+-------------+-----------------------------------+-------------+
| Tools       | Canvas                            | Layers      |
| Select      |                                   | eye lock    |
| Add Frame   |  live cover composition           | Card        |
| Add Card    |  drag / scale / snap guides       | Frame 12.34 |
| Align       |                                   | Frame 65.20 |
+-------------+-----------------------------------+-------------+
| Frame Time [Play] ---------------------- 00:12.34 | Inspector  |
|                                                   | brightness |
+---------------------------------------------------+-------------+
```

## External Project Evaluation

### Recommended Only As Reference: QtBitmapEditor

Repository: `https://github.com/0xMartin/QtBitmapEditor`

Relevant qualities:

- C++/Qt Widgets project.
- MIT license.
- Uses CMake.
- Its README describes layer preview, layer order, layer properties, opacity, blend modes, and masks.
- Conceptually close to the required layer-list UX.

Fit:

- Good reference for `LayerWidget`, `LayerManager`, layer thumbnails, visibility, ordering, and per-layer properties.
- Not recommended as a direct dependency because its layer manager is tied to its own project/document/image model.
- Best use is to study and reimplement the patterns on top of MiaCode's `CoverLayoutModel`.

Recommended MiaCode adaptation:

```text
CoverLayerListPanel
  backed by CoverLayoutModel / CoverLayerListModel
  rows show:
    thumbnail, visibility, lock, label, chart-frame time, z/order controls
  operations:
    add chart frame, duplicate, delete, move up/down, bring front/back
```

### Optional Later: Qt Advanced Docking System

Repository: `https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System`

Relevant qualities:

- Mature Qt docking system for IDE-like layouts.
- Can provide professional floating/docking panels.
- CMake-based and supports Qt Widgets style integration.

Fit:

- Useful if native `QDockWidget` is not enough.
- Not necessary for the first implementation.
- Adds dependency and licensing/packaging decisions. The project is commonly used as a separate library; this conflicts with the "avoid new DLLs" preference unless vendored/static-linking is explicitly accepted and license obligations are reviewed.

Recommendation:

- Start with native `QMainWindow + QDockWidget`.
- Reconsider ADS only after the basic cover studio works and there is a concrete need for detachable/floating saved workspace layouts.

### Not Recommended For This Feature: QtPropertyBrowser

Relevant qualities:

- Generic property editor widgets.
- Can represent enum/int/double/color-like properties.

Fit:

- Could reduce custom inspector code, but only after property count becomes large.
- Older Qt Solutions lineage and inconsistent modern Qt6 maintenance across forks.
- Styling generic property-browser rows to match MiaCode may take as much work as a purpose-built inspector.

Recommendation:

- Build a custom `CoverInspectorPanel` using MiaCode-themed Qt Widgets controls.
- Revisit a property-browser dependency only if the inspector grows into a large generic object editor.

### Not Recommended: kImageAnnotator

Repository: `https://github.com/ksnip/kImageAnnotator`

Source-level notes from CMake inspection:

- Builds against Qt Widgets and Svg.
- Supports Qt6 via `BUILD_WITH_QT6`.
- Requires `kColorPicker` as a package dependency.
- Provides a full annotation tool stack, not just layer management.

Fit:

- The secondary `kColorPicker` dependency conflicts with the "lightweight/no new DLL" priority.
- Its annotation model does not map cleanly to MiaCode chart-frame layers.
- It may be useful for future screenshot-annotation features, but not for cover-frame composition.

Recommendation:

- Do not adopt for cover export.

### Not Recommended: KQuickImageEditor

Repository family: KDE `kquickimageeditor`

Fit:

- QML image-editing component set, oriented toward operations such as crop/rotate rather than a custom layer composition workspace.
- KDE dependency chain is heavier than desired for MiaCode's current Qt Widgets + QML architecture.

Recommendation:

- Do not adopt for cover export.

### Not Recommended: Full Image Editors

Examples considered: Krita, PhotoFlare, OpenToonz-style editors.

Reasons:

- Too large for this feature.
- Their document/render models do not understand MiaCode chart frames, preview scene state, or export settings.
- License and dependency scope are high.
- Replacing MiaCode's existing `CoverComposerView` / `SceneFrameRenderer` would create more risk than value.

Recommendation:

- Use them only as UX inspiration, not as code dependencies.

## Recommended Architecture

Do not replace the current rendering stack. Instead, split the current dialog into a reusable studio window and focused panels.

Recommended new files:

```text
src/tools/cover_export/
  CoverStudioWindow.h/cpp        // independent large QMainWindow-style dialog/window
  CoverStudioPanel.h/cpp         // reusable editor body; can later be hosted by a separate executable
  CoverLayerListModel.h/cpp      // QAbstractListModel adapter over CoverLayoutModel
  CoverLayerListPanel.h/cpp      // layer list UI
  CoverInspectorPanel.h/cpp      // per-layer and background/card/frame properties
  CoverFramePickerPanel.h/cpp    // chart-frame time picker and visual playback controls
  CoverCompositionState.h/cpp    // v2 JSON state and v1 migration
```

Existing files to keep and evolve:

```text
CoverLayoutModel.h/cpp
CoverComposerView.h/cpp
SceneFrameRenderer.h/cpp
CoverComposer.qml
```

Suggested widget shell:

```text
CoverStudioWindow : QMainWindow
  centralWidget = CoverComposerView container
  right dock 1   = CoverLayerListPanel
  right dock 2   = CoverInspectorPanel
  bottom dock    = CoverFramePickerPanel
  toolbar        = add frame, duplicate, delete, lock, snap, fit, export
```

This uses only Qt modules already present in MiaCode and keeps full control over `UiTheme`.

## Layer Model Changes

Chart frames should become multiple independent layers. The model should no longer expose a single fixed `chartFrame` helper as the main API.

Suggested `CoverLayer` additions:

```text
opacity: qreal
selected/editable state: handled by UI, not necessarily persisted
frameSeconds: double
frameBgEnabled: bool
frameBgBrightness: qreal
frameStyle: QString or enum
thumbnail/image revision: existing imageRevision can stay
```

Suggested `CoverLayoutModel` additions:

```text
CoverLayer* addChartFrameLayer(double frameSeconds)
CoverLayer* duplicateLayer(const QString& key)
bool removeLayer(const QString& key)
QList<CoverLayer*> chartFrameLayers() const
QList<CoverLayer*> visibleChartFrameLayers() const
void moveLayerBefore/After or normalizeZOrder()
```

Backwards compatibility:

- Existing `card` key remains stable.
- Old v1 composition with `chartFrame` migrates into one v2 chart-frame layer.
- Existing `chartFrame.innerBackground` and `chartFrame.innerBrightness` fields migrate into that layer's per-frame style fields.

## QML Changes

`CoverComposer.qml` should keep one `Repeater` over `coverLayout.layers`.

Recommended change:

- Add root property `activeChartFrameKey`.
- Only the active chart-frame layer uses the live `PreviewQuickSceneRoot`.
- Non-active chart-frame layers show cached stills through `image://coverchart/<key>`.

Reason:

- Current `CoverComposerView` owns one `liveChartScene_`.
- Multiple live chart scenes would complicate lifetime, performance, and frame-state binding.
- A single live selected frame is enough for editing. Export can still refresh all visible frame stills before final composition.

Per-layer chart-frame style should be read from `modelData`, not global composer inputs.

## Export Behavior

Before exporting:

1. Stop visual playback.
2. Iterate visible chart-frame layers.
3. For each frame layer:
   - set renderer playhead to `layer.frameSeconds`;
   - render at the layer's export pixel size;
   - store image through `CoverLayoutModel::setLayerImage(layer.key, image)`.
4. Render `CoverComposer.qml` once at final output size.
5. Save PNG for transparent background, JPG otherwise.

This keeps exported chart frames crisp even when multiple frame layers have different sizes.

## Standalone Executable Option

The UI can later become a separate executable without changing the editor core if `CoverStudioPanel` is independent of `MainWindow`.

Potential target:

```text
miacode_cover_editor.exe
```

Input:

- chart path;
- selected difficulty id or exported task/snapshot JSON;
- cover composition JSON;
- output directory/path.

Recommended approach:

- First build the shared studio panel inside the main app.
- Only after the UI stabilizes, add a CLI/bootstrap path that constructs the same `VideoExportTask` or consumes a snapshot-like JSON.

This keeps the first implementation focused and avoids duplicating export-task construction too early.

## Phased Implementation Plan

### Phase 1: Refactor Current Dialog Without Behavior Changes

- Extract the current editor body from `ExportCoverDialog` into `CoverStudioPanel`.
- Keep modal launch behavior from `ExportSection::onExportCover`.
- Keep single chart frame for this phase.
- No external dependencies.

### Phase 2: Add Multi-Frame Data Model

- Extend `CoverLayoutModel` to support multiple chart-frame layers.
- Add v2 composition JSON with v1 migration.
- Update `CoverComposer.qml` to support `activeChartFrameKey`.
- Update export to refresh all visible chart-frame layers.

### Phase 3: Add Large Studio Window

- Add `CoverStudioWindow` using `QMainWindow + QDockWidget`.
- Add layer list, inspector, and frame picker panels.
- Keep all styling through `UiTheme`.
- Keep existing export-page cover action as "open studio".

### Phase 4: Optional Workspace Enhancements

- If native `QDockWidget` is not enough, evaluate Qt Advanced Docking System.
- Consider layer thumbnails and richer row controls inspired by QtBitmapEditor.
- Consider a separate executable once the shared panel boundary is stable.

## Final Recommendation

Do not integrate a full open-source image editor. Do not replace the current renderer.

Use MiaCode's current cover renderer as the core, build a dedicated independent Cover Studio window, and copy only proven UI patterns from lightweight Qt projects such as QtBitmapEditor. Avoid adding external DLLs in the first implementation. Revisit ADS or property-browser-style dependencies only if concrete UI needs exceed native Qt Widgets.
