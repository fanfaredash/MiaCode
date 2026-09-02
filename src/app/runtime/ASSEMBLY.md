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
| `PlaybackCoordinator`（4.8 播放协调器） | `PlaybackControl` + `PreviewPlaybackPort` + `AudioClockSource` | `src/app/runtime/playback/` |
| `PlaybackPreviewSurfaceAdapter` | `PreviewSurface` 兼容投影 | `src/app/runtime/playback/` |
| `PlaybackTimelineSurfaceAdapter` | `TimelineSurface` 兼容投影 | `src/app/runtime/playback/` |
| `TimelineHost`（4.6 过渡宿主） | `TimelineSurface` | `src/app/runtime/timeline/` |
| `PreviewHost`（4.7 过渡宿主） | `PreviewSurface` | `src/app/runtime/preview/` |
| `VideoExportHost` | `ExportEngine` | `src/app/runtime/export/` |

4.5 已建立独立的播放契约：`PlaybackControl` / `PlaybackStateFeed` / `PlaybackSnapshot` 位于
`src/app/v2/PlaybackControl.h`。4.8 起 `PlaybackCoordinator` 直接实现播放控制、Preview transport
port 与 canonical audio clock；`PlaybackPreviewSurfaceAdapter` / `PlaybackTimelineSurfaceAdapter`
只负责把旧 surface 槽位转接到协调器，不持有第二套状态。新的 Preview/Timeline 代码应依赖 playback
contract，而不是继续扩展复合宿主。

4.6 已将 `ApplicationServices::timelineSurface` 切换为 `TimelineHost`。QML ingress 先捕获
`TimelineCommandStamp`，host 在转发前校验原始 generation/revision/sequence；4.8 的
`PlaybackTimelineSurfaceAdapter` 是其下游兼容 surface，协调器不再实现 `TimelineSurface`。

4.7 已将 `ApplicationServices::previewSurface` 切换为 `PreviewHost`。PreviewHost 的 transport
命令只经过 `PreviewPlaybackPort`，canonical position 只读 `AudioClockSource`；4.8 起这两个端口
由 `PlaybackCoordinator` 直接提供，非 transport Preview 能力经 `PlaybackPreviewSurfaceAdapter`
转发。
| `LatencySandboxController` | `LatencyEngine` | `src/tools/latency/` |
| `MediaJobsHost` | `MediaToolsEngine` | `src/app/runtime/media/` |
| `SettingsHost` | `PreferencesStore` | `src/app/runtime/settings/` |
| `DocumentSessionHost` | `DocumentBridge` + `EditorPageRouter` | `src/app/runtime/document/` |
| `ShellHost` | 根窗口附着、关闭、快捷键、几何 | `src/app/runtime/shell/` |
| `StageMediaHost` | 舞台媒体与预热 | `src/app/runtime/preview/` |
| `EditorHost` | 便携状态、书签、字体持久化 | `src/app/runtime/editor/` |
| `ValidationHost` | 校验与无理列表 | `src/app/runtime/validation/` |

`PlaybackCoordinator` 不再直接填入预览与时间线 surface 槽位：两个无状态 adapter 分别填入
`PreviewHost` / `TimelineHost` 的下游兼容入口，三者共享同一个协调器实例。协调器的实现体仍暂时调用
显式的 `RuntimeContext::Ui` / `RuntimeContext::State` 过渡期共享记录，这是 4.9 的后续存储清理范围；
它不改变单一播放权威。`DocumentSessionHost`
同时填入文档桥与页面路由，因为页面切换与未保存守卫同属文档会话。

`ApplicationServices` 只装配，不实现上述接口。

入口：`QmlUiBootstrap` / CLI 导出构造 `ApplicationServices` + `Session`。`Session` 拥有宿主并安装槽位。QML 根窗口由 QML 引擎创建，与 `Session` 无父子关系。

## Preview / Timeline 二次拆分（计划）

当前的 `PlaybackCoordinator` 约 8,975 行，混合了 transport、canonical playhead、frame pacing、Timeline
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

