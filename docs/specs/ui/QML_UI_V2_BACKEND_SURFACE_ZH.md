# `src/app/qml_ui` → `MainWindow` 依赖清册

> 归属：[QML_UI_V2_PHASE1_TODO_ZH.md](QML_UI_V2_PHASE1_TODO_ZH.md) 阶段 3.5 第 2 / 3 项。
>
> 第 2 项要求 `QmlApplicationContext` 不再持有 `MainWindow& backend_`。这不是一次改名，
> 而是一份可以逐项消化的搬迁清单：本文登记 **QML 层今天还从隐藏窗口取走的每一个名字**，
> 按未来的所有者分组，好让「还剩多少」是一个可以看见、且只能往下走的数字。
>
> **漂移守卫**：`qml_ui_backend_surface_spec`（`src/tools/qml_ui/QmlUiBackendSurfaceSpec.cpp`）
> 扫描 `src/app/qml_ui/` 全部源文件，把实际用到的名字和下面的清单做**集合相等**比较：
> 多一个（新耦合）失败，少一个（搬走了但没更新本文）也失败。搬走一项 = 删掉本文一行，
> 计数自然下降；新增一项必须显式加行，评审能看见。
>
> **计数（2026-09-01）**：方法 **120**，直接读取的 `MainWindow` 私有成员 **15**，
> friend 授权 **5** 个 QML 类型。
>
> 计数按**去重后的名字**算，不是调用点数。`QmlEditorPageHost` 那两处私有成员换成公有访问器后，
> 方法数没有变——`documentActiveDifficultyId` 和 `qmlExportSession` 本来就已经被别的文件用到、
> 已经在清单里了。这正是想要的：耦合从「没有接口」降级成「已有的窄接口」，总面积不增。

## 为什么私有成员和 friend 单独记

`MainWindow.h` 把五个 QML 类型声明为 `friend`，它们因此绕过公有接口直接读写窗口的私有成员
（`document_`、`previewSkinDirectoryName_`、`exportSection_` 等 17 个）。这与第 2 项要求的
「QML 通过**窄 QObject 门面**访问 services / sessions」正相反：friend 不是窄接口，是没有接口。
所以清零顺序是——**先消除私有成员读取和 friend 授权，再削减公有方法**，否则把公有方法搬走
只会让剩下的耦合更隐蔽。

## friend 授权（必须清零）

| friend | 当前用途 | 去处 |
| --- | --- | --- |
| `QmlCommandService` | 命令总线调用窗口私有槽 | 命令改由 service/session 承接 |
| `QmlEditorPageHost` | `activeDifficultyId_`、`qmlExportSession_` | 页面路由改由 QML 宿主拥有 |
| `QmlExportSession` | 15 个预览/导出私有成员 | `ExportSession` + `PreviewSession` |
| `QmlPreviewModel` | 预览运行时私有状态 | `PreviewSession` |
| `miacode::qml_ui::QmlPreviewSettingsModel` | 5 个预览外观私有成员 | `PreviewSession` |

> `miacode::latency::LatencySandboxController` 也是 friend，但它是 widget 侧组件，不属于本清册；
> 它随阶段 4 的 `MainWindow` 一并处理。

## 清单

### 预览（→ `PreviewSession`）

预览运行时、播放传输、皮肤/判定外观、音频设置。`QmlPreviewModel` 与 `QmlPreviewSettingsModel` 还是 `MainWindow` 的 friend。

**`src/app/qml_ui/QmlPreviewModel.cpp`** — 方法 21，私有成员 0

- `beginShellPreviewScrub`
- `endShellPreviewScrub`
- `muriRenderMode`
- `nudgeShellPreviewRate`
- `resolvePreviewSkinDir`
- `seekShellPreview`
- `setMuriRenderMode`
- `setShellPreviewRate`
- `shellPreviewCanvasAspectRatio`
- `shellPreviewDurationSeconds`
- `shellPreviewLowerBoundSeconds`
- `shellPreviewPlaying`
- `shellPreviewPositionSeconds`
- `shellPreviewRuntimeObject`
- `shellPreviewSpeedLabel`
- `shellPreviewStageMediaHostObject`
- `shellPreviewStatsTexts`
- `stopShellPreview`
- `toggleShellMuriRenderMode`
- `toggleShellPreviewPlayback`
- `updateShellPreviewScrub`

**`src/app/qml_ui/preview/QmlAudioSettingsModel.cpp`** — 方法 5，私有成员 0

- `applyPreviewAudioSettingsFromUi`
- `currentPreviewAudioSettings`
- `restorePreviewAudioSettingsFromSoftwareDefault`
- `savePreviewAudioSettingsAsSoftwareDefault`
- `shellPreviewPlaying`

