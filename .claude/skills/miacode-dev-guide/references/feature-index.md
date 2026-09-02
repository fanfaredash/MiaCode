# Feature Index

Map a user-facing feature to the files / classes / functions that own it. Paths verified against
`CMakeLists.txt` on 2026-05-29. Code is source of truth — if an anchor moved, fix it here.

## 1. App boot & process modes — `src/app/main.cpp`

- GUI entry: `main`, `setWindowsAppUserModelId`, `wantsQuickShellBeta`,
  `startupOpenTargetFromArguments` (Qt startup, theme/font, window launch, startup-timing log,
  `--quick-shell-beta` routing, file/folder drag-open).
- First-run welcome / initial-config dialog: `wantsWelcomeDialog` (`--welcome` flag) + first-run
  probe `QFile::exists(UiText::preferencesFilePath())` **OR** a schema-outdated probe
  `UiText::storedPreferencesSchema() != UiText::currentPreferencesSchema()` — both captured right
  after `app.setApplicationName` (must be BEFORE the first `UiTheme::applyApplicationTheme` /
  `UiText::isChineseUi` read — that read auto-creates/rewrites `preferences.json` with the current
  schema). Bumping `kPreferencesSchema` (`UiText.cpp`, currently `miacode_preferences_v4`) thus
  re-runs onboarding for existing users; the welcome dialog re-saves prefs under the new schema on
  close, so it fires only once. `storedPreferencesSchema()` reads the RAW on-disk tag (it does NOT
  go through `loadPreferencesObject`/`normalizedPreferencesRoot`, which would inject the current
  token and mask an old file). Result is passed to
  `QuickShellBootstrap::setShowWelcomeDialogOnStartup`, which fires `MainWindow::showWelcomeDialog()`
  from its post-show hook. Dialog body: `sections/preferences/MainWindow.WelcomeDialog.cpp` (see §2).
- CLI export: `wantsCliVideoExport`, `runCliVideoExport`.
- **Export** worker (this is the export subprocess — keep): `wantsCliVideoExportWorker`,
  `runCliVideoExportWorker`.
- The **preview** out-of-process worker (`--preview-worker`, `MIACODE_PREVIEW_OUT_OF_PROCESS`,
  `src/preview/ipc/*`, `PreviewWorkerSession`/`Supervisor`) was **deleted (2026-06-02)** — in-process
  QSG is the keeper; do not reintroduce.

## 2. Main window & orchestration

- Surface + shared state: `src/app/mainwindow/MainWindow.{h,cpp}`, `MainWindowShared.{h,cpp}`.
  Class `MainWindow` owns top-level state, preview runtime instances, the export-worker process,
  portable/project settings. **Orchestration only** — feature bodies live in `sections/`.
- Section map: `src/app/mainwindow/sections/README.md`. Slices live under
  `sections/{frame,document,timeline,validation,editor,preferences,preview,export,window,dialogs}/`.
- Frame/menus/toolbar/layout: `sections/frame/MainWindow.BootstrapAndMenus.cpp`,
  `MainWindow.FrameBootstrap.cpp`, `MainWindow.FrameBootstrapFinalize.cpp`.
- **Sidebar (outline list) + central page stack (2026-06-11, export-page migration phase 1):**
  `outlineList_` is rebuilt by `DocumentSection::rebuildFieldSidebar()` (`MainWindow.DocumentUi.cpp`).
  Item keys (`Qt::UserRole`), in order: `metadata` → `metadataPage_`, `difficulty_chart`
  (+UserRole+1=id) → chart editor, `add` (inline menu), **`export`** → `exportPage_` (the Export
  hub page, §8), `toolbox` (popup menu, no page switch). There is NO `latency` sidebar item
  anymore — the latency page is reached from the metadata page's "延迟与偏移校准" entry card
  (§9); while it shows, the sidebar keeps **metadata** highlighted
  (`activeOutlineKey_=="latency"` selects the metadata item). `editorStack_` pages:
  `welcomePage_` / `metadataPage_` / `latencyDetectionPage_` / `exportPage_` / `chartPage_`.
  Page-switch functions (`switchToMetadataField` / `switchToWelcomePage` /
  `switchToDifficultyField` / `switchToLatencyField` / `switchToExportField`, all in
  `MainWindow.DocumentUi.cpp`) share one skeleton — **every leave path calls
  `latencyDetectionPage_->onPageLeft()` unconditionally** (SFX-leak regression guard, §9).
  Bottom-tab mode table: editor ON / latency ON / metadata OFF / welcome OFF / **export OFF**.
  `switchToExportField` is the one exception to the shared skeleton: the export page is slow to
  build (embedded video panel), so it shows `outlineBusySpinner_` (`miacode::ui::BusySpinner`,
  `src/app/ui/BusySpinner.{h,cpp}`) floated over the `export` sidebar row and defers the heavy
  body (`performSwitchToExportField`) by one event-loop tick via `QTimer::singleShot(0, …)`. The
  spinner is a mouse-transparent viewport overlay positioned with the same `visualItemRect`
  pattern as `deleteDifficultyButton_`; spinner `isActive()` guards re-entrancy. Stopped in the
  same deferred lambda once the build returns.
- QuickShell beta: `src/app/quick_shell/` (`QuickShellBootstrap`, `QuickShellController`,
  `QuickShellNativeSurfaceHost`, `QuickShellPreviewCompositeSurface`, `QuickShellStyleBridge`,
  `qml/QuickShellMain.qml`).
- Appearance prefs + first-run onboarding: theme pref persisted via
  `UiText::preferredTheme`/`setPreferredTheme` (`preferences.json` `ui.theme`); live re-theme via
  `MainWindow::WindowSection::applyUiTheme` (triggers `ApplicationPaletteChange` → `QuickShellStyleBridge`
  → QML chrome).
  **Live re-theme contract for tools-layer pages/panels:** Qt does NOT regenerate a widget's
  literal `setStyleSheet(...)` string or a baked `QIcon` on a palette change, so any persistent
  surface that bakes theme colors at construction needs an explicit re-apply hook that `applyUiTheme`
  calls. Today: `LatencyDetectionPage::applyThemeStyles()` (page sheet + the accent-tinted media-tools
  gear icon) and `ExportLauncherPage::applyThemeStyles()` (page sheet) are called from `applyUiTheme`,
  and `applyUiTheme` ALSO forwards to the export hub's live embedded `VideoExportDialog`
  (`owner_.embeddedVideoExportPanel_->applyThemeStyles()`) which re-applies its own sheet + every
  per-widget sheet (menu/push buttons, sliders, read-only edit, tab strip, footer rule) + the
  transport icons. When you add a baked `setStyleSheet`/`setIcon(make*Icon(color))` to one of these,
  mirror it in that class's `applyThemeStyles()` or it stays frozen at the startup theme. Preview-pane side = `workspacePanelsSwapped_` (`preview.swap_side_panels`), live via
  `MainWindow::setWorkspacePanelsSwapped` (QML reads `controller.workspacePanelsSwapped`, default =
  preview on right; swapped → preview on left). Both the theme row and a "谱面预览位置" (preview
  side) row live on the Preferences dialog's 外观 page
  (`MainWindow::PreferencesSection::onPreferences`, `MainWindow.PreferencesDialog.cpp`) AND in the
  first-run welcome dialog (`PreferencesSection::showWelcomeDialog`, `MainWindow.WelcomeDialog.cpp`)
  — both drive the same setters; the welcome dialog keeps its self-contained `WelcomeLayoutPreview`
  schematic. The welcome dialog also exposes a 中文输入法 radio group (关闭输入法 default / 开启输入法 /
  转换全角字符) wired to `applyEditorHalfWidthInputEnabled` + `applyEditorImeInputDisabled` — the same
  two prefs as the Preferences 中文输入 combo (2026-06-19). A round "?" help badge
  (`QLabel#WelcomeHelpBadge`, styled in `preferencesDialogStyleSheet` so it re-themes, +
  `miacodeAllowTooltip` to bypass the global tooltip suppression) sits beside the 中文输入法 title.
  zh strings under `dialog.welcome.*` / `dialog.preferences.preview_side*` in `UiText.cpp`.
  Toolbar settings/Preferences icon = `makeSettingsGearIcon` (`MainWindowShared.cpp`): the Google
  Material "settings" gear rendered via `QSvgRenderer` (**Qt6::Svg**). The gear is font-matched by
  rendering the artwork into an *inset* of the icon box (so the glyph reads ~menu-text size) — do
  NOT shrink `toolBar->setIconSize(...)` to size it: the gear is the toolbar's only icon, so its
  icon box drives the toolbar row height and a smaller iconSize visibly shortens the toolbar.
- Slider value labels are click-to-edit. A `QSlider` + percent/value `QLabel` pair built through
  the shared row helpers — `addAudioRow` / `addVideoSliderRow` (`MainWindow.Dialogs.cpp`),
  `addPercentSliderOption` (`VideoExportDialog.cpp`), `createSliderOption`
  (`BatchVideoExportDialog.cpp`), and the inline SFX-volume row (`LatencyDetectionPage.cpp`) —
  uses `miacode::ui::EditableValueLabel` (`src/app/ui/EditableValueLabel.{h,cpp}`) in place of a
  bare `QLabel`. Clicking the number opens an inline `QLineEdit` over the label's own `rect()`
  (zero layout change — it is still a `QLabel` at rest) and commits via `slider->setValue()`,
  so the typed edit re-fires every existing `valueChanged` connection (settings setter, label
  refresh, runtime apply) — identical to dragging. New value slider → swap `new QLabel` for
  `EditableValueLabel` and call `bindSlider(slider)`; nothing else changes. Deliberately NOT wired:
  time scrubbers (`previewSlider_`, cover `frameSlider_`, `QuickShellPreviewTransport.qml`) and the
  QML `TimelineTabSurface.qml` brightness slider (would need a QML-side editor).
- Native title-bar theming (Windows DWM): single owner `UiNativeWindowTheme`
  (`src/app/ui/UiNativeWindowTheme.{h,cpp}`) — dark-mode flag + caption/text/border colors +
  system backdrop per top-level window. Applied three ways, all idempotent: (1) an app-wide
  auto-apply event filter installed in `main.cpp` (GUI path only) themes every eligible QWidget
  top-level on Show/ActivationChange/palette change — popups/tooltips/frameless are excluded by
  `isEligibleWidget`; (2) `UiDialogs::prepareDialogWindow` applies it when preparing a dialog;
  (3) `MainWindow::WindowSection::applySystemWindowBackdrop` forwards to it (legacy call sites)
  and its no-target form sweeps ALL visible top-levels on theme switch. QML root windows are
  themed by `QuickShellBootstrap` via `UiNativeWindowTheme::applyToWindow` (no frame-refresh
  tail, unlike the widget path). Do NOT re-add per-file DWM copies — tools-layer dialogs
  (e.g. `ExportCoverDialog`) include `UiNativeWindowTheme.h` directly when they need an
  explicit call.

## 3. Document model & file flow

- Storage: `src/core/chart/document/SimaiDocument.{h,cpp}` (`createEmpty`, `fromText`, `toText`,
  `parseRawFields`, `serializeRawFields`, `ensureDifficulty`, `removeDifficulty`).
- Timing metadata: `src/core/chart/document/SimaiTimingMetadata.{h,cpp}` (`buildTimingMetadata`,
  `buildTimingMetadataFromRawText`, `parseInlineTimeSignatureComment`).
- Open/save/new/switch + autosave: `sections/document/MainWindow.DocumentFlow.cpp`
  (`onNewFile`, `onOpenFile`, `openStartupTarget`, `onSaveFile`, `runAutosaveCheck`,
  `loadDocument`, `rebuildFieldSidebar`, `populateMetadataPage`, `populateDifficultyPage`).
- Crash recovery + abnormal-exit autosave prompt: `src/common/CrashRecovery.{h,cpp}`
  (crash-handler snapshot → `<chart>.crash_recovery`; **per-instance session marker**
  `<AppConfigLocation>/sessions/session-<pid>.marker` — records `pid` + process `created`
  time + `chart` path — written via `TimelineSection::setCurrentFilePath`, cleared on both
  clean-close paths — legacy `WindowRuntime closeEvent` and quick-shell `WindowShell
  confirmShellClose` — GUI-only via `setSessionMarkerEnabled(true)` in `main.cpp`, so CLI
  export/worker runs never touch it. **Multi-instance safe:** markers are PID-keyed and a
  marker counts as abandoned only when its owning process is no longer alive — `processIsLive`
  via `OpenProcess`+`WaitForSingleObject`, with a creation-time match to reject recycled PIDs;
  a second concurrent window no longer mistakes the first's live marker for a crash, and each
  process only writes/deletes its own file). Chart open still calls
  `prepareForChart`, then `applyOpenedDocumentState` only detects an abandoned marker or
  existing crash-recovery file and records `pendingAbnormalExitBackupRestorePath_` as the same
  newest entry shown by File → Restore Backup (`backupRestoreEntriesForAutosaveDirectory`:
  crash file, latest `.bak`, history). After the chart/window finishes loading, a queued
  `runPendingAbnormalExitBackupRestore` calls `restoreBackupFilePath(path, true)`, so automatic
  crash recovery has the **same behavior** as the menu action (restore replaces editor text,
  original file is untouched, autosave reference remains the old file) with only one extra
  prompt line saying MiaCode did not exit normally last time. `cleanupCrashRecoveryForCleanExit`
  preserves the pending crash file until that deferred restore has had a chance to read it.