4.8 已完成契约边界收缩和旧类型重命名：`PlaybackCoordinator.h` 不直接包含或继承
`PreviewSurface.h`、`TimelineSurface.h`、QML 或场景图契约。4.9a 又将其头文件对共享存储的依赖
收缩为 `RuntimeContext.h`，不再包含 `Session.h`；`RuntimeContext::Ui` / `RuntimeContext::State`
仍是 4.9 的过渡期共享记录，后续按宿主拆分其所有权。4.9b 已切出第一个域记录
`RuntimeContext::TimelineState`：时间线存储由它拥有，`State` 只保留兼容引用，
借用方尚未改为直接持有该窄记录。协调器还暂时公开旧 Preview/Timeline
投影方法供 adapter 和 Session 内部转发；两个 surface adapter 仍是迁移期兼容层，协调器的旧实现
文件还保留部分 preview/timeline 业务调用，不能把本阶段误记为全部业务实现已搬空。

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
- [x] 阶段 4.7：建立 `PreviewHost` / `PreviewPlaybackPort` / `AudioClockSource`，将 Preview
  transport 与 canonical clock 从 legacy surface 隔离，并完成单一 authority / inert 生命周期测试
- [x] 阶段 4.8：将旧 `PlaybackHost` 重命名为 `PlaybackCoordinator`，直接实现
      `PlaybackControl` / `PreviewPlaybackPort` / `AudioClockSource`；Preview/Timeline 旧槽位改由
      两个无状态 surface adapter 承接，删除 `PlaybackControlAdapter`，并通过边界 spec 与 Release
      构建验证
- [x] 阶段 4.9a：将共享 UI/state 类型外置为 `RuntimeContext::Ui` / `RuntimeContext::State`，
      宿主构造签名改用显式上下文类型，`PlaybackCoordinator.h` 脱离 `Session.h`
- [ ] 阶段 4.9b：按 PlaybackCoordinator / PreviewHost / TimelineHost 及其他运行时宿主切开
      `RuntimeContext` 共享记录，Session 只保留生命周期、资源所有权和端口连接
  - [x] 第一片时间线存储（2026-09-02）：时间线刷新、快照、就绪、游标与分析调度的 29 个字段
        移入 `RuntimeContext::TimelineState`；`RuntimeContext::State` 只保留迁移期兼容引用，
        自身不再拥有这些值，`State` 与 `RuntimeContext` 均禁用拷贝（拷贝出的 `State`
        会继续别名源记录的存储）。`runtime_context_boundary_spec` 是独立编译 TU，
        逐字段 `static_assert`「State 只借不拥、TimelineState 拥有不借」；成员声明顺序
        由 `playback_coordinator_spec` 的文本检查守住，因为编译期断言看不到顺序。
  - [x] 结论修正（2026-09-02，两份盘点之后）：**「按宿主切存储」这条路只对 timeline 成立，
        不要复制到其他域**。盘点显示 `State` 剩余 338 个自有字段里，164 独占 / **165 跨域** /
        9 零引用；timeline 之所以能干净切出，是因为它是唯一自包含的域（切完后 `timeline`
        目录触及的剩余 State 字段为 0）。而 `playing_` 有 12 个域读写、`pauseSecond_` 8 个、
        `previewPlaybackRate_` 9 个、`scene_` 9 个——把它们塞进某个宿主的存储袋只是换个地方共享。
        这些恰好是 canonical 播放状态，正确动作是让它们**不再是字段**：收进协调器私有成员、
        其余人改读 4.5 已建好的 `PlaybackSnapshot`。因此剩余工作重排为 4.9c–4.9f。
- [x] 阶段 4.9c（2026-09-02）：删死存储——`State` 9 个 + `Ui` 47 个零引用字段（`src/` 中除 `.inc`
      自身外无任何访问），含 7 个从未创建过的重型控件/布局。连带删掉 selection-transform undo 死簇：
      存储字段没了之后，`DocumentSessionHost.h` 里操作它的 5 个方法全部只有声明——无实现、
      无调用点——连同 `Session.h` 的 `using` 别名与 `RuntimeContext::SelectionTransformUndoEntry`
      一并清除。净删 147 行。Release 全量构建通过，全量 CTest 101 项 100 通过
      （唯一红为既有 `qtavplayer_platform_spec`）。
