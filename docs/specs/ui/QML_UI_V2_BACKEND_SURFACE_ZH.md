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
> **计数（2026-09-01）**：方法 **40**，直接读取的 `MainWindow` 私有成员 **0**，
> friend 授权 **0** 个 QML 类型。
>
> 计数按**去重后的名字**算，不是调用点数。方法数在两次削减后都停在 120，这是想要的结果而不是
> 没进展：耦合从「没有接口」降级成窄接口时，用到的名字要么本来就在清单里，要么一进一出。
> 详见「已完成的削减」。

## 为什么私有成员和 friend 单独记

`MainWindow.h` 把五个 QML 类型声明为 `friend`，它们因此绕过公有接口直接读写窗口的私有成员
（`document_`、`previewSkinDirectoryName_`、`exportSection_` 等 17 个）。这与第 2 项要求的
「QML 通过**窄 QObject 门面**访问 services / sessions」正相反：friend 不是窄接口，是没有接口。
所以清零顺序是——**先消除私有成员读取和 friend 授权，再削减公有方法**，否则把公有方法搬走
只会让剩下的耦合更隐蔽。

## 已完成的削减

| 日期 | 动作 | 方法 | 私有成员 |
| --- | --- | --- | --- |
| 2026-09-01 | 起点 | 120 | 17 |
| 2026-09-01 | `QmlEditorPageHost` 的两处私有成员换成等价公有访问器 | 120 | 15 |
| 2026-09-01 | 预览外观八个值搬进 `miacode::v2::PreviewAppearanceState` | 120 | 4 |
| 2026-09-01 | 剩下三个可直接换掉的私有成员改走窄访问器 | 124 | 1 |
| 2026-09-01 | 公开 8 个 QML 页面本就在调用的方法，删掉 3 个 friend 授权（5 → 2） | 124 | 1 |
| 2026-09-01 | 导出引擎改由 `miacode::v2::ExportEngine` 接口承接，`QmlExportSession` friend 授权删除（2 → 1） | 124 | **0** |
| 2026-09-01 | 页面路由改由 `miacode::v2::EditorPageRouter` 接口承接，最后一条 friend 授权删除（1 → **0**） | **118** | **0** |
| 2026-09-01 | 音视频处理改由 `miacode::v2::MediaToolsEngine` 接口承接 | **112** | 0 |
| 2026-09-01 | 延迟检测改由 `miacode::v2::LatencyEngine` 接口承接，`QmlLatencyModel` **完全不再认识 `MainWindow`** | **104** | 0 |
| 2026-09-01 | 时间轴与底栏页签改由 `miacode::v2::TimelineSurface` 接口承接（含分析页的两处） | **88** | 0 |
| 2026-09-01 | 预览（传输 / 运行时对象 / 皮肤目录 / 渲染设置 / 音频混音）改由 `miacode::v2::PreviewSurface` 接口承接 | **61** | 0 |
| 2026-09-01 | 偏好设置改由 `miacode::v2::PreferencesStore` 接口承接 | **40** | 0 |

第三次削减用 4 个方法名换掉 3 个私有成员：`document_` 的四处读取改走本来就公有的
`documentDifficultyIds()` / `documentDifficultyChartText()`（语义完全等价——`difficultyIds()`
就是那张表的键），`projectLastOpenedDifficultyId_` 和 `muriRenderOptions_` 各加一个窄访问器。
方法数上升是**预期内的**：`friend` 给出的是无边界的访问权，任何后续改动都能顺着它拿到任何东西；
换成具名访问器后，这份耦合第一次是可枚举、可逐条搬走的。现在 `QmlExportSession` 只剩
`exportSection_` 一处无边界访问。

第二次削减去掉了 11 个私有成员，方法数却不变，因为同时**去掉**了
`savePortableState` 和 `applyPreviewSkinDirectoryToSurfaces`（改由窗口响应
`PreviewAppearanceState` 的信号来应用与持久化），又**加上**了两个窄方法
`applyPreviewSfxLevels` / `refreshPreviewSurfaces`（把 `previewCanvas_`、
`previewSfxRuntime_`、`previewAudioSettings_` 这三个私有成员换成一次调用）。

同一次改动还消掉了一处真实缺陷来源：皮肤选择的「比较目录名 → 赋值 → 推导 variant → 应用 → 持久化」
这段逻辑原本有**三份拷贝**（QML 导出页、QML 预览设置页、Widgets 导出设置对话框），三份各自推导
variant，任何一份写错就会出现「目录是 skinDX、variant 还是 Standard」的状态。现在 variant 由
`PreviewAppearanceState::variantForDirectory()` 单点推导，`setSkinDirectory()` 不接受不一致的组合。

