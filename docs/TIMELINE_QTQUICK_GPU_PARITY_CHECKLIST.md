Status: Draft implementation checklist

# Timeline Qt Quick + GPU Parity Checklist

Use this checklist before promoting any Timeline Quick/GPU route beyond an experimental flag.

## 1. Semantic Parity

- [ ] `L`, `R`, and `C` keep the same meanings as the widget Timeline.
- [ ] The playback-entry marker still updates on preview start/resume.
- [ ] The playhead marker still tracks preview runtime `R`.
- [ ] The cursor marker still tracks editor/timeline anchor `C`.
- [ ] Normal editor cursor movement updates `C` without forcing preview seek.
- [ ] Editor text edits still hit the fast Timeline refresh path immediately.
- [ ] Slow refresh still owns parser note-marker publication and analysis scheduling.
- [ ] Timeline header click still performs the current time-based navigation path.
- [ ] Timeline `Ctrl/Meta + click` in the body still matches header-click semantics.
- [ ] Timeline drag scrub still edits runtime `R`, not editor `C`.
- [ ] Timeline wheel pan still behaves as scrub/pan, not zoom.
- [ ] Timeline `Alt+wheel` still uses preset zoom steps.
- [ ] Timeline `Space` still toggles preview playback.
- [ ] Timeline `Left/Right` still move the Timeline viewport rather than stepping preview like the slider.
- [ ] Follow-toggle semantics still match the widget path.
- [ ] Paused follow still paints decoration without moving the editor caret.
- [ ] Active follow still binds editor caret/selection to the preview-follow span.

## 2. Cross-Region Linkage Parity

- [ ] Timeline-to-preview seek commands still flow through current `MainWindow` orchestration.
- [ ] Preview playback start/resume still updates Timeline `L`, `R`, and upper-bound state.
- [ ] Preview pause/stop/end still leaves Timeline in the same paused state as the widget path.
- [ ] Preview slider press/move/release still updates preview first and Timeline through preview position.
- [ ] Editor `Ctrl+LeftClick` still stops preview if needed and seeks preview to the editor-resolved second.
- [ ] Editor cursor sync still updates Timeline `C` when preview is paused.
- [ ] Validation and Muri publication still remain outside the Timeline renderer.
- [ ] Paused preview snapshot semantics still match the widget path.
- [ ] Validation explicit run can still switch the shared tab host to the error tab and activate the first issue.
- [ ] Muri item activation can still jump the editor, seek preview, and update Timeline markers.

## 3. Quick Bottom-Tabs Host Parity

- [ ] The bottom-tabs cluster runs in Quick and still hosts Timeline, Validation, and Muri as one coordinated unit.
- [ ] Entering chart mode still forces the Timeline tab current.
- [ ] Switching to metadata or welcome mode still hides the whole bottom-tabs cluster.
- [ ] Validation tab visibility still follows chart-page state correctly.
- [ ] The current tab index still survives issue-list ignore/unignore refresh paths.
- [ ] Validation and Muri rows still relayout correctly after tab switches and size changes.
- [ ] Quick tab selection and Quick focus routing preserve the same visible behavior as the current shared `QTabWidget` path.

## 4. Render Parity

- [ ] Grid line positions match the widget Timeline.
- [ ] Header line labels match line-start anchors, spacing, collapse rules, and marker triangles.
- [ ] Waveform shape matches the currently selected visible-range LOD.
- [ ] Note icon selection matches current flag combinations.
- [ ] Hold and touch-hold visuals match current thickness and cap behavior.
- [ ] Slide/wifi track visuals preserve current track/head separation.
- [ ] Note stacking order matches the widget Timeline and preview-mirrored order.
- [ ] Firework tail spans render in the correct rows and durations.
- [ ] Muri dots appear on the same note locations.
- [ ] Playhead, cursor, and entry markers match current color and placement rules.
- [ ] Zoom presets and centering behavior match the widget Timeline.

## 5. GPU Policy / Backend Selection Verification

- [ ] Preview and Timeline read from the same documented render policy abstraction.
- [ ] The actual GUI graphics backend is recorded in logs.
- [ ] GUI runtime backend reporting is distinct from CLI export/worker backend reporting.
- [ ] Timeline default inline path works under the intended Quick host.
- [ ] Any separate-surface Timeline path is explicitly diagnostic/fallback only and is validated as such.
- [ ] Startup policy logs include graphics API, render loop, native-sibling guard, and per-module surface decisions.

## 6. Theme And Color Verification

- [ ] Timeline color tokens come from one dedicated owner such as `TimelineThemeConfig`.
- [ ] Theme switching preserves the current tab.
- [ ] Theme switching preserves active focus.
- [ ] Theme switching preserves playback, follow, selection, and marker state.
- [ ] Pure color layers update through cheap material/color revision where intended.
- [ ] Theme-baked texture caches rebuild only when required.
- [ ] Validation and Muri Quick tabs update their visuals from the same palette/metrics revision stream as Timeline.

## 7. Focus, Shortcut, And Modality Verification

- [ ] Timeline click gives the Timeline Quick item active focus.
- [ ] Timeline keys are ignored when editor text focus should own input.
- [ ] Preview fullscreen `F11`, `Esc`, and `Space` still work correctly.
- [ ] Preview slider `Left/Right` held seek still works correctly.
- [ ] Timeline `Left/Right` do not accidentally trigger preview slider seek.
- [ ] Quick tab navigation does not steal Timeline-local keys when the Timeline tab content should own them.
- [ ] Validation/Muri issue-list navigation does not break preview or Timeline shortcuts.
- [ ] Editor undo/redo shortcuts still stay editor-owned.
- [ ] Text-focus restore after app reactivation still works.
- [ ] Native dialogs do not leave the Quick bottom-tabs cluster or preview in a broken focus state.
- [ ] Quick-shell host shortcut forwarding still works for QAction-owned app shortcuts.

## 8. Host Architecture Constraints

- [ ] The implementation path under test is explicit about its host mode.
- [ ] The experimental Quick Timeline route does not silently replace the widget Timeline until intended.
- [ ] The target architecture does not depend on a new `QWidget::createWindowContainer()` Timeline embedding path.
- [ ] The Quick bottom-tabs cluster can coexist with the retained-native editor/metadata workspace during migration.
- [ ] Theme, DPI, and font parity were checked in the actual host route under test.

## 9. Exit Criteria

- [ ] Widget Timeline remains available as the semantic reference route until the Quick path is fully accepted.
- [ ] Every item above is green or has an explicit, reviewed exception.
- [ ] No business logic ownership was moved from `MainWindow`/`TimelineQuickModel` into ad-hoc QML state.