- [ ] 阶段 4.9d：**切断 `PlaybackCoordinator` 对 `Session&` 的依赖**（本主线的关键路径）。
      现状：构造签名要 `Session&`，实现体有 204 处 `session_.` 调用、73 个不同成员。
      这直接导致没有任何测试能构造协调器——门槛第 1 项（fake-clock 七种转换）与第 3 项
      （三宿主装配与生命周期）为空，根因是同一个。

      **归口分类（2026-09-02 事实盘点后，推翻了此前"73 个方法各自归口"的设想）**：
      决定性事实是 `PlaybackCoordinator.h:348-350` —— 协调器**已经持有与 `Session` 同一个
      `RuntimeContext::Ui&` / `State&`**。因此相当一部分 `session_.` 调用只是绕路去读写
      协调器自己手里就有引用的字段，属于冗余而非耦合。

      | 类 | 成员 | 调用 | 处置 |
      |---|---|---|---|
      | A 自指绕路 | 3 | 5 | Session 侧实现体就是 `playback_->同名方法()`，绕回协调器自己；改直调 |
      | B 死代码/纯诊断 | 4 | 14 | `noteStatus` 函数体为空却被调 10 次；`updateEditorHeaderLayoutMode` 同样为空；`setProperty` 写的动态属性全仓无人读；`findChildren` 仅诊断计数 |
      | C 已持有同一存储 | 10 | 22 | 函数体即 `return state_.xxx;`；改直读。其中 `setPreviewPlayingFlag`（6）含异步 `emit presentationChanged`，须留到端口步，不可直读替代 |
      | D Widgets 补妆 | 8 | 33 | 纯 QWidget/QLayout 操作，**且 widget 指针全部来自协调器已持有的 `ui_`**；只有"刷新布局"这个动作绕道 Session |
      | E 需要真端口 | 46 | 112 | 收敛为 5 个端口，见下 |
      | F 时钟 | 1 | 14 | `currentPreviewAuthoritativeAudioClockSecond`，见下 |
      | G 平台条件 | 1 | 4 | `setPreviewFixedTimerHighResolutionActive` 全 body 在 `#ifdef Q_OS_WIN` 内；macOS 上为空操作但 Windows 有真行为，**不是死代码** |

      **F 类不是正确性问题**：曾怀疑协调器自身的 `AudioClockSource` 与 Session 侧方法是两份真相，
      经查是"源与下游采样"——Session 侧按墙钟实时外推（数据全部来自协调器同样持有的
      `state_.qtPreviewElapsed_` / `qtPreviewStartSecond_` / `previewPlaybackRate_`），
      而 `AudioClockSource` 返回的 `state_.pauseSecond_` 正是 `Tick.cpp:145` 调用前者后写入的。
      一份真相两条路径，不是权威冲突。但**canonical 时钟的计算体住在 `Session` 而非号称播放权威的
      协调器里**，且该函数只读协调器已有的 `state_` 字段——搬进协调器是纯移动，不需要重新设计。

      **D 类不与 Widgets 移除合并**：函数体只碰 `ui_`、不碰任何 Session 状态，所以搬进协调器即可，
      不需要造用完即弃的兼容端口，也不必把 4.9d 阻塞在整条 Widgets 移除之后；搬完后它们会随
      Widgets 移除自然消失。

      **E 类的 5 个端口**：`StageMediaHost&`（吃掉全部 `*StageMediaRoute*` 家族与 SFX 预热，约 41 处）、
      持久化端口（`savePortableState` 及音频/渲染偏好存取，约 16 处）、`ApplicationServices&`
      （11 处全是 `.workspace().document()`）、`EditorSyncController&`（含
      `clearPreviewFollowDecoration`，11 处）、`ValidationHost&` / `DocumentSessionHost&`（约 11 处）。
      `previewPlayheadChanged` 已有等价契约 `ShellNotifications::previewPlayheadChanged`
      （Session 现在就在转发），改直接发即可，不算新端口。
      `appliedQmlWorkspaceRevision_` 是 `Session` 自己的字段（不是 `state_` 别名），
      是唯一没有现成去处的成员，需单独定归属。

      **执行顺序**（每步之间构建 + 全量 CTest；`session_.` 计数是天然进度指标 204 → 169 → 155 → 122 → 0）：
      1. ~~A+B+C（不含 `setPreviewPlayingFlag`）：纯删除与直读，零设计风险~~
         **已完成 2026-09-02**：`session_.` 计数 204 → **169**（降 35 = A 类 5 + B 类 14 + C 类 16）。
         连带删除第 1 步之后变成全仓无引用的 6 个 `Session` 方法、1 个私有 helper
         （`bottomTabsTabIdString`）、`finishQtPreviewPlaybackAndReturnToEntry` 的死参数与其
         `Session` 转发壳、以及常量 `kQuickShellTransportSeekProperty`——这是解耦第一次开始
         **减少 `Session` 的表面积**，而不只是不再去调它。`qml_export_font_contract_spec` 有一条
         断言原本钉着 `"void Session::refreshPreviewSurfaces()"` 的源码文本，已改为钉协调器侧的
         `PlaybackCoordinator::refreshSurfaces()`，强度不变。
      2. F：canonical 时钟计算搬进协调器
      3. D：Widgets 补妆函数体搬进协调器
      4. E：5 个端口 + `setPreviewPlayingFlag` 的信号路径
      5. 构造签名去掉 `Session&`；门槛 1、3 的测试从此可写

      **完成判据：`PlaybackCoordinator` 能在 spec 里被构造出来。**
