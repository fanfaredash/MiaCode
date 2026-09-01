# 运行时装配

本文件是本次架构调整的唯一进度源。代码与本文冲突时以代码为准，并在同一改动里更新本文。

## 目标

产品前端：`src/app/qml_ui/`（QML + `Qml*Model`）。

文档后端：`ChartWorkspace`（`ApplicationServices` 持有）。

运行时：`miacode::runtime` 下的独立宿主。装配对象持有宿主并填入 `ApplicationServices` 的槽。不再存在 `QMainWindow` 产品窗口，目录 `src/app/mainwindow/` 已删除。

宿主之间只经过：

- `ChartWorkspace`
- `EditorSyncController`
- `PreviewAppearanceState`
- `UiRequestService` / `JobProgressService`
- `ShellNotifications`

字段名按宿主职责命名。`Session` 只装配宿主、附着 QML 根窗口、转发关闭与欢迎流程。

## 宿主

| 宿主 | 接口 | 代码 |
|---|---|---|
| `PlaybackHost`（当前复合宿主） | `PreviewSurface` + `TimelineSurface` | `src/app/runtime/playback/` |
| `TimelineHost`（4.6 过渡宿主） | `TimelineSurface` | `src/app/runtime/timeline/` |
| `VideoExportHost` | `ExportEngine` | `src/app/runtime/export/` |

4.5 已建立独立的播放契约：`PlaybackControl` / `PlaybackStateFeed` / `PlaybackSnapshot` 位于
`src/app/v2/PlaybackControl.h`，`PlaybackControlAdapter` 以类型化观测和命令转发连接当前复合
`PlaybackHost`。新的 Preview/Timeline 代码应依赖 playback contract，而不是继续扩展复合宿主；
4.6–4.9 再分别抽离 TimelineHost、PreviewHost 和最终的 PlaybackCoordinator。

4.6 已将 `ApplicationServices::timelineSurface` 切换为 `TimelineHost`。QML ingress 先捕获
`TimelineCommandStamp`，host 在转发前校验原始 generation/revision/sequence；当前被包裹的
`PlaybackHost` 仍保留 Timeline 实现，后续阶段再搬出所有权。
| `LatencySandboxController` | `LatencyEngine` | `src/tools/latency/` |
| `MediaJobsHost` | `MediaToolsEngine` | `src/app/runtime/media/` |
| `SettingsHost` | `PreferencesStore` | `src/app/runtime/settings/` |
| `DocumentSessionHost` | `DocumentBridge` + `EditorPageRouter` | `src/app/runtime/document/` |
| `ShellHost` | 根窗口附着、关闭、快捷键、几何 | `src/app/runtime/shell/` |
| `StageMediaHost` | 舞台媒体与预热 | `src/app/runtime/preview/` |
| `EditorHost` | 便携状态、书签、字体持久化 | `src/app/runtime/editor/` |
| `ValidationHost` | 校验与无理列表 | `src/app/runtime/validation/` |

`PlaybackHost` 当前同时填入预览与时间线两个槽，因为走带、播放头与时间线 QSG 仍共用同一套运行时对象；这
是迁移后的暂态，不是最终职责边界。`DocumentSessionHost` 同时填入文档桥与页面路由，因为页面切换与未保存
守卫同属文档会话。

`ApplicationServices` 只装配，不实现上述接口。

入口：`QmlUiBootstrap` / CLI 导出构造 `ApplicationServices` + `Session`。`Session` 拥有宿主并安装槽位。QML 根窗口由 QML 引擎创建，与 `Session` 无父子关系。

## Preview / Timeline 二次拆分（计划）

当前的 `PlaybackHost` 约 8,975 行，混合了 transport、canonical playhead、frame pacing、Timeline
QSG / viewport、PreviewRuntime、StageMedia、音频混音、布局和分析状态。后续遵循唯一不变量：
**一个时间域、一个播放权威、两个独立投影**。

目标装配为三个独立实例：

| 目标宿主 | 负责 | 明确不负责 |
|---|---|---|
| `PlaybackCoordinator` | transport 状态机、canonical playhead、clock / frame pacing、seek / scrub 事务、播放生命周期、代次 / revision / sequence、Timeline command gate | QML/QSG、viewport/layout、StageMedia、mixer 实现、analysis 实现 |
| `PreviewHost` | `PreviewRuntime`、StageMedia 路由与预热、渲染设置、音频 mixer / SFX、preview executor、只读音频时钟资源 | canonical playhead、独立 transport、Timeline 状态 |
| `TimelineHost` | `TimelineQuickStateBridge`、QSG readiness、viewport / zoom、drag / follow / navigation 投影、底栏页签、Timeline 侧分析展示 | transport clock、播放权威、直接写文档模型、Preview 资源 |

协调器向两个宿主发布带 `sessionGeneration`、`documentRevision`、`playbackSequence` 和 canonical
chart time 的只读快照。Timeline 发出的命令经过 `TimelineCommandGate` 校验 revision、代次和
写入顺序；Preview 与 Timeline 之间不得直接调用。`EditorSyncController` 继续拥有文档 revision
与 follow 真相，Session 只负责显式构造、端口连接、生命周期和资源所有权。

迁移期间可以保留旧 `PreviewSurface` 的 transport adapter；只有当三个槽位已分别指向
`PlaybackCoordinator`、`PreviewHost`、`TimelineHost`，且契约测试与跨模块时间对齐回归通过后，才
将旧 `PlaybackHost` 更名或删除。

## 进度