**`src/app/qml_ui/preview/QmlPreviewSettingsModel.cpp`** — 方法 9，私有成员 5

- `applyPreviewOutlineVariant`
- `applyPreviewSkinDirectoryToSurfaces`
- `availablePreviewSkinDirectoryNames`
- `previewRenderSettings`
- `previewSkinDisplayName`
- `resolvePreviewCustomOutlineDir`
- `resolvePreviewSkinRootDir`
- `savePortableState`
- `setPreviewRenderSetting`
- `previewCanvas_` *(私有成员)*
- `previewJudgeEffectStyle_` *(私有成员)*
- `previewOutlineVariant_` *(私有成员)*
- `previewSkinDirectoryName_` *(私有成员)*
- `previewSkinVariant_` *(私有成员)*

### 时间轴与分析（→ `TimelineSession`）

走带导航、底栏页签可见性、拖拽状态、Muri 提示偏好。

**`src/app/qml_ui/QmlTimelineModel.cpp`** — 方法 15，私有成员 0

- `centerShellTimelineNavigate`
- `navigateShellTimelineToSecond`
- `setShellBottomTabsCurrentTab`
- `shellBottomTabsCurrentTabId`
- `shellBottomTabsVisible`
- `shellMuriTabVisible`
- `shellTimelineDragFinished`
- `shellTimelineDragStarted`
- `shellTimelineFollowPreviewToggled`
- `shellTimelineStateBridgeObject`
- `shellTimelineSurfaceReady`
- `shellTimelineTabVisible`
- `shellTimelineUserInteractionStarted`
- `shellValidationTabVisible`
- `wheelShellTimelineNavigate`

**`src/app/qml_ui/QmlAnalysisModel.cpp`** — 方法 2，私有成员 0

- `ignoreMuriIssuePrompts`
- `navigateShellTimelineToSecond`

### 导出与页面切换（→ `ExportSession`）

这是耦合最深的一块：`QmlExportSession` 直接读 `MainWindow` 的 15 个私有成员，页面切换仍走隐藏 `DocumentSection` 的 `switchTo*Field`。

**`src/app/qml_ui/export/QmlExportSession.cpp`** — 方法 9，私有成员 15

- `applyPreviewOutlineVariant`
- `applyPreviewSkinDirectoryToSurfaces`
- `availablePreviewSkinDirectoryNames`
- `currentPreviewAuthoritativeAudioClockSecond`
- `previewSkinDisplayName`
- `refreshExportIntroState`
- `resolvePreviewCustomOutlineDir`
- `resolvePreviewSkinRootDir`
- `savePortableState`
- `document_` *(私有成员)*
- `exportSection_` *(私有成员)*
- `muriRenderOptions_` *(私有成员)*
- `previewAudioSettings_` *(私有成员)*
- `previewCanvas_` *(私有成员)*
- `previewCenterDisplayMode_` *(私有成员)*
- `previewIntroSoundFileName_` *(私有成员)*
- `previewJudgeEffectStyle_` *(私有成员)*
- `previewOutlineVariant_` *(私有成员)*
- `previewSfxRuntime_` *(私有成员)*
- `previewSkinDirectoryName_` *(私有成员)*
- `previewSkinVariant_` *(私有成员)*
- `previewSlideEarlierSecondAndTextOnTop_` *(私有成员)*
- `previewTapJudgeTextDistance_` *(私有成员)*
- `projectLastOpenedDifficultyId_` *(私有成员)*

**`src/app/qml_ui/QmlEditorPageHost.cpp`** — 方法 8，私有成员 0

- `documentActiveDifficultyId`
- `hasActiveDifficulty`
- `onPackAsZip`
- `qmlExportSession`
- `switchToDifficultyField`
- `switchToExportField`
- `switchToLatencyField`
- `switchToMetadataField`

> 2026-09-01：两处私有成员读取（`activeDifficultyId_`、`qmlExportSession_`）已换成等价的公有
> 访问器——`documentActiveDifficultyId()` 就是返回该成员，`qmlExportSession()` 本来就是头文件里
> 写明的「唯一导出会话的只读交接口」。`friend class QmlEditorPageHost` **暂时保留**：四个
> `switchTo*Field` 声明在 `MainWindowPrivateMethodsA.inc` 里，把它们改成公有等于把接口做宽而不是
> 做窄。它们随阶段 3.5 第 3 项（页面路由归 QML 宿主）一起消失，friend 授权届时才能删。

### 文档（→ `ChartWorkspace` / `DocumentService`）

文档真相已在 `ChartWorkspace`，但初始装载、最近文件、备份与编辑器导航仍经窗口。