- [ ] 阶段 4.9e：canonical 播放状态收进协调器私有成员，跨域读者改读 `PlaybackSnapshot`
      （`playing_` / `pauseSecond_` / `previewPlaybackRate_` / `previewTransportState_` /
      `qtPreview*` 一族）。这是把 4.5 的契约真正落地，「一个播放权威」才算成立。依赖 4.9d 的端口。
- [ ] 阶段 4.9f：补齐门槛测试，依赖 4.9d。四项：协调器 fake-clock 的
      play/pause/resume/stop/seek/scrub/rate；三宿主装配与生命周期；`TimelineCommandGate` 的
      drag-follow **带戳重载**乱序注入（当前只有 `navigateToSecond` 走过乱序）；
      parser → timeline → preview → export 的 revision / chart-time 对齐。
      **同时替掉两处文本扫描**：`playback_coordinator_spec` 的 `verifyRuntimeContextOutlivesHosts`
      与 `verifyTimelineStorageIsConstructedBeforeItsAliases` 现在都是比较成员声明字符串在
      `Session.h` / `RuntimeContext.h` 里的先后位置。当时的理由「编译期断言看不到成员顺序」本身没错，
      但它掩盖了真正的原因：**这些对象根本构造不出来**。4.9d 之后应改为真的构造/析构观察。
      另外协调器的依赖检查漏了门槛原文要求的 "media UI" 一项，且 Timeline↔Preview 互不依赖
      目前没有任何断言，只是两个 spec 的 CMake SOURCES 恰好精简——是副作用不是设计。
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

QML 产品面：语法页 `validationRows` + 编辑器 `syntaxIssues`，无理页 `muriRows`，解析走 `AnalysisService`（`parseForTimeline` + `buildTimelineAnalysisRefreshResult`）。时间线音符走 `PlaybackCoordinator` 读 `ChartWorkspace`。预览走带走 `PreviewSurface`。导出走 `ExportEngine` / `QmlExportSession`。页签走 `EditorPageRouter`。

已修：`QmlAnalysisModel` 曾用 `ignoreMuriIssuePrompts` 清空 `muriRows`。该偏好只表示分析完成时不打断（时间线圆点、自动切页），无理页仍应列出结果。

`ValidationHost` 仍向已删除的 `errorList_` / `muriList_` 写列表，QML 不读这两份控件。`RuntimeContext::Ui`
仍保留大量迁移期空指针；这些不挡当前产品面，但属于后续 Widgets 清理范围。

### 2026-09-01 语法错误点击跳到编辑器第一行

语法行没有播放头时间，投影却把 `second` 写成 `0`。点击先按行列定位编辑器，激活完成后再调用 `navigateToSecond(0)`，时间轴把光标改到 0 秒对应行（通常是正文开头）。现语法行 `second < 0`，完成激活时只对 `pending.second >= 0` 走带。无理行仍带真实秒数。

### 2026-09-02 远程合并后的当前边界

`2aa9db83` 已将 `MainWindow` 宿主迁入 `src/app/runtime/` 并删除 `src/app/mainwindow/`；合并提交
为 `4416596d`。`Session` 现在是 QObject 装配壳，`QmlApplicationContext` 只接收
`ApplicationServices&`。下一步不再继续堆叠 `PlaybackCoordinator`，而是先立 playback contract，再按
`PlaybackCoordinator` / `PreviewHost` / `TimelineHost` 三个职责拆分；GUI 验收不属于本阶段进度判断。