## friend 授权（已清零）

**QML 类型对 `MainWindow` 的 friend 授权已全部删除（2026-09-01）**：
`QmlCommandService`、`QmlPreviewModel`、`miacode::qml_ui::QmlPreviewSettingsModel`、
`QmlExportSession`、`QmlEditorPageHost`。

前三个的做法是把它们**本来就在调用**的 8 个方法从私有改为公有，并集中成 `MainWindow.h` 里
一个具名的「QML 页面的有界入口」块：5 个皮肤/判定线目录查询（纯路径解析与目录枚举，
本来就是查询形状）、`applyPreviewOutlineVariant` 与 `setMuriRenderMode`（两个都早就带
`persist` 参数，与既有的偏好设置公有面同形）、`onPreferences`。
**清单计数一个都没动**——这 8 个名字本来就在清单里；变的是访问权从「无边界」变成「这 8 个」。

后两个做法不同，也必须不同——它们要的不是值，是窗口内部的整块能力，加访问器只会把
「页面依赖窗口的内脏」写得更正式：

- `QmlExportSession` 要的 `exportSection_` 是导出引擎。先立接口
  （`miacode::v2::ExportEngine`，七个操作），再让 section 实现它。剩下的
  `currentPreviewAuthoritativeAudioClockSecond` / `refreshExportIntroState`
  是两个查询/通知，按前三个的办法公开。
- `QmlEditorPageHost` 要的四个 `switchTo*Field` 声明在私有 `.inc` 里，而且它们**同时**驱动
  一个只因为窗口还没删掉才存在的隐藏 `QStackedWidget`——公开它们等于把第 3 项要消灭的东西
  写进正式接口。所以同样先立接口（`miacode::v2::EditorPageRouter`，七个操作），
  由 `MainWindow` 实现；`switchTo*Field` 保持私有。等 widget 栈消失时，实现掉一半、
  留下领域那一半，页面宿主一行不动。

> `miacode::latency::LatencySandboxController` 也是 friend，但它是 widget 侧组件，不属于本清册；
> 它随阶段 4 的 `MainWindow` 一并处理。

## 清单

### 预览（→ `PreviewSession`）

已改由 `miacode::v2::PreviewSurface` 承接（2026-09-01）：播放传输、scrub 手势、倍速、
QML 绑定的运行时对象、皮肤/判定线目录、渲染设置、音频混音。外观那八个**值**更早就搬到了
`miacode::v2::PreviewAppearanceState`——这里剩的是需要「正在跑的预览」的部分。

`QmlAudioSettingsModel` 与 `QmlPreviewSettingsModel` 现在**完全不认识 `MainWindow`**。
`QmlPreviewModel` 还留着 `MainWindow&`，但只用于连三个推送信号
（`shellPresentationChanged` / `shellPreviewPlayheadChanged` / `previewSkinDirectoryChanged`），
不再调用任何方法。

**`src/app/qml_ui/QmlPreviewModel.cpp`** — 方法 0，私有成员 0

- *（已清空：本文件不再触达 `MainWindow`）*

**`src/app/qml_ui/preview/QmlAudioSettingsModel.cpp`** — 方法 0，私有成员 0

- *（已清空：本文件不再触达 `MainWindow`）*

**`src/app/qml_ui/preview/QmlPreviewSettingsModel.cpp`** — 方法 0，私有成员 0

- *（已清空：本文件不再触达 `MainWindow`）*

### 时间轴与分析（→ `TimelineSession`）

已改由 `miacode::v2::TimelineSurface` 承接（2026-09-01）：走带导航、拖拽生命周期、
底栏页签可见性与当前页签、无理提示偏好。窗口上那些 `shell*` 前缀是 v1 QuickShell 控制器的
遗留命名，接口没有把这段历史带过来。

`QmlTimelineModel` 还留着 `MainWindow&`，但**只用于连一个信号**（`shellPresentationChanged`
的推送），不再调用任何方法——清单计的是方法，信号是另一种（推送式）耦合，随阶段 4 处理。

**`src/app/qml_ui/QmlTimelineModel.cpp`** — 方法 0，私有成员 0

- *（已清空：本文件不再触达 `MainWindow`）*

**`src/app/qml_ui/QmlAnalysisModel.cpp`** — 方法 0，私有成员 0

- *（已清空：本文件不再触达 `MainWindow`）*

### 导出与页面切换（→ `ExportSession`）