- Editor header/page-mode UI: `sections/document/MainWindow.DocumentUi.cpp`.
- Chart text editor: `src/editor/PlainCodeEditor.{h,cpp}` (line numbers, transform context menu,
  half-width normalization, `normalizedViewportHitPosition`, bracket auto-close
  `tryAutoCloseBracket`, closing-bracket type-over `tryOverwriteClosingBracket` (typing `)]}` when
  the same glyph already sits to the caret's right steps over it instead of inserting a duplicate),
  empty-pair backspace `tryDeleteBracketPair` (deletes both glyphs of
  `[|]` in one undo step). All gated by the single auto-completion pref; hold shortcut
  `tryHoldExpand` (typing `h` inserts a bare `h` and pops the `[8:1]`-style suggestions — it
  inserts NO bracket itself, so a following `[` yields `h[]`, never the old `h[[]]`).
- Drag-selection autoscroll on adopted surfaces: `src/common/AdoptedSurfaceDragAutoScroll.{h,cpp}`
  (`miacode::ui::installAdoptedSurfaceDragAutoScroll(QAbstractScrollArea*)`, **macOS-only body**,
  no-op elsewhere; geometry helper `planDragAutoScrollStep` compiled everywhere and specced in
  `plain_code_editor_spec`). Reason: Qt's own autoscroll re-derives the held pointer from
  `QCursor::pos()` through `QWidget::mapFromGlobal()`, which on the adopted QuickShell surface
  still resolves through the neutralized orphan NSPanel (see `common/AdoptedWidgetCoordinates.h`),
  so its synthesized move lands on the wrong line and fights the real drag — the selection strobes
  whenever the pointer leaves the viewport (the gutter and top overlay inset are viewport margins,
  so dragging left/up is enough). The installed viewport event filter swallows out-of-viewport
  moves, re-sends them clamped (which is also what keeps Qt's timer from arming), and steps the
  scrollbars from a timer fed by real event coordinates. **Install it on every scroll area that
  supports drag-selection on the workspace/sidebar surfaces** — today `PlainCodeEditor`'s ctor
  (covers the chart editor + copy area) and `metadataExtraEdit_` (plain `QTextEdit`,
  `MainWindow.FrameBootstrap.cpp`).
- Bracket-completion dropdown ("tab 补全"): typing `( [ {` pops a simai-aware suggestion list under
  the caret; typing `h` pops the full-bracket hold durations (`[8:1]` …). Candidate
  data + scans: `src/editor/SimaiCompletionCatalog.{h,cpp}` (pure — `candidatesForOpening` for the
  brackets, `holdDurationCandidates` for `h`; spec at
  `src/tools/editor/SimaiCompletionCatalogSpec.cpp`); the non-focusing popup widget:
  `src/editor/BracketCompletionPopup.{h,cpp}`; editor glue (`tryBracketInput`, `tryHoldExpand`,
  shared opener `openCompletionPopup` behind `maybeOpenBracketCompletion` / `maybeOpenHoldCompletion`,
  `handleCompletionPopupKey`, `acceptCompletionCandidate`, filter via `cursorPositionChanged`) in
  `PlainCodeEditor.cpp`. `(` BPM list needs the `&wholebpm` value pushed via `setWholeBpmCandidate`
  from `DocumentSection::setEditorText`. **One unified preference** `editor_auto_completion`
  (default on) drives auto-close, the suggestion popup, AND the `h` hold suggestions together —
  formerly three separate toggles (`editor_auto_close_brackets` / `editor_auto_insert_square_after_h`
  / `editor_bracket_completion`), now migrated in `EditorSection::loadPortableState` by falling back
  to the legacy auto-close key. Single setter `PlainCodeEditor::setAutoCompletionEnabled`, apply
  `applyEditorAutoCompletionEnabled`, one checkbox ("自动补全") in `MainWindow.PreferencesDialog.cpp`.
  Keys: ↑↓ navigate, Tab/Enter accept (Enter swallows the newline), Esc/keep-typing dismiss.
- **Offset (`&first`) field location:** the chart-wide timing offset is edited from the
  **difficulty-page header** (`firstEdit_`, label `difficultyFirstLabel_`, built in
  `MainWindow.FrameBootstrap.cpp` next to `&lv_N`), NOT the metadata page (the metadata `first`
  row was removed). One source of truth = `document_.first`, shared with the latency page. While a
  difficulty is active, `TimelineSection::parsedRawFirstSeconds` reads the live field text (so an
  uncommitted edit reflows the timeline; `editingFinished` triggers `refreshTimelineMetadata`);
  it commits to `document_.first` in `applyCurrentFieldToDocument`'s difficulty branch (sets
  `metadataTimingChanged`).
- **Header 顶部显示 preference (offset vs designer):** the header field pair next to Lv is
  switchable — `MainWindow::EditorHeaderTopDisplay` (`Offset` default / `Designer`), state
  `editorHeaderTopDisplay_`, persisted as `ui.editor_header_top_display` (`"offset"`/`"designer"`),
  preference row at the top of Preferences → 编辑器. In Designer mode the header shows
  `difficultyDesignerLabel_` + `difficultyDesignerEdit_` (`&des_N` of the active difficulty,
  re-added in `MainWindow.FrameBootstrap.cpp`); visibility is applied by
  `updateEditorHeaderLayoutMode`, apply chain `MainWindow::applyEditorHeaderTopDisplay` →
  `EditorSection::applyEditorHeaderTopDisplay`. The hidden pair's edit is still populated
  (`populateDifficultyPage`) and recaptured on commit/autosave, so reads are unconditional; any
  code that rewrites designers behind the header's back must call
  `DocumentSection::syncHeaderDesignerEditFromModel` (designer dialog commit, unified reconcile,
  unified broadcast, preference flip all do).
- **Per-difficulty designers + unified designer:** managed from a modal dialog, NOT a persistent
  checkbox. The metadata designer row has a "管理多个难度名义" button →
  `MainWindow::onManagePerDifficultyDesigners` → `DocumentSection::openPerDifficultyDesignerDialog`
  (`MainWindow.DocumentFlow.cpp`): seven rows for `&des_1..7` plus the "所有难度采用相同名义"
  toggle, committed on OK. When the toggle is turned ON, ≤1 distinct non-empty name unifies
  silently; otherwise `promptCanonicalDesignerName` lets the user pick the canonical name or
  "clear all". Per-project pref key `unified_designer_enabled`;
  `SimaiDocument::inferUnifiedDesignerDefault()` is **always false** (never auto-enable).
  - **Chart-less `&des_N` (standalone designers):** a slot 1..7 can hold a designer name without a
    chart. `SimaiDocument` keeps these in `standaloneDesigners_` (disjoint from `difficulties_`),
    API `designerForSlot` / `setDesignerForSlot` / `standaloneDesignerIds` / `perDifficultyDesigners`.
    `fromText` post-pass moves a designer-only difficulty (empty level+chart, non-empty designer)
    into the standalone map; `toText` emits a bare `&des_N=` for it (no phantom `&lv_N`/`&inote_N`,
    no sidebar entry). The dialog writes chart-less names via `setDesignerForSlot`.
  **Unified sync invariant — every designer-touching path must preserve it** (sync set): edit-time
  broadcast `applyCurrentFieldToDocument` (both the metadata top-`&des` branch AND the difficulty
  branch's header `difficultyDesignerEdit_` — see the 顶部显示 preference above); apply
  `applyUnifiedDesignerName`; load reconcile
  `refreshUnifiedDesignerStateForLoadedDocument`; new-difficulty seed
  (`MainWindow.FrameBootstrap.cpp`); undo-delete restore re-seed
  (`MainWindow.DocumentEditorState.cpp`); autosave snapshot mirror (`currentDocumentTextForAutosave`,
  which captures the live header designer text and treats it as canonical under unified mode).
  All broadcast/apply sites iterate `perDifficultyDesigners()` + `setDesignerForSlot` so standalone
  `&des_N` participate too. Cross-page UI sync = mirror `designerEdit_` (metadata) and call
  `syncHeaderDesignerEditFromModel` (header) after model-side rewrites — the broadcast block,
  dialog commit, and `applyUnifiedDesignerName` all do both. The free-form "Other &xx Fields"
  editor must parse via
  `SimaiDocument::parseUnmanagedFields` (not `parseRawFields`) so a manually typed managed key
  (`des`/`des_N`/`lv_N`/`inote_N`/title/artist/first/video) can't bypass the model and emit a
  duplicate/divergent line.
  **Export-side fallback contract (sync set):** the exported "谱师名义" (intro banner designer +
  chart-info-HUD `chartDesigner`) uses per-difficulty `&des_N`, falling back to top `&des` when
  the per-difficulty name is **blank including whitespace** — gate on `designer.trimmed().isEmpty()`,
  never bare `.isEmpty()` (a `&des_N= ` value parses non-empty and must still fall back). All five
  sites must stay aligned: intro `MainWindow.ExportSnapshot.cpp` (`intro.designer`), single-export
  task `MainWindow.ExportSnapshot.cpp` + `MainWindow.ExportFlow.cpp` (live-preview `setChartInfo`
  path) + batch `MainWindow.ExportFlow.cpp`, and the snapshot rebuild in `VideoExportSnapshot.cpp`
  (`buildVideoExportTaskFromSnapshot`).

## 3b. Editor bookmarks (2026-07-06 redesign)

- Spec: `docs/specs/editor/BOOKMARK_REDESIGN_SPEC.md` (includes the implemented detailed design).
- **Storage — simai file is authoritative:** one managed single-line field
  `&miacode_bookmarks={"schema":"miacode_bookmarks_v2","items":[{"d","l","n","s","src","fp","cb","ca","locked"}]}`.
  Model: `SimaiBookmarkData` + `SimaiDocument::bookmarks` / `bookmarksParseError`
  (`src/core/chart/document/SimaiDocument.{h,cpp}`). `fromText` routes the key into `bookmarks`
  (never `extraFields`; bad JSON → ignored + flag, never blocks loading); `toText` emits it after
  extra fields, before the difficulty triples, and omits it when empty; `parseUnmanagedFields`
  treats it as reserved so it never shows in the metadata "Other &xx Fields" editor. Spec cases in
  `simai_document_spec`.
- **Legacy fallback / migration:** `.miacode/miacode_settings.json` `editor_bookmarks` is read into
  `state_.legacyJsonEditorBookmarks_` by `EditorSection::loadProjectRenderState` and consumed by
  `adoptBookmarksForLoadedDocument()` (called from `DocumentSection::loadDocument`) only when the
  simai payload is absent. `state_.editorBookmarksInSimai_` gates the legacy JSON mirror in
  `saveProjectRenderState` (kept for crash safety until the first simai save, dropped after).
  `saveToPath` pushes `editorBookmarks_` into the document via `syncBookmarksIntoDocument` before
  `toText()` and flips the flag.
- **In-memory model:** `MainWindow::EditorBookmark` (user-visible face = `title` + `line`;
  `nameLocked` set on explicit rename; `text` is a legacy import-only field). User-initiated
  mutations call `EditorSection::markBookmarksMutatedByUser()` (marks the document dirty so the
  change reaches the file on save); the comment auto-sync (`syncBookmarksFromEditorText`) never
  dirties and NEVER renames — default names are generated exactly once at creation
  (`defaultBookmarkNameFromComment`: first token of the `||` comment, else `fallbackBookmarkNameForLine`
  "第 N 行"/"LN").
- **Sidebar (IDE-style tree, `outlineList_`):** built by `DocumentSection::rebuildFieldSidebar`
  (`MainWindow.DocumentUi.cpp`); painted by `OutlineItemDelegate` in `MainWindowShared.h` from the
  shared `kOutlineItem*Role` constants. Difficulty rows carry the fold chevron at the ROW START
  (path chevron, `kDifficultyFoldHitZone` click zone in FrameBootstrap; per-difficulty state in
  `outlineBookmarkGroupExpanded_`, untouched groups default expanded only for the active
  difficulty); `bookmark` rows (item text = bare name) draw a 1px indent guide + a fixed-width
  neutral line badge (`kOutlineItemMaxLineRole` sizes it group-wide; solid accent = last-activated).
  `kOutlineItemActiveRole` on metadata/export/difficulty rows is the persistent "you are here"
  marker (borderless fill + 3px left accent bar), driven by `activeOutlineKey_`/`activeDifficultyId_`
  — NOT the list selection, so it survives bookmark clicks. Non-interactive `spacer` kind rows
  separate the sidebar sections. Rebuild preserves fold state, bookmark selection and scroll
  position. Single click = jump to
  line (+ accent marker `activeBookmark*`), double click = inline rename (`editItem`; commit via
  `itemChanged` → `EditorSection::renameBookmark`, empty name reverts), right click = 重命名 /
  删除 / 跳到时间轴位置 (difficulty & group rows add 插入书签). Reveal/rename entry:
  `DocumentSection::revealBookmarkInSidebar`.
- **Editor entry points (`PlainCodeEditor`):** gutter double-click activates/creates; gutter drag
  moves; body & gutter right-click add 插入/重命名/删除/在侧边栏显示 via intent signals only
  (`lineNumberBookmarkCreateRequested/RenameRequested/DeleteRequested/Activated/ContextMenuRequested`)
  — MainWindow (`FrameBootstrap`) owns the actions. Dialog-free: the old create/detail/manager
  dialogs and the toolbox 创建书签/书签管理 entries were REMOVED (toolbox keeps JSON
  import/export as compatibility tools; import marks the document dirty).

## 4. Parser, validation, markers

- API: `src/core/chart/parser/SimaiNativeParser.h` + `SimaiNativeParser.Driver.cpp`
  (`parseForTimeline`, `validateSyntax`, `buildValidationReport`).
- Internals (include-split into `SimaiNativeParser.cpp`): `.Slide.cpp`, `.TouchTap.cpp`,
  `.StrictChecks.cpp` (see `SimaiNativeParser.cpp:1584`).
- Validation UI: `sections/validation/MainWindow.ValidationFlow.cpp` (`runValidateSimai*`,
  `addValidationError`, `addValidationDecoration`).
- Note-modifier sync set (one patch touches all): native parser (`.cpp`/`.TouchTap`/`.Slide`) →
  marker flags (`src/timeline/TimelineData.h`) → mirror (`TimelineQuickModel.cpp` +
  `TimelineRenderData.h` flags) → transform (`ChartBatchTransform.cpp`, must NOT `return false` on
  unknowns) + normalization round-trip (`ChartNormalization.cpp`) → skin selectors + timeline icons
  → specs (`SimaiParserSpec`, `ChartBatchTransformSpec`, `TimelineModelSpec`) + diagnostics docs.
- **Mine notes** (`m` suffix): `isMine`/`trackMine`/`headMine`; mine OVERRIDES break/each (one
  `<base>_mine.png` per type, skinSTD only); suppressed in SFX (`PreviewSfxTimeline.buildTimeline`) +
  Muri (`MuriAnalyzer`, `MuriRuntimeModelBuilder`); counted in stats. Docs:
  `docs/MINE_NOTE_RESEARCH_AND_MIACODE_PORT_HANDOFF_ZH.md` §7.
- **HS (`<HS*N>`)**: per-note `hsMultiplier` (frozen at emit, Q5), parsed in `Driver.cpp` (Q1/Q2/Q7);
  applied in scene via `previewTapTimingForEffectiveFlowSpeed` (`PreviewOpacityCurves`). **Negative HS**
  is ON by default (`SimaiNativeParser::g_allowNegativeHs` defaults true; opt-out
  `MIACODE_PREVIEW_REJECT_NEGATIVE_HS` at boot); reverse flow via `PreviewTapTiming.directionSign` + `sampleTapApproach`
  reverse branch; per-type sign policy (tap/star/line signed, hold/touch/slide abs). Docs:
  `docs/ops/DEBUG_INDEX.md` (`MIACODE_PREVIEW_REJECT_NEGATIVE_HS`).

## 5. Timeline, cursor mapping, preview sync

- Quick model: `src/timeline/TimelineQuickModel.{h,cpp}` (lightweight parse, cursor anchors,
  preview-follow buckets, incremental edit apply; owns comma-only `C` anchor lookup).
- Widget timeline: `src/timeline/TimelineView.{h,cpp}` + `.Core/.Interaction/.Paint.cpp`
  (include-split). Visible-range paint, playhead/cursor, waveform, follow-preview.
- Scene-state + Quick surface: `src/timeline/TimelineSceneState*`, `TimelineSceneStateBuilder.*`,
  `TimelineNoteAssets.*`, `src/timeline/quick/TimelineQuick*Layer.*`, `TimelineQuickItem.*`,
  `TimelineQuickStateBridge.*`, `src/common/TimelineThemeConfig.h`.
- Refresh orchestration: `sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  (`applyTimelineQuickChange`, `refreshTimelineQuickModelFromCurrentText`, `scheduleTimelineRefresh`,
  `requestTimelineSlowRefresh`, `scheduleTimelineAnalysisRefresh`, `seekTimelineToCursor`).
- Slow refresh workers: `src/timeline/TimelineSlowRefresh.{h,cpp}`.
- Timing getters: same `PreviewTimelineFlow.cpp` (`currentTimingMetadata`, `parsedFirstSeconds`,
  `parsedWholeBpm`, `parsedLatencyMeterId`, `applyLatencyDetectorOffset`).
- **Preview-follow while PAUSED updates the DECORATION ONLY** — `syncEditorCursorToPreviewSecond`
  (`MainWindow.TimelinePreviewFollowSync.cpp:217`) returns after `setPreviewFollowDecoration`; the
  real `QTextCursor` is moved (`applyPreviewFollowCursor`) only while playing. An earlier attempt to
  move it while paused was reverted because it clobbered drag selections (see the note at
  `MainWindow.WindowInteraction.cpp:1450`). So the text caret and the playhead legitimately diverge
  after any paused seek, and that divergence is by design — the decoration is a read-only indicator
  of where the playhead is. Nothing authors against it: touch-pad click input targets the caret
  (§5b). The caret does drag the preview the other way, though — `cursorPositionChanged`
  (`MainWindow.FrameBootstrap.cpp:1794`) syncs the timeline/preview to the caret while paused when
  `timelineSyncEnabled_`, which is why the two normally agree.

### 5b. Touch-pad click authoring (Ctrl/Cmd + click the preview)

- Setting `preview.touch_pad_authoring_shortcut` (启用touch点击输入); Ctrl-hold gate in
  `MainWindow.WindowInteraction.cpp:958` → `setTouchPadAuthoringCtrlHoldActive` →
  `PreviewRuntime::setTouchPadAuthoringEnabled`.
- Hit test + press/release gesture: `PreviewQuickSceneRoot::mousePressEvent/mouseReleaseEvent`
  (`touchPadAtItemPoint` → `touchPadTokenAtLogicalPoint`), state machine in
  `core/scene/TouchPadAuthoringState.h`, signal `PreviewRuntime::touchPadAuthoringClicked`.
- Click handler: `MainWindow.FrameBootstrap.cpp:1326`. **Target token = the text caret, always** —
  resolving a playhead second onto a token is not predictable for a user, so the insertion point is
  the one they set by hand (decided 2026-08-18; an earlier revision targeted the preview-follow
  highlight). Text edit planned by `planTouchPadAuthoringEdit`
  (`src/editor/TouchPadAuthoringEdit.cpp`); undo entry recorded with **pre-edit int offsets**
  (`recordChartCursorUndoEntry` — a live `QTextCursor` would be shifted by the edit itself);
  preview then seeks to `tokenSecond - 1/60` (a deliberate convention, not a bug).
- That seek parks the playhead one token EARLY, so `touchPadAuthoringAnchor*`
  (`MainWindow.TimelinePreviewFollowSync.cpp:43`, cleared in `setEditorText`) maps it back for the
  highlight. Purely cosmetic — it does not feed the insertion point.
- Token boundaries come from `src/core/chart/parser/SimaiCommentScan.*` so `||` comments are
  skipped exactly as the two parsers skip them — see `cross-chain-linkage.md` §15.
- Spec: `plain_code_editor_spec` (`src/tools/editor/PlainCodeEditorSpec.cpp`).

## 6. Preview video, media, render state

- Runtime host + export session: `src/preview/runtime/PreviewRuntime.{h,cpp}`,
  `PreviewSceneAssetLoader.*`, `PreviewSceneAssetRepository.*`, `PreviewQuickRuntimeSurface.*`,
  `PreviewQuickExportSession.*`, `qml/PreviewRuntimeView.qml`. `PreviewRuntime` is the on-screen
  preview host (`QQuickView`) and the export session renders `PreviewQuickSceneRoot` headlessly.
- Stage-media host (background image+video, both shells): `src/preview/runtime/PreviewStageMediaHost.{h,cpp}`,
  `qml/PreviewStageMediaItem.qml`. **`PreviewMediaController` / `src/preview/video/` was removed**
  — background media now flows through `PreviewStageMediaHost` plus `PreviewRuntime`'s
  stage-background setters (`setStageMediaAvailable`, `setStageMediaPresentationMode`,
  `setExternalStageMedia*`). Verify exact widget-shell vs quickshell routing in
  `sections/preview/MainWindow.PreviewStageMediaRoute.cpp`.
  - **Video decode backend:** on **Windows** the host decodes PV/BG via **QtAVPlayer (FFmpeg)**,
    not `QMediaPlayer` — guarded by the `MIACODE_USE_QTAVPLAYER` build macro (CMake-defined on
    Windows; other platforms keep the `QMediaPlayer` path under `#elif defined(HAVE_QT_MULTIMEDIA)`).
    QtAVPlayer source is vendored in `third_party/QtAVPlayer/`; it links the FFmpeg dev SDK under
    `third_party/ffmpeg/windows/dev/`. Decoded frames are *pushed* into the QML `VideoOutput`'s
    `QVideoSink` (`handleDecodedVideoFrame`) rather than observed; `setSpeed` (not `setPlaybackRate`)
    + `seek` drive playback, and the paused-seek / prepared-start acks settle on frame `pts`.
    The public method/signal contract is unchanged, so `MainWindow.*` callers don't change.
    See `docs/ops/DEBUG_INDEX.md` for the public backend summary.
- Backend-neutral scene state (NO GPU deps): `src/core/scene/` — `PreviewFrameState.h`,
  `PreviewLayerOrder.h`, `PreviewOpacityCurves.*`, `PreviewSceneGeometry.*`, `PreviewHudState.*`,
  `PreviewProgressStatsCache.*`, `PreviewPreparedSceneCache.*`, `PreviewMarkerDrawOrder.*`,
  `Preview*LayerState.*`, `PreviewSkinSelectors.*`, `PreviewAnimatedSpriteHelpers.*`.
- Active QSG layers: `src/preview/quick_scene/` — `PreviewQuickSceneRoot.*`, `PreviewQuick*Layer.*`,
  `PreviewQuick{Sprite,Circle,Arc,Sector}Nodes.*`, `PreviewTextureRepository.*`,
  `shaders/PreviewSpriteMaterial.*` / `PreviewFireworkMaterial.*` / `PreviewStageDimMaterial.*`.
  - Center-display HUD (combo/达成率/DX分, `CenterDisplayMode`, default Off): `updateCenterDisplaySlot`
    in `PreviewQuickSceneRoot.cpp` re-rasterizes a full-`renderSize` QImage → ad-hoc
    `createTextureFromImage` per judged-note stats change. **Texture-ownership contract (beta8 leak
    fix, 2026-06-10):** that slot's `QSGSimpleTextureNode` MUST keep `setOwnsTexture(true)`, and its
    teardown paths do a plain `delete node` with NO manual `texture()` delete. Without OwnsTexture,
    `setTexture()` replacement leaked one ~4.6 MB full-viewport texture per judged note — the 0.5.0
    "掉帧→必须重启/闪退" root cause (`docs/PREVIEW_FRAMEDROP_DIAGNOSIS_AND_FIX_SPEC_ZH.md` §8). Any
    new ad-hoc `createTextureFromImage` + texture-node site must declare ownership explicitly
    (repository-cached textures stay `setOwnsTexture(false)` — the repository deletes them).

## 7. Preview audio & SFX scheduling — `src/audio/`

- Facade: `QtPreviewSfxRuntime.{h,cpp}` (+ include-split `.Assets/.Timeline/.Background/.Engine/.Voices.cpp`)
  — backend selection, prepare/commit/pause/resume/seek surface.
- Backends behind `src/audio/PreviewAudioBackend.h`: `BassPreviewAudioBackend.{h,cpp}` (Windows/macOS/Linux,
  real BASS, master mixer clock, preloaded SFX channels, BASS_FX tempo) and
  `MiniaudioPreviewAudioBackend.{h,cpp}` (no-BASS compatibility, SoundTouch stretch).
- Settings/semantics: `src/audio/PreviewAudioSettings.*`, `src/common/PreviewSfxAssets.h`,
  `PreviewSfxSemantics.h`, `PreviewSfxTimeline.h`, `PreviewSfxTiming.h`.
- **Output-device change → auto-pause** (BASS platforms only): `PreviewAudioDeviceWatcher.{h,cpp}`
  (owns the `QMediaDevices` observer + snapshot) + `PreviewAudioDeviceChangePolicy.h` (pure decision,
  CTest `preview_audio_device_change_policy_spec`) → `TimelineSection::pausePreviewForAudioDeviceChange`
  in `sections/timeline/MainWindow.PreviewPlaybackState.cpp`, wired in `sections/frame/MainWindow.FrameBootstrap.cpp`.
  A hotplug or default-output switch pauses a playing preview; the user's resume is what re-anchors
  the transport. See `docs/superpowers/specs/2026-08-06-preview-audio-device-autopause-design.md` —
  in-place re-anchoring was tried three times and removed.
- MainWindow hooks: `MainWindow.cpp` (`ensurePreviewSfxRuntimePrepared`,
  `applyPreviewAudioSettingsToRuntime`); playback clock authority:
  `sections/timeline/MainWindow.TimelinePlayback.cpp` (`currentPreviewAuthoritativeAudioClockSecond`).

## 8. Video export — `src/tools/video_export/`

- Dialogs: `VideoExportDialog.{h,cpp}`, `BatchVideoExportDialog.{h,cpp}`, `VideoExportPreferences.h`.
- Controller + pipeline: `VideoExportController.{h,cpp}` (⚠ ~5000 lines — see god-file list),
  `VideoExportQuickRenderBackend.*`, `VideoExportAudioRenderPlan.*`, `VideoExportAudioBackend.h`,
  `BassExportAudioBackend.*`, `LegacyExportAudioBackend.*`, `RawVideoPipeTransport.*`,
  `VideoExportRuntimePolicy.*`. Key fns: `exportFullPreview`, `exportPreparedTask`,
  `buildVideoExportAudioRenderPlan`, `chooseVideoEncoder`.
- Snapshot boundary (contract): `VideoExportSnapshot.{h,cpp}` (`toJson`, `fromJson`,
  `buildVideoExportTaskFromSnapshot`).
- MainWindow ownership: `MainWindow.cpp` / `sections/export/*` (`onExportPreviewVideo`,
  `buildVideoExportSnapshot`, `launchVideoExportWorker`, `handleVideoExportWorkerEvent`).
- **Export hub page (E-C hybrid since 2026-06-11 — phases 1+2 of the export-page migration,
  implementation record; kept as local private notes):**
  `src/tools/export_page/ExportLauncherPage.{h,cpp}` (`miacode::export_page::ExportLauncherPage`,
  a `MainWindow`-friend widget like the latency page) — an `editorStack_` page reached via the
  sidebar `export` item, the toolbar Export button (now a direct jump; the old dropdown menu +
  250ms hover-open timer + `showExportToolbarMenu()` are DELETED), the Tools-menu
  `exportVideoAction_` (since 2026-06-12 — see below), and `MainWindow::switchToExportField()`.
  **Fixed-frame layout (2026-06-12 redesign):** the page itself NEVER scrolls and horizontal
  scrolling is forbidden everywhere — a pinned header (difficulty badge pill row + an UNDERLINE
  HORIZONTAL sub-nav, `role="subNavTab"` QToolButtons styled by
  `UiTheme::exportLauncherPageStyleSheet`; card frames + all hint/description text deleted) +
  `#ExportHeaderRule` hairline + the sub-page stack filling the rest. (A left nav COLUMN was
  tried and reverted: the quick-shell workspace surface can be ~700 logical px total and the
  embedded 6-tab panel needs the full content width; the panel's 560px dialog minimum is also
  dropped in embedded mode.) Four entries: **视频导出 (IN-PAGE — see "Embedded video panel"
  below)** / 封面导出 (dialog launcher pane) / 批量导出 (dialog launcher pane) / 打包ZIP
  (in-page action pane) — the three non-video panes are action-button-only ("↗" marks dialog
  launchers; greyed with a reason label when unavailable).
  **Embedded video panel (E-C):** NOT a separate panel class — `VideoExportDialog` itself gained
  an embedded mode (`setEmbeddedPanelMode(true)`: **`setWindowFlags(Qt::Widget)` — MANDATORY:
  QLayout only strips the Qt::Dialog window flag when it has to reparent, so a panel constructed
  with the host as parent would otherwise pop up as a top-level window while a phantom layout
  slot wrecks the page (the 2026-06-11 弹窗+错位 bug)** / no modality / no self-sizing
  height+width locks / done() & Esc-reject no-op / Cancel hidden; `startExport()` emits
  `exportConfirmed()` and the panel STAYS OPEN; the export button doubles as 取消导出 while
  running via `setEmbeddedExportRunning`). **Embedded fixed-frame internals (2026-06-12):** the
  in-panel transport strip (`previewStrip_`) is HIDDEN — the preview-area transport on the right
  is the only seek surface; `previewTimer_` runs for the panel's whole life and
  `onRangePreviewTick`'s embedded branch mirrors the main preview's authoritative clock into the
  range tab's current-time readout + the 设为起点/终点 seed (those also re-read the clock at
  click time); each tab page is re-hosted in a vertical-only `QScrollArea`
  (`#EmbeddedExportTabScroll`, horizontal policy AlwaysOff — content must compress) with the tab
  area stretched to fill and the Start-Export button box pinned at the bottom under an
  `#EmbeddedExportFooterRule` hairline; the 片头 tab's live `IntroPreviewWidget` is DELETED in
  embedded mode (tallest content, sacrificed so every tab fits without scrolling at default
  window sizes); tabs restyled by `UiTheme::embeddedExportTabStyleSheet` (flat underline).
  **D6 OVERTURNED 2026-06-12:** no UI entrance opens the modal form anymore — the Tools-menu
  「导出谱面」 action now jumps to this page (`MainWindow::onExportPreviewVideo` wrapper deleted;
  `ExportSection::onExportPreviewVideo` kept in code as the unreachable modal twin). Prefs
  persistence + the `refreshAddIntroEnabledState`/`applyUiToTask` bake-gate lockstep stay
  single-sourced in the one class. Lifecycle:
  `ExportSection::createEmbeddedVideoExportPanel(difficultyId,parent)`
  (shared ctor wiring via `buildConfiguredVideoExportDialog`; brackets the
  `begin/endExportPreviewSession` pair — exportPreviewActive_ + debug-HUD suppression — that the
  modal path also uses) on entering the video sub-page;
  `destroyEmbeddedVideoExportPanel()` (= dialog-close semantics) on leaving it. The page's
  `onPageLeft()` is called UNCONDITIONALLY from every page-leave switch function (same idempotent
  pattern as the latency teardown). Badge switch while the video sub-page shows re-seeds the panel.
  **Inline export progress (STATUS-BAR ONLY — 2026-06-13 redesign; supersedes the A3 "ride the
  PLAYBACK bar" amendment):** a panel-launched export creates NO `QProgressDialog` (every
  dialog-update site in `MainWindow.ExportWorker.cpp` is null-guarded). Progress (percent · stage ·
  ETA) shows in the STATUS BAR ONLY — the preview transport is NEVER touched by the export, so
  playback and a running export are NOT mutually exclusive (the worker is out-of-process). Helpers
  `begin/update/endInlineExportProgress` (`MainWindow.ExportWorker.cpp`) only write the status bar;
  `videoExportInlineProgressSecond_ >= 0` is now just the "inline export running" sentinel (no
  slider drive); `shellVideoExportProgressSeconds()` returns -1 so `QuickShellPreviewTransport.qml`
  does NOT override the quick-shell preview slider. `videoExportUseInlineProgress_` still marks the
  no-popup launch mode and survives the safe-mode retry; `beginInlineExportProgress` MUST still run
  after `videoExportWorkerSnapshot_` is stored; menu-launched exports keep the popup. (⚠ Earlier
  the slider rode CHART-TIME export progress with seeking disabled — reverted; do not reintroduce
  slider coupling.)
  **Playable export-page preview (所见即所导 — 2026-06-13):** the embedded video panel installs the
  BADGE-SELECTED difficulty as a real, PLAYABLE preview source so the NORMAL transport (play /
  pause / seek) drives the right-side preview even though `activeDifficultyId_ == 0` (D4 kept).
  `ExportSection::installExportPreviewAuditionScene(difficultyId)` (`MainWindow.ExportSnapshot.cpp`)
  mirrors `LatencySandboxController::installSandboxScene`: parse the difficulty's chart →
  `buildTimelinePreviewRefreshState` → publish `latestTimelineNoteMarkers_` / signature / revision /
  `latestTimelinePreviewSnapshotReady_` + `previewCanvas_->setNoteMarkers` + `timelineQuickModel_`
  rebuild (feeds `previewDurationSeconds()` / slider though the strip is hidden) + SFX
  `configureTimeline`, and set `state_.exportPreviewAuditionActive_`. **THREE difficulty gates must
  all OR-in the audition flag** (the latency page only needed the first two because it drives its
  OWN audition button, not the main transport): (1) `hasPreviewableChart()`
  (`PreviewTimelineFlow.cpp`); (2) the latency/audition early-path in `preparePreviewStartState()`
  (`PreviewPlaybackGlue.cpp`) — else `startQtPreviewPlayback` silently returns false; (3) the
  `playbackEnabled` branch in `DocumentSection::updateDifficultyScopedActionStates()`
  (`MainWindow.DocumentUi.cpp`) — else the play/pause/stop button + shortcuts are GREYED on the page
  (refreshed by `installExportPreviewAuditionScene` / `teardownExportPreviewAuditionScene` calling
  it). Installed in `createEmbeddedVideoExportPanel`
  (so a badge switch, which recreates the panel, re-installs the new difficulty); torn down in
  `endExportPreviewSession` via `teardownExportPreviewAuditionScene` (stops playback, clears the
  flag, invalidates the snapshot so the next field rebuilds — NO cache/restore, because leaving the
  page reinstalls the destination's preview). The export WYSIWYG forcing
  (`beginExportPreviewSession`: PV/BG on, debug-HUD off, export outline) stays. ⚠ This REPLACES a
  reverted "导出效果预览" experiment (upstream `21bd164`+`1465036` was cherry-picked then `git reset`
  away per product decision 2026-06-13; the adapted integration is parked on branch
  `backup/effect-preview-adapted-b0b41ff`). Intro preview is now the negative-time 片头 region below.
  **片头 (intro) preview at NEGATIVE time (2026-06-13, commit `62f88e2`):** when 添加片头 is on
  (full-range), the preview timeline extends left to `[-introDuration, 0)` (introDuration =
  `miacode::intro::kDurationSeconds` ≈ 5.82s) — the 片头 IS that negative segment. Scrub/click left of
  0 shows a STATIC intro frame; play advances through it (overlay frame stepped + a `track_start.wav`
  one-shot via the BASS `audition("track_start")` path — see 片头 audio below) and crosses 0 into the
  chart audition; pause freezes the frame. Driver =
  `TimelineSection` `enter/exit/render/startExportIntroAdvance` + `tickExportIntroLeadIn` +
  `handleExportIntroSliderSeek` (routes a slider seek < 0 into the region) in
  `MainWindow.TimelinePlayback.cpp`; overlay rendering is the re-added `PreviewRuntime`
  `setIntroOverlayData/setIntroOverlayFrame/clearIntroOverlay` + a z=3 `introOverlayLayer` in BOTH
  `PreviewRuntimeView.qml` and `QuickShellPreviewSurface.qml`.
  ⚠ **片头 styling (背景虚化 / 自定义背景 / 卡片阴影) is plumbed via a 5th `bannerStyle` arg
  on `setIntroOverlayData` carrying `introBannerStyleMap(spec)` (the SAME map the export mount
  applies in `PreviewQuickExportSession::setIntroBannerData`) → `PreviewRuntime.introBannerStyle`
  (QVariantMap prop) → each QML `syncIntroOverlayData()` loops the map onto IntroOverlay key-by-key
  (`backdropImage`/`backdropBlurEnabled`/`cardShadowEnabled`), mirroring the export's `setProperty`
  loop so preview ≡ export. `backgroundImage` is ALWAYS the 曲绘 jacket (card slot + backdrop
  fallback); the custom backdrop rides `backdropImage` in the style map — do NOT pass the custom
  path as `backgroundImage` (the pre-2026-06-16 bug: blur toggle did nothing in preview + custom
  bg wrongly replaced the card jacket). Both QML edited → touch their qrc (`preview_runtime_qml.qrc`
  + `quick_shell_qml.qrc`).
  ⚠ **片头 audio (2026-06-16):** BOTH sounds go through the **BASS audition path** (`audition(kind)` →
  loaded SFX sample), NOT `QSoundEffect` — the `QSoundEffect` path (even preloaded + qrc→temp file)
  was INAUDIBLE on this Windows/Qt build (GUI-confirmed dead), while `audition()` is proven. (a) the
  **opening jingle** (`track_start.wav`) is loaded as a pseudo-SFX kind `"track_start"`
  (`assetFileNamesForKind`+`previewSfxVolumeForKind`→answer bucket in
  `common/PreviewSfxAssets.h`/`audio/PreviewAudioSettings.h`; `BassPreviewAudioBackend.trackStartSample_`).
  **`track_start.wav` MUST ship in `assets/SFX/`** (the BASS backend loads from the resolved SFX dir,
  not qrc — the file is duplicated from `src/intro/audio/`). `startExportIntroAdvance` (head-gated)
  calls `ensurePreviewSfxRuntimePrepared()` + `audition("track_start")`. (b)
  **clock_count count-in** plays on the chart audition AFTER the 片头 hands off at 0 (✅ GUI-confirmed
  audible 2026-06-16): a per-tick
  scheduler (`setExportAuditionClockSchedule`/`resetExportAuditionClockCursor`/
  `maybeFireExportAuditionClockTicks` in `MainWindow.TimelinePlayback.cpp`, gated on
  `exportPreviewAuditionActive_`) fires `audition("clock")` at chart-time `index*60/clockBpm` for
  `index∈[0,clockCount)` — mirroring the export's `appendClockCountPlaybacks`. **NOT** injected into
  the SFX prepared timeline (the resume hand-off `resetCursor(0,false)` would skip the downbeat).
  Seeded from `installExportPreviewAuditionScene` (`clockCountFromDocument`+`clockBpmForChart`),
  cleared in `teardownExportPreviewAuditionScene`. ⚠ **Both backends must load a `"clock"` sample
  for `audition("clock")` to make sound** — added to `BassPreviewAudioBackend` (Windows/macOS/Linux:
  `clockSample_`, load+reset+`applySampleLevels`); the miniaudio compatibility backend gracefully
  no-ops (`sampleForKind` miss → silent count-in there). `clock.wav` ships in `assets/SFX/`.
  ⚠⚠ **The on-screen preview transport (default shell too) is the QML
  `QuickShellPreviewTransport.qml`** — its slider was hardcoded `from: 0` (the real clamp) — NOT the
  QWidget `ui_.previewSlider_`, which is null on the export page (so `updatePreviewSliderRange`
  early-returns there; that QSlider negative-min path is kept only as a fallback). The negative `from`
  is plumbed `TimelineSection::exportIntroLowerBoundSeconds()` →
  `QuickShellContracts::shellPreviewLowerBoundSeconds()` →
  `QuickShellController.previewLowerBoundSeconds` → the QML slider's `from`;
  `WindowSection::shellPreviewPositionSeconds()` returns the negative `exportIntroPlayheadSeconds_`
  during the region so the QML thumb follows; the QML progress-fill + `formatDisplayTime` handle
  negative time. **Touch `resources/quick_shell_qml.qrc` after editing that QML** (AUTORCC misses the
  dep). The dialog emits `introPreviewSettingsChanged` on 添加片头 / 片头 changes **AND on an
  export-RANGE gate flip** (`refreshAddIntroEnabledState`, when `isAddIntroActiveForPreview()`
  toggles — guarded by `introActiveForPreviewLast_` so a range drag doesn't re-emit per tick) →
  `refreshExportIntroState` (resize the range, enter/leave the region). ⚠⚠ **Stale-region
  invariant (2026-06-16 fix):** `exportIntroRegionActive_`/`exportIntroLeadInActive_` must never
  outlive `exportIntroEnabled()`. The bug: moving the range start off 0 flips the gate false
  without clearing the region → `shellPreviewPositionSeconds()` keeps returning the frozen
  negative `exportIntroPlayheadSeconds_` (thumb stuck far-left, scrubs overridden) + the play
  toggle hits the dead region branch where `startExportIntroAdvance` early-returns (no-op). The
  range-change path (`onRangeSpinChanged`→`refreshAddIntroEnabledState`) previously did NOT reach
  `refreshExportIntroState`. Three guards now keep the invariant: (1) the range-flip emit above
  tears the region down; (2) the seek funnel (`seekPreviewToSecond` + `seekPreviewDiscreteToSecond`)
  calls `exitExportIntroRegion()` on ANY chart seek (clamped ≥0), covering step/arrow/held/stop;
  (3) `onTogglePreviewPause` re-checks `exportIntroEnabled()` live on both intro branches and
  self-heals (exit + fall through) when stale. `exitExportIntroRegion` also resets
  `exportIntroPlayheadSeconds_=0`. Decision: KEEP the negative-time intro preview (the lower bound
  stays `-introDuration` when enabled) — only the staleness is fixed; "can't drag into 片头" was
  about RANGE selection, which already clamps `[0,total]`. ⚠ **添加片头 detection
  (`VideoExportDialog::isAddIntroActiveForPreview`) reads the checkbox + `rangeStartSeconds()`
  DIRECTLY, NOT `addIntroCheck_->isEnabled()`** — the embedded tab-rehost flips a widget's effective
  enabled state false while it stays checked, which silently collapsed the region/slider (cost ~3
  wrong-layer fix rounds). Modal keeps its own static `IntroPreviewWidget` (C3, unreachable).
  **Difficulty context (decision D4):** the page keeps `activeDifficultyId_ == 0`, so NOTHING on
  it may use `hasActiveDifficulty()`; the badge default = difficulty active before entering →
  kept page selection → `projectLastOpenedDifficultyId_` → first existing. Cards grey with a
  reason when no difficulty has a chart body (ZIP: empty document).
  **Explicit-difficulty plumbing:** `onExportPreviewVideo` / `onExportCover` /
  `onBatchExportPreviewVideo` / `buildVideoExportSeedTask` / `buildVideoExportSnapshot` all take
  `int difficultyId = 0` (0 = active difficulty, the menu-action behavior). For a non-active
  target, the seed/snapshot parse that difficulty's chart directly via
  `ExportSection::buildParsedMarkersForDifficulty()` (`MainWindow.ExportSnapshot.cpp` — parse +
  &first-shift, mirroring the worker rebuild) + an inline `MuriAnalyzer::analyze`, because the
  live timeline markers belong to the active difficulty only. Batch export treats the explicit id
  as a default-token hint only. The intro payload comes from
  `buildIntroBannerSpecForDifficulty(id)` (renamed from `buildActiveDifficultyIntroBannerSpec`).
  Neither the toolbar button nor `exportVideoAction_` (Tools menu) is difficulty-scoped anymore
  (`updateDifficultyScopedActionStates` touches neither): both jump to the page, which greys its
  own panes.

## 8b. Export as ZIP — `src/tools/zip_export/`

- Packs the current chart into a `.zip` (`maidata.txt` from `SimaiDocument::toText()` +
  canonical siblings `track.mp3` / `bg.{jpg,png,jpeg}` / sibling PV `pv.mp4`/`bg.mp4`).
- Core (backend-neutral, no Qt-widget dep, unit-testable): `ChartZipPackager.{h,cpp}`
  (`packChartToZip`, `sanitizedZipStem`). Spec: `ChartZipPackagerSpec.cpp` → CTest
  `chart_zip_packager_spec`. Compression via vendored **miniz** (`third_party/miniz/`).
- Asset resolution reuses `miacode::chart_assets` (`src/common/ChartAssetPaths.h`). Rules:
  one background image, one PV; `*_bak` backups excluded; an out-of-folder `&video=` target
  is skipped (no sibling fallback); only an empty chart body is fatal.
- Entry slot: `MainWindow::ExportSection::onPackAsZip()` in
  `sections/export/MainWindow.PackZip.cpp` (progress + result popup mirror batch export).
  UI: File menu under "Save As" + toolbox "Bookmarks" submenu, both via the single
  `packAsZipAction_` (created in `setupMenusAndActions`, reused in the toolbox build), **plus
  the Export hub page's 打包ZIP card (§8) — same slot, in-page entry (2026-06-11)**.

## 8c. Cover (difficulty-card) export — `src/tools/cover_export/`

- Renders the maimai difficulty banner card (`src/intro/qml/MaimaiBannerCard.qml`,
  the same card the intro pre-roll composites) to a single still image — the
  program-internal port of `tools/intro_remotion/qml/render-banner-from-maidata.ps1`
  + its `qml/exporter/main.cpp`.
- **Live WYSIWYG composer (2026-06-09)** — replaced the deleted `IntroCoverExporter` /
  `CoverCardRenderer` / `exportIntroCover` / `renderIntroCoverImage` / `CoverRenderOptions`. The
  cover is no longer a "card-fills-the-canvas" still; it is a composed scene (full-bleed
  background + the difficulty card as a free, draggable/scalable LAYER) edited and exported
  through ONE Qt Quick scene, so the live preview is pixel-identical to the export bar resolution.
  - **`CoverLayoutModel.{h,cpp}`** — layout source of truth. Each `CoverLayer` (QObject) stores
    NORMALISED geometry (`nx`/`ny` = layer centre 0..1, `sizeFraction` = content height / canvas
    height, `z`, `visible`, `locked`), so one model drives both the small preview and the full-res
    export with no rescaling. Exposed to QML as `layers` (`QList<QObject*>` Repeater model;
    per-property NOTIFY for two-way drag/scale binding). z-order helpers (`bringToFront` /
    `sendToBack` / `raiseLayer` / `lowerLayer`), `resetLayout()`, and `toJson`/`fromJson` (the layer
    block of the dialog's B2 layout save/import; per-chart auto-persistence still pending). Seeds
    **two** layers: `kind=="card"`
    (centred, `z=1`, visible) and `kind=="chartFrame"` (centred-behind, `z=0`, **hidden** until
    `setChartFrameEnabled(true)` — drives its `visible`). A `chartFrame` layer also carries a
    rendered square playfield still: C++ pushes it via `setLayerImage(key, QImage)` (stores
    `frameImage_` + bumps `imageRevision`); QML reads it through the **"coverchart" image provider**.
    `frameSeconds` records the grabbed chart time (persisted in `toJson`). `z=0` keeps the frame
    above the background dim (which paints before the Repeater) but below the card (`z=1`).
  - **`CoverComposer.qml`** (`src/intro/qml/`, **listed in `resources/intro.qrc`** → the
    AUTORCC-stale caveat below now bites when you edit it). The scene: a background fill layer
    (曲绘 / custom image / transparent, blurred-or-crisp + dim) + a `Repeater` over
    `coverLayout.layers`. The card layer hosts `MaimaiBannerCard` in **transparent mode** inside a
    wrapper sized so the card CONTENT (tab shoulder → card bottom) exactly fills it (clones the
    template, solves `card.heightRatio`/`topMargin` for "content fills the box", sizes the wrapper
    to `cardAspect`), so dragging/snapping the wrapper == the visible card. Move-drag (centre/edge
    **snap guides** + `clampCentre` keeping ≥25% on the clipped canvas), a corner **scale** handle,
    the selection border and guide lines are ALL gated behind **`editable`** (false in the export
    → no chrome baked in). The card drop-shadow is a `MultiEffect` `layer` on the card wrapper and
    works in EVERY background mode incl. Transparent (soft shadow on the alpha PNG). **Blur radii
    are resolution-invariant** (backdrop `blurMax` scaled by `canvas.height/1080`; shadow `blurMax`
    + offset by the card's px-per-native scale) so preview == export at any size. Initial selection
    fires from `onCoverLayoutChanged` (the host assigns `coverLayout` AFTER `Component.onCompleted`).
    Because the card runs transparent, its OWN `backgroundImage`/`backdropBlurEnabled`/internal
    `cardShadowEnabled` knobs are unused by the cover path (the composer draws bg + shadow itself).
    The `chartFrame` delegate is an `Image` whose source is `image://coverchart/<key>?r=<rev>`
    (`cache:false`, `PreserveAspectFit`); the `?r=` query is the `imageRevision` cache-bust, and a
    revision `< 0` (no still yet) yields a blank source.
  - **`SceneFrameRenderer.{h,cpp}` (chart-frame layer, Phase 2, 2026-06-09)** — renders a single
    chart frame at an arbitrary time T **in-process** for the `chartFrame` layer. Same capture rule
    as `CoverComposerView` (bare `QQuickWindow` + `grabWindow()` on the process RHI = D3D11; **no
    `QQuickView`, no forced OpenGL** — it is NOT the export worker), but it hosts a
    **`PreviewQuickSceneRoot`** (the same C++ chart scene the live preview + video export use; no
    QML/engine needed) with `kPreviewExportOverlayRenderLayers`
    (everything except the song-background media) captured over **transparent**, so the playfield
    (outline ring + notes + judge) composites as a layer over the cover's own background.
    `bootstrap(task)` maps the `VideoExportTask` → base `PreviewFrameState` ONCE (mirrors
    `VideoExportQuickRenderBackend::bootstrap`: note markers + progress cache + muri + skin/judge
    assets + render settings, incl. `outlineVariant`/`outlineImagePath`); each `renderAt(T, px)`
    only moves `playheadSeconds` and re-grabs the WARM scene (no re-parse — the same "only the
    playhead changes per frame" insight the export uses). The seeded `VideoExportTask` therefore
    must carry `skinDirectory` + `outlineImagePath` (added in `MainWindow.ExportFlow.cpp`, since the
    cover dialog gets `baseTask_` directly with NO snapshot rebuild).
  - **`CoverComposerView.{h,cpp}`** — host + export. The live view owns a bare `QQuickWindow`
    running `CoverComposer.qml`, embedded into the dialog via **`QWidget::createWindowContainer`**
    (which reparents + owns the window); drag/scale mutate the shared `CoverLayoutModel` with zero
    readback. `renderCoverComposite(model, inputs, fullSize, err)` does an OFFSCREEN full-resolution
    `grabWindow()` of the SAME scene + model (transient bare window, opacity 0, off-screen;
    transparent→ARGB32 PNG, else RGB32 JPG; guards an empty/`card`-less template); `exportCoverComposite(...)`
    renders + saves `card.png`/`card.jpg` (`(1)`,`(2)`… on collision). All presentation rides on
    **`CoverComposerInputs`** `{templateMap, trackOverrides, jacketPath, backgroundPath,
    backgroundMode (Jacket/Custom/Transparent), blurBackground, cardShadow}`. Both the live and the
    export engine register the **`CoverChartImageProvider`** (id `coverchart`, backed by the shared
    `CoverLayoutModel` via `QPointer`) BEFORE loading the QML, so the `chartFrame` `Image` resolves.
    - **Live chart-frame edit scene (A2, 2026-06-09):** in EDIT mode the `chartFrame` delegate hosts
      a LIVE `PreviewQuickSceneRoot` (`import MiaCode.Preview`) instead of the static grab `Image`, so
      scrubbing/playback only move the playhead with ZERO readback (the old per-scrub
      `grabWindow()` is gone; the offscreen grab is now export-only). `CoverComposerView`
      `qmlRegisterType`s that one type process-globally + idempotently BEFORE either engine loads the
      QML — the widget shell never runs the quick-shell bootstrap that normally registers it, and the
      `import` must resolve in both the live engine AND the export engine (even though the export path
      never instantiates the type). C++ `bindLiveChartScene` (called from the QML `Loader.onItemChanged`
      via the `chartSceneBinder` root property, set on the LIVE path only) configures the scene exactly
      like `SceneFrameRenderer`: `kPreviewExportOverlayRenderLayers` +
      the SHARED `PreviewFrameState` borrowed from the dialog's `SceneFrameRenderer`
      (`frameState()`/`setPlayheadSeconds()`), plus `clip:true` on the scene root for square-box parity
      with the export framebuffer. The export render (`editable=false` → `chartSceneBinder` null → the
      Loader stays inactive) keeps the static grab `Image`. The dialog severs the live scene's borrowed
      `PreviewFrameState` pointer (`detachLiveChartScene`) in its DESTRUCTOR BODY, before the
      `SceneFrameRenderer` member that owns it is freed — otherwise the QQuickItem (destroyed later in
      `~QObject`) could read freed state.
  - **Dialog (`ExportCoverDialog.{h,cpp}`)** — ctor now takes the full **`VideoExportTask`** (not
    just `IntroBannerSpec`): `task.intro` drives the card, and `task.noteMarkers`/`skinDirectory`/
    render-settings/`contentDurationSeconds` bootstrap the `SceneFrameRenderer`. `buildInputs()` maps
    the controls (size / background source 曲绘·custom+browse·transparent / blur / card shadow /
    level-text-render / long-text overflow) to `CoverComposerInputs`; hosts the embedded live
    composer + a "重置布局 / Reset layout" button; caches the parsed template once in the ctor.
    **Chart-frame picker (A2 live scene):** an "添加谱面帧 / Add chart frame" checkbox (disabled when
    the difficulty has no notes) + a play/pause button + a frame-time slider (`0..contentDurationSeconds`
    ms) + mm:ss.cs readout. The transport button is a 28×28 square (the theme sheet's QSS `min-width:
    92px` beats `setFixedWidth` and its `min-height: 30px` is a CONTENT-box bound that clipped the
    bottom border — both overridden to a 26px content box via appended QSS + `setFixedSize(28,28)`).
    Its icons are **QPainter-painted** (`makeTransportIcon`: slim 2.5px pause bars / triangle, theme
    `textPrimary`) — font glyphs were rejected twice: `U+23F8 ⏸` = color emoji on Windows, `U+275A ❚❚`
    = too heavy/wide. Enabling does ONE verify+seed grab under `Qt::WaitCursor` (reverts + warns
    on first-grab failure so the layer can't ship blank) and shows the LIVE edit scene. **Scrubbing /
    playback no longer re-grab** — `applyFrameSeconds` moves the shared playhead and repaints the in-QML
    `PreviewQuickSceneRoot` (zero readback). Play is **visual-only (no audio)**: a wall-clock `QTimer`
    (`QElapsedTimer`, ~60 fps) advances the playhead at real speed; the slider follows during play
    (its `setValue` is `QSignalBlocker`-guarded so the clock never self-pauses) and ANY user scrub
    (handle drag / groove-click / keyboard — all `valueChanged`) pauses. **Held ←/→ on the slider
    (2026-06-10)** reuses the preview transport's accelerate-and-cap design via
    `common/PreviewInteractionConfig.h` (an event filter swallows the slider's default 1 ms arrow
    step + OS auto-repeat; press steps ±1/120 s immediately, then a 16 ms `Qt::PreciseTimer` advances
    by real elapsed time × `min(1 + heldSeconds, 3.0)`, mirroring
    `MainWindow::TimelineSection::applyPreviewHeldSeekTick`). `renderChartFrameNow` (still
    re-entrancy- and `chartFrameEnabled()`-guarded) now grabs at the CURRENT shared playhead, so the
    exported still is WYSIWYG with the live scene. `exportCover` stops playback, then re-grabs at the
    exact export resolution (`chartFrameRenderPx` = `sizeFraction × outputHeight`) before compositing,
    and warns (non-fatal) if an enabled frame still has no still rather than silently shipping a cover
    missing it. Blur is gated to non-transparent. On accept the caller drives `exportCover(outputDir)`.
    **Controls layout (2026-06-10):** three `QGroupBox` sections — "尺寸 / 背景" (size, background
    source+path, blur), "难度卡" (an **"添加难度卡 / Add difficulty card" opt-in checkbox** + DX/SD
    chart type / shadow / level-text / long-text, sub-options gated on it), "谱面帧" (the add
    checkbox + frame-time row + inner-bg + brightness). **DX/SD chart type (2026-06-11):**
    `cardModeCombo_` ("谱面类型", data `"DX"`/`"Standard"`) feeds `trackOverrides["mode"]`; on
    `"Standard"` `MaimaiBannerCard.qml` shows the スタンダード plate top-right AND `mirror`s the
    MBase tab so its tall shoulder seats the plate (the prefab only ships a left-shoulder tab).
    Persisted in the composition JSON `card.mode` (B2 save/import + silent preference restore).
    The VIDEO intro path is still hardcoded `intro.mode = "DX"` in `MainWindow.ExportSnapshot.cpp`
    (`buildIntroBannerSpec`) — only the cover dialog exposes SD; banner_.mode just seeds the combo. The card toggle drives the card layer's `visible` on the shared model
    (NOTIFY → live scene + export both follow); reset re-ticks it, B2 import syncs it from the restored
    layer, and the QML selection chrome skips hidden layers (`l.visible` gate). The card drop shadow
    itself still works in EVERY background mode incl. Transparent. Esc closes the dialog even with
    focus inside the embedded NATIVE Quick window (which swallows keys before QDialog's default
    Esc-reject): the dialog filters the window via `CoverComposerView::previewWindowObject()`.
  - **Chart-frame inner-ring background (B1, 2026-06-09):** when "谱面帧内圈背景 / Chart-frame inner
    background" is ticked (and the cover background isn't Transparent), the chart-frame playfield disk
    shows the SAME cover background image (`backdropSourceUrl` = 曲绘/custom), crisp, dimmed by a
    "背景亮度" slider (in the 谱面帧 group); the OUTER ring stays transparent (the overlay's notes/ring/effects still
    extend across the square). Implemented **purely in `CoverComposer.qml`** (no scene-root / media /
    layer-flag change): a hidden source `Image` (`PreserveAspectCrop`) + a `MultiEffect` (`maskEnabled`)
    masked by a centred white-circle captured via `ShaderEffectSource` (`hideSource`, the repo's proven
    pattern), **dimmed by a black circle overlay at `opacity = 1 − brightness`** — the SAME
    multiplicative model the realtime preview's 内圈亮度 uses (stage-background dim) — NOT
    `MultiEffect.brightness`, which is additive and crushes dark pixels (fixed 2026-06-10). The slider
    seeds from the user's `task.backgroundBrightnessInner`, so the same value looks the same as the
    preview. The circle diameter = the playfield
    ring: `SceneFrameRenderer::playfieldDiskDiameterFraction()` = `layoutSquareScale ·
    effectiveLayoutRingDiameterRatio` (the exact ring math from `PreviewVideoGeometryConfig.h`), passed
    through `CoverComposerInputs.chartFrameDiskDiameter`. Renders identically in the live preview and
    the export (same QML, both engines). The controls gate on chart-frame-enabled + non-transparent bg.
  - **Layout save / import (B2, 2026-06-09):** "保存布局… / 导入布局…" buttons round-trip the WHOLE
    composition through one `*.miacover` file (a dedicated suffix since 2026-06-10; the payload is
    plain composition JSON, and import still accepts legacy `*.json`) — assembled at the DIALOG level
    (`exportCompositionJson` /
    `applyCompositionJson`), wrapping `CoverLayoutModel::toJson`/`fromJson` (layer geometry + visibility
    + `frameSeconds`) plus size / background mode+path+blur / card shadow+level-text+long-text /
    chart-frame inner-bg+brightness. Import sets every control signal-blocked, then re-grabs the chart
    still at the restored `frameSeconds` by driving `onChartFrameToggled` (the still itself isn't
    persisted). Import validates the root `kind == "miacode-cover-composition"` tag (rejects unrelated
    JSON rather than silently resetting). **Absolute-path strategy with fallback:** a `Custom`
    background whose `customPath` no longer exists (`QFileInfo::exists`) falls back to `Jacket`; a chart
    frame the importing session can't render (no notes / skin) is skipped — both surfaced in one
    non-blocking notice. (This explicit
    file import/export is separate from the still-pending per-chart/global portable-settings auto-memory
    — handoff §5.4 #4.)
  - **⚠ Render mechanism (two hard constraints — both cost a crash/bug to learn):**
    `CoverComposerView` (live) and `renderCoverComposite` (export) load the scene with a **bare
    `QQmlEngine` + `QQmlComponent`** parented into a **plain `QQuickWindow`** (the live one embedded
    via `createWindowContainer`; the export one shown at opacity 0, off-chrome), then
    captures with the public `grabWindow()`. Do NOT "simplify" this:
    1. **No `QQuickView`.** A `QQuickView` re-registers the QtQuick / QtQuick.Window modules in
       the **packaged (windeployqt) layout** → `Cannot install element '…' into protected
       module 'QtQuick'`. A bare `QQmlEngine`+`QQmlComponent` does not (it's how the intro
       export loads the same QML; same QtQuick/QtQuick.Effects imports).
    2. **No forced OpenGL / no manual QRhi.** This runs **in-process** in the GUI app, whose
       scene graph already runs on the platform RHI (Direct3D11 on Windows). Forcing an
       OpenGL graphics device (`QQuickGraphicsDevice::fromOpenGLContext` + FBO readback — fine
       in the *export worker* process, which forces OpenGL globally) **hard-crashes** here.
       Manual `QRhi` readback was also rejected: this Qt install ships `qrhi.h` but **no
       `Qt6GuiPrivate` CMake package**, so `Qt6::GuiPrivate` can't be linked. `grabWindow()`
       on a shown plain `QQuickWindow` uses the process RHI with public API only.
- **Over-long text + arbitrary LV (2026-06-08, `MaimaiBannerCard.qml`):**
  - **Marquee:** title/artist/designer/BPM are now `MarqueeText` (inline component) instead of
    raw `HorizontalFit`+`ElideRight`. Split on the `revealStartFrame` gate: animated intro
    (`>=0`) renders FIXED-size and MARQUEE-scrolls over-long text left (`root.marqueeOffset`,
    frame-driven — NOT a QML animation, which won't tick headless); the still cover export (`<0`)
    `HorizontalFit`-shrinks then left-aligns + clips (a still can't scroll). No more ellipsis.
    Tuned via template `marquee:{startHoldFrames,maxSpeedPxPerFrame}`.
  - **LV render mode:** template/track `lvRenderMode` = `"atlas"` (default, pre-baked digit
    sprites, `[0-9]`/`+` only) or `"text"` (raw level STRING, no "Lv" prefix, bundled Heavy font
    via `Text.Outline`+`MultiEffect` shadow, `Text.Fit` inside `layout.lvTextArea`, per-difficulty
    `lvRendered` palette). In `atlas` mode `effectiveLvMode()` AUTO-FALLS-BACK to text when the
    level carries an unsupported glyph (never silently drops chars). **No new font** (reuses the
    bundled RHR Heavy). **Sync set for `IntroBannerSpec::lvRenderMode`:** struct
    (`VideoExportController.h`) → cover `buildInputs` → `CoverComposerInputs.trackOverrides`
    (`ExportCoverDialog.cpp`) + intro track map (`VideoExportQuickRenderBackend.cpp`) → snapshot
    `lv_render_mode` (`VideoExportSnapshot.cpp` to/fromJson) → UI toggle "等级文本渲染" on
    `ExportCoverDialog` (folded into `buildInputs`'s `trackOverrides`).
- **Card fonts** (shared by the intro pre-roll AND the cover export — same `MaimaiBannerCard.qml`):
  title (`displayFont`) uses **`ResourceHanRoundedCN-Heavy.ttf`** and body (`bodyFont`) uses
  **`ResourceHanRoundedCN-Bold.ttf`** (思源圆体 / Resource Han Rounded, OFL) — Heavy gives the
  title prominence over the Bold body. It natively covers simplified Chinese (no system
  fallback) and approximates SEGA MaruGothic. Replaced the old M PLUS 1p Black / M PLUS
  Rounded 1c Bold pair (deleted). Wired in `src/intro/assets/fonts/` + `resources/intro.qrc`
  + `src/intro/templates/maimai_banner.json` (`fonts.display`/`fonts.body`) + the `FontLoader`
  fallbacks in `MaimaiBannerCard.qml`.
  - **The two bundled TTFs are SUBSET** (via `pyftsubset`) to 中(通用规范8105)/日(JIS X0208 kanji
    +假名)/英(Latin)/俄(Cyrillic)+希腊/标点/全角/常见符号 — **NO Korean** (RHR-CN ships no Hangul
    anyway). Heavy 13.05→5.03 MB, Bold 13.33→5.12 MB (~10 MB total vs ~26 MB full). The subset
    recipe + source `.7z`/full TTFs live in `font_candidates/_subset/do_subset.py` (uses
    `china/standard/tongyong_guifan.txt` from ButTaiwan/cjktables). Re-subset from the full
    fonts if coverage needs to change. Family names stay distinct (`Resource Han Rounded CN`
    vs `…CN Heavy`) so the title/body weight split survives subsetting.
  - OFL compliance: `licenses/Resource-Han-Rounded-OFL.txt` is shipped in the package
    (`package-win.ps1` copies repo `licenses/` → `<dist>/licenses/`, validated in
    `$requiredPackagePaths`).
  - **Jacket is a true 1:1 square (320×320 native window at (51,42)).** The `UI_TST_MBase_*`
    frame PNGs had earlier been top-cropped (commits dab3f11 + 4f9f71d, "crop … so it aligns
    under the Tab") which squashed the square window to 319×304 — that broke 1:1. The pre-crop
    (square-window) frames were recovered from git and restored (originals also stashed in
    `font_candidates/original_frames/`). Instead of cropping, the Tab+plates are raised 18 native
    px (`mbaseTab.y -36→-54`, plates `-31→-49`) so the shoulder sits on the card's top border,
    not over the jacket. Canvas (420×636) + `heightRatio`/`topMargin` are unchanged, so the card
    BOTTOM edge stays fixed. `jacketSlot` = `{51,42,320,320}`.
  - The jacket has a crisp 2-device-px black frame (`MaimaiBannerCard.qml`, declared AFTER
    tab+plates, BEFORE the LV pill so the top edge isn't occluded and the pill stays on top).
    It snaps each edge to a whole device pixel in ABSOLUTE space (folding in fractional
    `geom.cardX/Y`) and uses `border.width: 2/geom.cardScale` so all four sides are equal — a
    plain native-space stroke renders uneven under the non-integer card scale.
  - **All 7 difficulty cards exist** (ids 1–7 = Easy/Basic/Advanced/Expert/Master/Re:Master/Utage).
    The prefab only shipped the 5 standard colours (Basic→Re:Master = ids 2–6); **Easy (blue
    `#69A6FF`) and Utage (orange `#E29A46`) are fan-made hue-recolours** matching MiaCode's own
    `difficultyColor()` palette (`MainWindowShared.cpp`). Generator + recipe:
    `tools/lvcard_gen/recolor_difficulty_cards.py` (masked-hue remap of EXP/ADV frame+tab+pill +
    atlas `_03`/`_02`, re-letters the baked name). The baked difficulty NAME is not a plain
    font+shadow — pixel cross-sections of MASTER/EXPERT show a **4-layer build**: near-white fill
    → inner bright stroke → thin DARK outline ring at a small stand-off → soft drop shadow, set in
    SEGA **FOT-NewRodin Pro UB** (~0.87 horizontal condense, cap 28, centred x≈140 baseline y≈403).
    NewRodin is commercial → NOT bundled; the generator reads it from `build/fonts_ref/` (env
    `NEWRODIN_UB`) and only the rasterised PNG ships. Output sprites: `UI_TST_MBase_{EASY,UTAGE}.png`
    (+`_Tab`, `_LV_`) in `src/intro/assets/trackstart/` and `UI_NUM_MLevel_{EASY,UTAGE}.png` in
    `src/intro/assets/lv_atlas/` (atlas also copied to `tools/intro_remotion/public/assets/intro/lv_atlas/`).
    Wired by `EASY`/`UTAGE` keys in **both** `maimai_banner.json` (frame/tab/lvPill/lvAtlas),
    `resources/intro.qrc`, and `introDifficultyAtlasKey()` in `MainWindow.ExportSnapshot.cpp`
    (case 1→`EASY`, 7→`UTAGE`; previously fell back to BASIC/MASTER stand-ins).
- **Mirror QML/template edits to the prototype copies under `tools/intro_remotion/qml/`** for
    the standalone exporter preview. ⚠ Editing only `src/intro/qml/*.qml` (or qrc-listed assets)
    may NOT rebundle (AUTORCC misses the qrc→content dep): delete `build/**/qrc_intro.cpp*` (or
    touch `resources/intro.qrc`) to force RCC.
- Sub-dialog controls + embedded live composer: `ExportCoverDialog.{h,cpp}` (detailed above);
  size presets mirror the video-export resolutions, seeded from the persisted video size, tracked
  independently. **All dialog settings persist to app preferences (2026-06-10):** the whole
  composition JSON (`exportCompositionJson`) saves under `app.cover_export` (the portable
  preferences object, same store as `miacode::video_export::loadDialogPreferences`) when the user
  exports, and silently restores on the next dialog open (`applyCompositionJson(saved,
  interactive=false)` — fallback notice boxes suppressed).
- Entry slot (2026-06-11): the **Export hub page's 导出封面 card** (§8) →
  `MainWindow::ExportSection::onExportCover(difficultyId)` (`MainWindow.ExportFlow.cpp`) — the
  toolbar Export dropdown is GONE; the toolbar Export button now jumps straight to the Export
  page. The seed `VideoExportTask` comes from
  `ExportSection::buildVideoExportSeedTask(difficultyId)` (shared with `onExportPreviewVideo`):
  `task.intro` (`IntroBannerSpec`, via `buildIntroBannerSpecForDifficulty()` wrapping
  `buildIntroBannerSpec` in `MainWindow.ExportSnapshot.cpp`) drives the card, and
  `task.noteMarkers`/`skinDirectory`/`outlineImagePath`/render-settings/`contentDurationSeconds`
  feed the chart-frame `SceneFrameRenderer`. Output dir = the chart directory. (The former
  `VideoExportDialog::openExportCoverDialog` + its Font-tab button are REMOVED.
  **皮肤 panel reorg (2026-07-04/05):** skin / judge line / 字体 (embedded HUD-font picker) are
  now ONE shared owner-wired panel — `DialogsSection::buildSkinSettings(parent, skinOut,
  includeFolderButtons)` (`MainWindow.Dialogs.ExportSettings.cpp`, alongside
  `buildExportInjectedSettings`) — reused by BOTH a main-window **皮肤设置 popup** (`onSkinSettings`
  → `openSkinSettingsDialog`; toolbar button `skinSettingsButton_`/`skinSettingsAction_` sits
  between 预览设置 and 导出 at the SAME width as the 导出 button, wired in
  `MainWindow.FrameBootstrapFinalize.cpp`) AND the export dialog's **皮肤 tab** (injected via
  `VideoExportDialog::injectOwnerWiredSettings(videoExtras, gameplayWidget, skinWidget)`). Intro
  sound + a 当前谱面资源 readout were part of an earlier draft but were DROPPED (2026-07-05) — the
  panel is skin / judge line / font only. The HUD-font controls are an EMBEDDABLE widget
  `miacode::video_export::createHudFontSettingsWidget(parent, onFontChanged)`
  (`tools/video_export/HudFontSettings.{h,cpp}` — the former modal `openHudFontSettingsDialog` +
  the export dialog's `hudFontSettingsButton_`/`openHudFontSettingsDialog()`/`refreshLivePreviewHudFont()`
  are REMOVED). The export dialog's standalone **字体 tab is GONE**; the 视频设置 dialog
  (`onPreviewVideoSettings` → `openPreviewSettingsDialog`) dropped its skin/judge-line rows + the
  音乐 + 字体 tabs and now reads **视频 / 游戏 / 性能** (性能 = 预览刷新率). `buildExportInjectedSettings`
  keeps only 判定效果 / slide 层叠 / 中心显示. ⚠ **W1 note:** the preview-settings
  `createDialogMenuButton` must keep the `ensurePolished()`+`setFixedHeight(qMax(sizeHint,30)+4)`
  or its dropdowns clip their bottom border (`qt-ui-layout-pitfalls` W1).) The **default** (no custom font) HUD
  family is **"Xiaolai Mono"** — embedded resource `:/fonts/xiaolai_mono.ttf`
  (`resources/fonts.qrc` → `assets/fonts/XiaolaiMono-Regular.subset.ttf`), loaded in
  `PreviewHudState.cpp::previewHudTimestampFont` (replaced the old JetBrains Mono, 2026-06-19).
  The bundled .ttf is a CJK subset (21 MB → ~5.5 MB) regenerated by
  `scripts/assets/subset_hud_font.py` (通用规范汉字表 8105 + JIS X 0208 + Latin/kana/symbols, same
  recipe as `font_candidates/_subset/do_subset.py`); editing the subset needs the AUTORCC repack. **⚠ Note:** `CoverComposer.qml` is a NEW file in `intro.qrc`, so
  editing it (or `MaimaiBannerCard.qml`) DOES need the AUTORCC repack — delete `build/**/qrc_intro.cpp*`
  / touch `resources/intro.qrc` to force RCC (cf. the over-long-text mirror note above).
- **Export dialog "片头" tab (2026-06-11):** the 添加片头 checkbox moved from the 视频 tab to a
  dedicated **片头 tab** (tab order: 输出 / 视频 / 游戏 / 皮肤 / 片头 / 导出区间) carrying the cover-dialog-style controls —
  添加片头 as the bold master switch wrapped in a neutral rounded box (`QFrame#AddIntroCapsule` —
  inputBg + border + 8px radius, matching the 游戏 tab dropdown chrome; styled by
  `UiTheme::exportDialogStyleSheet` so it re-themes; 2026-06-19. The old "?" dev-status badge +
  tooltip were REMOVED per user), 背景 group (背景源 曲绘/自定义图片 + path/browse + 背景虚化)
  and 难度卡 group
  (谱面类型 DX|SD / 难度卡阴影 / 等级文本渲染 with a `miacodeAllowTooltip` tooltip "atlas only
  covers 0-9/+") — plus a **read-only live preview** (`IntroPreviewWidget`,
  `src/tools/video_export/IntroPreviewWidget.{h,cpp}`: a native `QQuickView` in a
  `createWindowContainer` hosting the REAL `IntroOverlay.qml` pinned at card-hold frame 120,
  no-focus + key-forwarding event filter). **⚠ Hosting rule (2026-06-12):** it was a
  `QQuickWidget` first — that breaks the quick shell: the first texture child inside the
  rehosted `QuickShellWorkspaceSurface` (the embedded Export-page panel) forces a top-level
  HWND destroy+recreate, the shell's `fromWinId` WindowContainer keeps the dead handle, and
  the whole workspace pops out as a floating frameless window (整列错位, persisted across
  pages). NO `QQuickWidget`/`QOpenGLWidget` under any rehosted surface — see
  qt-ui-layout-pitfalls recipe **Z6**. **Preview geometry (2026-06-11 fix):** the widget is a FIXED
  320×220 box (the dialog must NOT resize when the resolution preset changes); inside it a
  letterboxed `clip:true` QQuickItem stands in for the export framebuffer and the overlay root
  keeps its NATIVE 1920×1080 layout with `applyIntroGeometry`'s exact transform (scale to frame
  height around centre + centre-crop) — do NOT re-layout the overlay at the output aspect: the
  export centre-crops a 16:9 render (`PreviewQuickExportSession::applyIntroGeometry`), so a
  re-layout diverges on 1:1/4:3 backdrop framing. **Gate (2026-06-11 fix):**
  `refreshAddIntroEnabledState` greys 添加片头 only when the range START is non-zero, mirroring
  `applyUiToTask`'s bake gate (a [0, partial] clip counts as full-range and carries the intro)
  — the two gates must stay in lockstep. **⚠ Wiring rule:** `buildVideoExportSnapshot`
  (`MainWindow.ExportSnapshot.cpp`) REBUILDS `built.intro` from the live document and must call
  `copyIntroStyling(requestedTask.intro, &built.intro)` (`VideoExportController.h`) — without
  it every dialog styling choice silently resets to defaults at export launch (the original
  "settings don't affect the export" bug). Deliberately ABSENT: 文字超长 (the animated
  intro always marquee-scrolls; shrink/ellipsis are still-cover-only), 透明 background (video
  has no alpha), and 添加难度卡 (an intro ALWAYS carries the card — no `cardEnabled` knob
  anywhere). **Contract:** `IntroBannerSpec` gained `backgroundMode`("jacket"|"custom") /
  `customBackgroundPath` / `blurBackground` / `cardShadow`; spec→QML mapping is the shared
  `introBannerTrackMap`/`introBannerStyleMap` (`VideoExportController.h`), used by BOTH
  `VideoExportQuickRenderBackend::setupIntro` (→`PreviewQuickExportSession::
  setIntroBannerData(..., style)` → `IntroOverlay` root properties `backdropImage`/
  `backdropBlurEnabled`/`cardShadowEnabled`) and the dialog preview (`applySpec`) — preview ==
  export by construction. Snapshot keys `background_mode`/`background_custom_path`/
  `background_blur`/`card_shadow` (`VideoExportSnapshot.cpp`); dialog prefs keys
  `intro_background_*`/`intro_card_type`/`intro_card_shadow`/`intro_level_text_render`
  (`appendIntroPersistedSettings`). `applyUiToTask` injects `currentIntroSpec()`. The intro card
  shadow is drawn by `IntroOverlay` (layer MultiEffect) — `MaimaiBannerCard.cardShadowEnabled`
  is a no-op in transparent mode. The custom backdrop replaces only the blurred backdrop; the
  card's jacket slot still shows the 曲绘. Batch export (`BatchVideoExportDialog`) and the CLI
  keep spec defaults (jacket/blur/DX).

## 9. Latency settings (BPM / offset / clock_count) — `src/tools/latency/`

- `LatencyDetectionPage.*`, `LatencyAnalysis.*`, `LatencySandboxController.*`,
  `LatencyTestChartBuilder.*` (an in-sidebar page + sandbox audition).
- **UI** is one "谱面参数" (Chart Parameters) card holding three grid rows
  (label / spin / 自动检测 / result) for BPM, 偏移 (Offset), and `clock_count`, plus the
  audition card; the audio/video media-tools launcher sits in the page back-bar (not on
  the Offset row) so the rows stay uniform. `clock_count` (spin 1–64, default 4) is a
  plain manual field — no auto-detect button and BPM detection does NOT write it — that
  saves through `MainWindow::applyLatencyDetectorClockCount` → `parsedClockCount` →
  `extraFields`. The `自动检测 Offset` entry point is visible here (wiring always intact).
- **Detection algorithm** lives in `LatencyAnalysis.*` (pure, GUI-free): `decodeMonoTrack` →
  `buildOnsetEnvelope` (energy-flux, for BPM) + `buildTransientEnvelope` (abs-diff, for offset)
  → `detectBpm` (autocorrelation + meter-template comb) + `detectOffset` (per-beat phase scan,
  result folded to bar/quarter/eighth). All the envelope-mix / phase-penalty weights are a
  defaulted **`DetectionTuning`** struct threaded through `buildOnsetEnvelope`/`detectOffset`
  (default-constructed = production behavior; GUI callers pass nothing). Phase-1 field
  `offsetEdgeWeight` (default 0.5) blends the transient envelope with its positive slope so
  scoring locks onto the attack rising edge, not the late amplitude peak (kills the soft-onset
  +24ms bias). Corpus-tuned defaults (`offsetPhasePenalty` 0.14, `offsetEdgeWeight` 0.5) lifted
  the batch pass rate 52.8%→63.0% @10ms — see `docs/OFFSET_DETECTION_BASELINE_v1_ZH.md`.
- **Offline batch evaluator:** `LatencyBatchTest.cpp` → dev tool **`latency_offset_batch`**
  (built behind `MIACODE_BUILD_DEV_TOOLS`, NOT a CTest case; own miniaudio-impl TU
  `LatencyBatchAudioImpl.cpp`, decode-only). Walks a root for `maidata.txt`+`track.*` projects,
  parses declared BPM (`&wholebpm`→`&bpm`→inline `(NNN)`) + `&first`, runs `detectOffset`, and
  scores the error **folded modulo one 8th-note** (an integer number of 8th-notes = 0 error).
  Every `DetectionTuning` field is a CLI flag (`--transient-weight`, `--phase-penalty`,
  `--snap-threshold`, …); `--bpm-source chart|detect`, `--snap-mode`, `--tolerance-ms`, `--csv`.
  **Sweep mode:** repeatable `--sweep name:start:stop:step` runs a Cartesian param grid,
  decoding each project ONCE and reusing its envelopes across all combos (onset envelope rebuilt
  per combo only when an `onset-*` dim is swept), then prints a combo table ranked by pass%. Used
  for Phase-0 coordinate-descent tuning against the `&first` ground-truth corpus.
- UI title is **"延迟设置" / "Latency Settings"** (page header in
  `sections/document/MainWindow.DocumentUi.cpp`, gated on `activeOutlineKey_=="latency"`).
- **Entries (2026-06-11, L-A migration — the sidebar item is GONE):** (1) the metadata page's
  "延迟与偏移校准" entry card (built in `MainWindow.FrameBootstrap.cpp` after the metadata card;
  BPM/offset summary label `latencyEntrySummaryLabel_` refreshed by `populateMetadataPage` from
  `parsedWholeBpm` + `document_.first`) → `switchToLatencyField()`; (2) the Tools menu's
  "BPM && 延迟检测" `latencyDetectorAction_` (kept as the keyboard-reachable direct path).
  The page carries a "← 返回谱面信息" bar at the top of `LatencyDetectionPage::buildUi()`
  (`QPushButton#LatencyBackButton`, styled in `UiTheme::latencyDetectionPageStyleSheet`) →
  `switchToMetadataField()`. While the page shows, the sidebar highlights **metadata**.
  Page body + lifecycle contracts (onPageEntered/onPageLeft, SFX isolation, Ctrl+S filter)
  are UNCHANGED by the migration — only the way in/out moved.
- **Audition reuses the main preview transport** — it is NOT a separate player. The page
  installs the synthesized test chart as the preview source and plays it through the real
  `startQtPreviewPlayback`/`onQtPreviewTick`. `LatencySandboxController` is a thin
  install/teardown + UI-poll layer; the page button → `toggleAudition()` →
  `MainWindow::onTogglePreviewPause()`. See `cross-chain-linkage.md` §12.
- **Live param edits (incl. mid-playback):** `setBpm/setOffsetSeconds/setSubdivision` funnel into
  `regenerateAndPushIfActive()` → `setupSandboxPreviewState()`. The song audio is the fixed master
  clock and is never re-seeked here; only the test-chart notes + SFX timeline are rebuilt. Because
  `QtPreviewSfxRuntime::configureTimeline()` resets the SFX event cursor to 0, while playing we
  follow it with `resetCursor(currentPreviewAuthoritativeAudioClockSecond(), false)` so taps before
  "now" aren't re-fired (the music keeps playing untouched).
- **Ctrl+S (works):** the app has no global undo stack (undo = text editor only). Ctrl+S on this page
  is routed via an **app-level `eventFilter`** (installed on `qApp`) → `MainWindow::onSaveFile()`, NOT a
  page `QShortcut` — a page QShortcut on the same key as the global Save `QAction` is *ambiguous* (Qt
  fires neither). Scoped to `isVisible() && (target==this || isAncestorOf(target))`; `onPageEntered()`
  takes focus so it catches the key on entry. Spin boxes use `keyboardTracking(false)` so a half-typed
  value is never applied.
- **Page-local Ctrl+Z/Y undo — ATTEMPTED & REVERTED (failed).** A dedicated BPM/offset history plus an
  eventFilter Undo/Redo branch was tried; the keys never reached the page-local handler reliably.
  Removed — Ctrl+Z/Y fall through to the global Undo/Redo handler. Failure noted in code at the
  `LatencyDetectionPage` ctor + `eventFilter()`. Don't re-add without a different mechanism.
- **SFX-volume audition tap — ATTEMPTED & REVERTED (failed).** Dragging the SFX slider used to fire a
  one-shot tap (`LatencySandboxController::playSfxAuditionSample()` → `QtPreviewSfxRuntime::audition`);
  it could not be kept silent during playback (kept sounding despite guarding on `qtPreviewPlaying_`,
  primary suspicion: a stray play/audition event scheduled elsewhere). Removed — the slider now only
  sets the volume. Failure noted in code at `onSfxVolumeChanged` + the deleted-method comment in
  `LatencySandboxController.cpp`. Default SFX volume = `kDefaultSfxVolumePercent` (**50**; stored
  per-user under `latency/sfxVolumePercent`).
  - **Root cause of that "stray play event" — IDENTIFIED & FIXED (2026-06-01).** The mid-playback noise
    when dragging *any* SFX/volume slider was NOT the deliberate audition: it was
    `BassPreviewAudioBackend::applyLevels()` calling `resetCursor(authoritativeSecond(), false)` after its
    group rebuild. During active playback `authoritativeSecond()` is FROZEN at the last start/seek snapshot
    (MainWindow's wall-clock is the live SFX master and is never written back to `lastAuthoritativeSecond`),
    so re-seeking to it rewound the event cursor to the playback start and the next `drainEvents(wallClock)`
    replayed every tap from there in one burst. This hit BOTH the latency page and the audio-settings
    dialog — both funnel through `applyPreviewAudioSettingsToRuntime` → `applyLevels`. Fix: anchor the
    post-rebuild `resetCursor` to the last-fired group's chart-second instead
    of `authoritativeSecond()`, preserving the fired/unfired boundary whether playing or paused. The miniaudio
    backend's `applyLevels` (volumes only, no cursor touch) was already correct. A re-attempt of the
    slider-drag audition is now viable if gated to non-playing state.
- **SFX-level isolation (audition vs normal preview) — REDESIGNED (2026-06-01).** Two modes share ONE
  `previewSfxRuntime_`; runtime levels are now a PURE FUNCTION of the current mode, re-dispatched through
  the single entry `MainWindow::applyPreviewAudioSettingsToRuntime()`. On the latency page
  (`LatencySandboxController::isOnPage()`) it pushes `makePreviewLatencyAuditionLevels(mix, sfxPercent)`;
  otherwise the user's real mix. No snapshot/restore, no `latencySandboxAuditionActive_` *audio* gate
  (that flag now only gates *playback* — `hasPreviewableChart`). **Old leak root cause:** the sidebar
  handler overwrote `activeOutlineKey_` with the destination BEFORE calling `switchToXField`, so the
  `== "latency"` teardown guard was always false and `onPageLeft()` never ran → the audition override
  stayed live on the shared runtime. Fixed by ungating `onPageLeft()` (idempotent) from
  `activeOutlineKey_`. Full contract in `cross-chain-linkage.md` ("SFX-level isolation").
- The three gray hint labels under BPM / Offset / SFX-volume were removed from `buildUi`.
- The "auto-detect Offset" button is currently hidden (`detectOffsetButton_->setVisible(false)` in
  `LatencyDetectionPage::buildUi`) — code/wiring kept; auto-detect BPM is unaffected.

## 10. Muri analysis — `src/tools/muri/`

- `MuriAnalyzer.{h,cpp}` (key fn `analyze`, now a ~187-line thin orchestrator; ~1300 lines of
  cross-stage shared primitives + that orchestrator), `MuriStaticChecker.*`
  (`buildStaticMuriReferences`), `MuriPanelEntries.*` (`buildVisibleMuriPanelEntries`),
  `MuriDump.cpp` (dev tool). Types: `src/common/MuriTypes.*`, config `src/common/MuriConfig.h`.
- **Pipeline decomposition (✅ complete, namespace `miacode::muri::detail`):** `analyze()`
  orchestrates per-stage TUs — `MuriAnalyzerGeometry.*` (pure geometry), `MuriAnalyzerModel.h`
  (shared structs), `MuriSlideReferenceData.*` (`slide_data.json` resource layer),
  `MuriRuntimeModelBuilder.*` (Stage 1: marker → runtime note/touch model), `MuriOverlayBuilder.*`
  (Stage 2: `buildOverlayActions` pad-windows/trails + `buildSlideState`/`buildWifiState`),
  `MuriSlideWifiJudge.*` (Stage 3: `simulateRuntimeSlideAndWifiJudgments` tick-stepped
  "judged-too-fast" simulation), `MuriSimpleNoteJudge.*` (Stage 4: `collectSimpleNoteRuntimeDiagnostics`
  — simple-note judging + multi-touch + tap-on-slide/overlap diagnostics), `MuriDiagnosticLabels.*`
  (diagnostic vocabulary: labels / source anchors / alert text / marker lookups),
  `MuriDiagnosticCollector.*` (Stage 5: output-owning collector — `addDiagnostic`/`add*SpriteEvent`
  + `finalize()` sort/dedupe). Remaining cross-stage primitives (slide predicates, pad geometry,
  timing, pad-window/trail + hand-action + pad-event builders) are declared in
  `MuriAnalyzerInternal.h`, defined in `MuriAnalyzer.cpp`. History + optional follow-ups:
  `.claude/MURI_DECOMPOSITION_HANDOFF.md`.

## 11. Batch transforms — `src/core/chart/transform/`

- `ChartBatchTransform.{h,cpp}` (`transformChartText`, `toggleBreak/Ex/FireworkForSelection`,
  `randomRotateForSelection`), `ChartNormalization.{h,cpp}` (`normalizeChartText`),
  `Non384SnapTable.*`. MainWindow entries: `onMirror*`, `onRotate*`, `onNormalizeWholeChart`,
  `onToggle*Selection`.

## 12. Toolbox media utilities

- `sections/dialogs/MainWindow.Dialogs.cpp` + `MainWindow.DialogsSection.cpp` — prepend
  silence/black, compress bg video, convert track to 44100 Hz (`onPrependTrackSilence`,
  `onPrependPvBlack`, `onCompressBackgroundVideo`, `onConvertTrackTo44100Hz`). These four open
  from a single popup, `onMediaProcessingTools()` (one button + one-line description each),
  reached via the toolbox's "音频/视频处理 / Audio/Video Processing" entry — not a hover submenu.
  The shared `runFfmpegBlocking(... totalDurationSeconds, error)` helper drives a determinate
  progress bar by parsing ffmpeg `-progress pipe:1` `out_time_us=` against the expected output
  duration (falls back to an indeterminate bar when duration is unknown).
- Toolbox menu itself is built in `sections/frame/MainWindow.FrameBootstrap.cpp` (`toolboxMenu_`).
  BPM & Latency was dropped from the toolbox (still in the top Tools menu via
  `latencyDetectorAction_`); Copy Area is gated off by the local `kCopyAreaIntegratedIntoToolbox`
  constant (feature kept: `copyAreaPanel_`/`fullCopyAreaAction_`/`setFullCopyAreaVisible`).

## Update this file when

- A feature owner moves file, or a class/function becomes the new canonical entry point.
- A capability gains a second mirrored path (also note it in `cross-chain-linkage.md`).