- [x] 本文建立
- [x] `Session` 为 `QObject`，不再继承 `QMainWindow`；启动路径不再隐藏一个控件窗口
- [x] 删除剩余控件树（堆栈页、底栏列表、预览面板几何）
- [x] 八个槽位不再指向 `Session`；由对应宿主填入
- [x] 嵌套 `*Section` 解嵌套为 `miacode::runtime::*Host`；目录迁到 `src/app/runtime/`；`src/app/mainwindow/` 删除
- [x] 空菜单 `FrameHost` 删除
- [x] 静态阅读：播放、时间线、导出、延迟、媒体工具、偏好、打开/保存/自动保存、页面切换均接到槽位
- [x] 删除 Session / ShellHost 上已无调用方的走带转发；QML 走槽位
- [x] QML 时间线就绪改走 `TimelineSurface::noteTimelineSurfaceReady`
- [x] 底栏三个页签（时间线 / 语法 / 无理）可见性改走独立布尔，不再询问已删除的 `QTabWidget`
- [x] 产品面自查：语法 / 无理 / 解析、编辑、时间线、预览、导出、页签；无理列表不再被「不打断提示」偏好清空
- [x] 语法列表跳转：语法行 `second` 哨兵为负值，激活完成后不再按 0 秒走带把光标拉回谱面开头
- [x] 阶段 4.5：建立 `PlaybackControl` / stamped `PlaybackSnapshot`，完成兼容 adapter、服务槽位、
  stale callback/session invalidation 边界与 Release 契约测试
- [x] 阶段 4.6：建立 `TimelineHost` / `TimelineCommandGate`，将 QML Timeline ingress 改为带
  generation/revision/sequence 的命令，并完成失效、乱序与 Session 装配测试
- [ ] `PlaybackHost` 仍是 Preview + Timeline 复合宿主；按上面的计划拆分为
      `PlaybackCoordinator`、`PreviewHost`、`TimelineHost`
- [ ] `HostUi` / `HostState` 仍由 `Session` 持有并借给宿主；后续按宿主切开这份存储
- [ ] `QApplication` / `Qt6::Widgets` 仍在入口与 CMake；宿主拆分后再清除 native fallback 与
      Widgets 依赖，避免把 QWidget 生命周期藏进新宿主

## 记录

### 2026-09-01 本文建立

当时代码仍用窗口类实现八个接口。

### 2026-09-01 Session 改为 QObject，控件树停造

`Session : QObject`。QML 根窗口由引擎创建。

### 2026-09-01 宿主落地并迁出 mainwindow

槽位安装在 `SessionBootstrap.cpp`：`videoExport_`、`documents_`、`mediaJobs_`、`latencySandboxController_`、`playback_`、`settings_`。类文件不再带 `MainWindow.` 前缀。CLI 与延迟沙箱 include `runtime/Session.h`。

### 2026-09-01 删走带转发并补时间线就绪

QML 走带已走 `PreviewSurface` / `TimelineSurface`。Session 上对应的走带包装已删除。`QmlTimelineModel::surfaceReady()` 调用 `noteTimelineSurfaceReady()`，否则只会读取就绪标志、不会通知宿主。入口附着改名为 `setBackendActive` / `attachRootWindow` / `setRootWindowFrameGeometry` / `noteRootWindowReady`。推送信号改名为 `presentationChanged` / `previewPlayheadChanged`。

### 2026-09-01 底栏语法页签丢失

控件树删除后，`bottomTabsTabVisible` 对语法 / 无理去问 `QTabWidget`，容器为空则恒为 false；`setChartBottomTabsMode` 写不进状态。QML 语法页签绑了 `validationTabVisible` 因此消失，无理页签未绑可见性因此仍显示。校验命令会切到 `validation` 内容区，界面只剩时间线与无理两个按钮，语法列表看起来像进了无理。语法与无理的数据链本来就是分开的：`validationRows` / `syntaxIssues` 对 `snapshot.validation`，`muriRows` 对 `snapshot.muri`。现改为三个独立布尔；无理页签同样绑 `muriTabVisible`。

### 2026-09-01 产品面自查

QML 产品面：语法页 `validationRows` + 编辑器 `syntaxIssues`，无理页 `muriRows`，解析走 `AnalysisService`（`parseForTimeline` + `buildTimelineAnalysisRefreshResult`）。时间线音符走 `PlaybackHost` 读 `ChartWorkspace`。预览走带走 `PreviewSurface`。导出走 `ExportEngine` / `QmlExportSession`。页签走 `EditorPageRouter`。

已修：`QmlAnalysisModel` 曾用 `ignoreMuriIssuePrompts` 清空 `muriRows`。该偏好只表示分析完成时不打断（时间线圆点、自动切页），无理页仍应列出结果。

`ValidationHost` 仍向已删除的 `errorList_` / `muriList_` 写列表，QML 不读这两份控件。`HostUi` 大量空指针仍在。这些不挡当前产品面。

### 2026-09-01 语法错误点击跳到编辑器第一行

语法行没有播放头时间，投影却把 `second` 写成 `0`。点击先按行列定位编辑器，激活完成后再调用 `navigateToSecond(0)`，时间轴把光标改到 0 秒对应行（通常是正文开头）。现语法行 `second < 0`，完成激活时只对 `pending.second >= 0` 走带。无理行仍带真实秒数。

### 2026-09-02 远程合并后的当前边界

`2aa9db83` 已将 `MainWindow` 宿主迁入 `src/app/runtime/` 并删除 `src/app/mainwindow/`；合并提交
为 `4416596d`。`Session` 现在是 QObject 装配壳，`QmlApplicationContext` 只接收
`ApplicationServices&`。下一步不再继续堆叠 `PlaybackHost`，而是先立 playback contract，再按
`PlaybackCoordinator` / `PreviewHost` / `TimelineHost` 三个职责拆分；GUI 验收不属于本阶段进度判断。