**导出引擎的接缝已经立起来了（2026-09-01）**：`QmlExportSession` 不再认识
`MainWindow::ExportSection`，它依赖 `miacode::v2::ExportEngine` 这个七个操作的接口。
实现仍然是窗口里的那 3,600 行 section，它把自己装进装配对象的槽位；等实现真的搬出窗口时，
**只有实现侧要改**，页面一行不动。`QmlExportSession` 的 `friend` 授权随之删除。

**页面路由的接缝同日立起**：`QmlEditorPageHost` 依赖 `miacode::v2::EditorPageRouter`，
不再认识 `switchTo*Field`——那四个入口保持私有，因为它们**同时**驱动一个只因为窗口还没删掉
才存在的隐藏 `QStackedWidget`，公开它们等于把第 3 项要消灭的东西写进正式接口。
等 widget 栈消失时，实现掉一半、留下领域那一半（保存守卫、播放头保留、难度复位、
底栏页签模式、校验装饰、窗口标题），页面宿主一行不动。

**下一步的已知陷阱（`document_` 读取已改走公有访问器，但源头问题仍在）**：
`documentDifficultyIds()` / `documentDifficultyChartText()` 读的仍是 `MainWindow` 的文档副本，
不是 `ChartWorkspace`。看起来应该顺手改成读工作区，但**不要盲改**：
`MainWindow` → 工作区这个方向是**延迟**同步的（`documentReplaced` 经
`QMetaObject::invokeMethod` 排队后才 `adoptBackendDocumentReplacement()`），所以文档被后端替换
（启动、根窗口拖放、原生「打开」、崩溃恢复）之后存在一个窗口期：`MainWindow` 已是新谱面，
工作区还是旧的。此时改读工作区会让导出页列出**旧的**难度列表。要动这一处，得先把那条延迟同步
处理掉，或者确认导出页不会在该窗口期内重建列表。

**`src/app/qml_ui/export/QmlExportSession.cpp`** — 方法 13，私有成员 0

- `applyPreviewOutlineVariant`
- `applyPreviewSfxLevels`
- `availablePreviewSkinDirectoryNames`
- `currentPreviewAuthoritativeAudioClockSecond`
- `documentDifficultyChartText`
- `documentDifficultyIds`
- `muriRenderOptions`
- `previewSkinDisplayName`
- `projectLastOpenedDifficultyId`
- `refreshExportIntroState`
- `refreshPreviewSurfaces`
- `resolvePreviewCustomOutlineDir`
- `resolvePreviewSkinRootDir`

**`src/app/qml_ui/QmlEditorPageHost.cpp`** — 方法 1，私有成员 0

- `qmlExportSession`

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

已改由 `miacode::v2::PreferencesStore` 承接（2026-09-01）。这些 setter 确实不是纯设置——
它们同时把变化**应用**出去（字号与行距重排文本视图、三个帧率模式重新定速、交换工作区面板
重排布局），所以留在窗口实现的接口后面，而不是变成一个值对象。
每个 setter 都带 `persist`：同一批入口既服务用户编辑（要写盘）也服务从盘恢复（不能写盘），
去掉这个参数会让「读设置」变成「重写设置」。
`QmlPreferencesModel` 现在**完全不认识 `MainWindow`**。

**`src/app/qml_ui/preferences/QmlPreferencesModel.cpp`** — 方法 0，私有成员 0

- *（已清空：本文件不再触达 `MainWindow`）*

**`src/app/qml_ui/QmlApplicationContext.cpp`** — 方法 1，私有成员 0

- `qmlExportSession`

### 延迟检测（→ `LatencyService`）

已改由 `miacode::v2::LatencyEngine` 承接（2026-09-01）。`QmlLatencyModel` 是**第一个完全不认识
`MainWindow` 的 Qml*Model**——连构造参数都没有了。

读侧（bpm/offset/clock）刻意留在接口上而不是直接读 `ChartWorkspace`：延迟页没有活动难度，
所以「谱面的 bpm」意思是「`&wholebpm`，否则任意非空难度的第一个内联 `(BPM)`」——
这条解析规则属于页面的所有者，不属于工作区。

**`src/app/qml_ui/latency/QmlLatencyModel.cpp`** — 方法 0，私有成员 0

- *（已清空：本文件不再触达 `MainWindow`）*

### 媒体工具（→ `MediaToolsService`）

ffmpeg 单文件流程的入口。已改由 `miacode::v2::MediaToolsEngine` 承接（2026-09-01）；
PV 批量队列本来就归模型自己（唯一有跨次开启状态的部分）。UI 请求与作业进度走装配对象。

**`src/app/qml_ui/media/QmlMediaToolsModel.cpp`** — 方法 0，私有成员 0

- *（已清空：本文件不再触达 `MainWindow`）*


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