**`src/app/qml_ui/QmlDocumentModel.cpp`** — 方法 14，私有成员 0

- `applyCommittedQmlDocument`
- `backupDocumentEntries`
- `chartNormalizeOptions`
- `documentActiveDifficultyId`
- `documentFilePath`
- `documentSourceText`
- `noteRecentDocument`
- `recentDocumentEntries`
- `requestEditorNavigation`
- `restoreBackupDocument`
- `setChartNormalizeOptions`
- `setQmlChartTextHandler`
- `setQmlDocumentSaveHandler`
- `setQmlLeaveDocumentHandler`

### 偏好设置（→ `PreferencesService`）

这些 setter 不是纯设置：它们经 `editorSection_` / `timelineSection_` 把变化应用到隐藏 widget 布局和预览，拆分要连同应用侧一起搬。

**`src/app/qml_ui/preferences/QmlPreferencesModel.cpp`** — 方法 21，私有成员 0

- `applyEditorAutoCompletionEnabled`
- `applyEditorHalfWidthInputEnabled`
- `applyEditorImeInputDisabled`
- `applyEditorLineSpacingFactor`
- `applyEditorTextFontSize`
- `currentEditorAutoCompletionEnabled`
- `currentEditorHalfWidthInputEnabled`
- `currentEditorImeInputDisabled`
- `currentEditorLineSpacingFactor`
- `currentEditorTextFontSize`
- `currentPreviewCanvasFrameRateMode`
- `currentPreviewCanvasRefreshRate`
- `currentPreviewStageMediaFrameRateMode`
- `currentTimelineFrameRateMode`
- `currentVideoDecodePrefersSoftware`
- `currentWorkspacePanelsSwapped`
- `setPreviewCanvasFrameRateMode`
- `setPreviewStageMediaFrameRateMode`
- `setTimelineFrameRateMode`
- `setVideoDecodePrefersSoftware`
- `setWorkspacePanelsSwapped`

**`src/app/qml_ui/QmlApplicationContext.cpp`** — 方法 3，私有成员 0

- `currentEditorLineSpacingFactor`
- `currentEditorTextFontSize`
- `qmlExportSession`

### 延迟检测（→ `LatencyService`）

读侧（bpm/offset/clock）现在可以直接读 `ChartWorkspace`；写侧仍经 `timelineSection_`。

**`src/app/qml_ui/latency/QmlLatencyModel.cpp`** — 方法 8，私有成员 0

- `applyLatencyDetectorBpm`
- `applyLatencyDetectorClockCount`
- `applyLatencyDetectorOffset`
- `latencyDocumentClockCount`
- `latencyDocumentOffsetSeconds`
- `latencyDocumentWholeBpm`
- `latencySandboxController`
- `latencyTrackPath`

### 媒体工具（→ `MediaToolsService`）

ffmpeg 单文件流程的入口；UI 请求与作业进度已改走装配对象。

**`src/app/qml_ui/media/QmlMediaToolsModel.cpp`** — 方法 6，私有成员 0

- `applyMediaBlank`
- `detectMediaBlankTiming`
- `onCompressBackgroundVideo`
- `onConvertTrackTo44100Hz`
- `prependMediaBlankContext`
- `restoreMediaBlankBackup`

### 外壳宿主（→ QML 宿主自身，阶段 3.5 第 3 项）

根窗口、拖放、关闭与偏好设置入口。这一组消失就等于隐藏 `MainWindow` 消失。

**`src/app/qml_ui/QmlUiBootstrap.cpp`** — 方法 9，私有成员 0

- `handleAudioDrop`
- `hide`
- `preparePreviewForShutdown`
- `releaseChartDropImportService`
- `setQuickShellBackendActive`
- `setQuickShellRootWindow`
- `setVisible`
- `shellNoteQuickUiReady`
- `shellSetRootWindowFrameGeometry`

**`src/app/qml_ui/QmlShellLifecycle.cpp`** — 方法 1，私有成员 0

- `requestShellClose`

**`src/app/qml_ui/QmlCommandService.cpp`** — 方法 2，私有成员 0

- `onPreferences`
- `requestLeaveDocument`

## 更新规则

- 从 QML 层搬走一个名字 → **同一次提交**删掉本文对应行。守卫会因为「清单里有、代码里没有」而失败，
  这正是逼你更新计数的机制。
- 新增一个名字 → 必须显式加行并说明去处。没有「先用着以后再说」的路径：守卫直接拒绝。
- 全部清零时，`QmlApplicationContext` 就可以去掉 `MainWindow& backend_`，阶段 3.5 第 2 项完成；
  `QmlUiBootstrap` 不再需要构造隐藏窗口，第 3 项随之完成。
