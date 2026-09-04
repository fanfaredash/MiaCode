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
      2. ~~F：canonical 时钟计算搬进协调器~~
         **已完成 2026-09-02**：`session_.` 计数 169 → **155**（14 处）。
         `PlaybackCoordinator::authoritativeAudioClockSecond()` 承接墙钟外推计算体，
         `Session::currentPreviewAuthoritativeAudioClockSecond()` 保留为转发壳——它在
         `playback/` 之外**还有 8 个调用者**（延迟校准 2、启动装配 5、文档页 3、视频导出 1），
         不能删。函数体确认只读 `state_`，纯移动无语义改变。
         定义处留了一段注释，说明它与 `AudioClockSource::currentAudioClockSecond()` 是
         「源」与「下游采样」而非同一个数，并写明**不许合并、不许其一转调另一**。
         **4.9e 的一个真问题**：那 8 个外部调用者要的是外推值，而 `AudioClockSource` 给的是
         采样值，所以它们不能机械改成走契约——`AudioClockSource` 建好了却被所有潜在消费者绕开，
         契约本身可能需要同时暴露两种读法。
      3. ~~D：Widgets 补妆函数体搬进协调器~~
         **已完成 2026-09-02**：`session_.` 计数 155 → **125**。8 项中 7 项函数体逐字搬入。
         `refreshQuickShellRehostedWidgetParent`（14 处）做成 `LayoutUi.cpp` 匿名命名空间里的
         自由函数——它不依赖任何成员状态，调用点又全在这一个文件里，不必给协调器公共接口增负担。
         `updateEditorFindBarGeometry` / `applyFindOverlayInset` 只搬函数体；在 2026-09-04 的
         QML shell 源集收口中，`ShellHost` 侧原方法随废弃原生 shell 一并移除。连带删除因此无人调用的
         `Session::clearValidationErrors` 转发壳。

         **裁决一：`setCurrentBottomTabsTabId` 移出 D 类，归入 E。** 它不是 Widgets 补妆——
         函数体除 `ui_`/`state_` 外还调用兄弟宿主 `validation_->flushPendingMuriDiagnosticsPanelRefresh()`
         （另有一句 `playback_->flushDeferredTimelineBridgeState()` 确属自指绕路）。
         把它搬进协调器会让协调器穿过 `Session` 去调另一个宿主，正是本阶段要消除的耦合；
         跨宿主编排本就是 `Session` 保留的正当职责。改为在 E 步走窄端口。

         **裁决二：搬入的 `updatePauseButtonAppearance` 体内保留了
         `emit session_.presentationChanged()`。** 这不是新增耦合——原宿主
         `DocumentSessionHost` 里就是这么写的，且 `PlaybackCoordinator` 早已是 `Session` 的
         friend（`Session.h:157-158`），属平移而非回退。但它和 `setPreviewPlayingFlag` 是同一类
         问题，**E 步必须把这两条信号路径一并改走 `ShellNotifications`**（已有等价契约）。
         历史计数里那 2 处新增的 `session_.` 分别来自该信号转发和旧的
         `formatPreviewPlaybackRateToastText`；后者已随隐藏预览速率提示移除，前者仍待后续
         信号路径收口。
      4a. ~~T 类薄转发：不需要端口~~
         **已完成 2026-09-02**：`session_.` 计数 125 → **79**（46 处，125−46=79 精确吻合）。
         一次逐成员实现体判定把剩余 125 处分成 T（薄转发，只碰协调器已持有的 `state_`/`ui_`，
         含对 `state_.previewStageMediaHost_` 这类已持有指针的调用）/ H（需宿主自有成员）/
         S（跨宿主编排）三类：**T 20 个成员 46 处、H 1 个 1 处、S 29 个 64 处、分支歧义 2 个 7 处**。
         T 类按「无其他调用者→搬进协调器 / 有其他调用者且体小→协调器侧等价实现 /
         有其他调用者且体大→提成 `runtime::shared` 自由函数，双方共用」三选一处理，
         避免把 60 行的体复制两份。连带删除因此无引用的 15 个 `Session` 方法。
         `Shared.cpp` 因新增自由函数达 1548 行（超「约 800 行/单一职责」软目标），
         已按仓库 `Base.Suffix.cpp` 惯例拆出 `Shared.Preview.cpp`（400 行），
         `Shared.cpp` 回到 1166 行。
         **本轮第三次遇到文本扫描 spec 钉着具体实现位置**（`PreviewTransportPushSpec` 钉
         `"bool Session::ensureAuditionSceneReady"`，前两次是 `refreshPreviewSurfaces`），
         每次搬家都要跟着改断言——这本身就是 4.9f「替掉文本扫描」的佐证。
      4b-1. ~~注入 `ApplicationServices&`~~ **已完成 2026-09-02**：`session_.` 计数 79 → **49**（30 处）。
         一个注入覆盖三组，因为装配对象同时持有它们：文档工作区（11）、`editorSync()`（11）、
         `shellNotifications()`（8）。`Session::editorSyncController_` 本就只是从装配对象借来的
         缓存指针，不是 Session 自有对象。信号改直接发之前先查证了消费方——全部连在
         `ShellNotifications` 上，无人直连 `Session` 的信号，因此绕过 Session 无感知损失。
         异步 `QueuedConnection` 语义原样保留。
         **`setPreviewPlayingFlag`（6 处）撞上真实不变量**：`preview_transport_push_spec` 要求
         `state_.playing_` 全仓**恰好一个**赋值站点。原计划「协调器直接写」会产生第二个写入口，
         那不是文本 pin 跟随的表面问题，是破坏守卫要保护的不变量。改为按仓库既有做法新增写入原语
         `writePreviewPlayingFlag`——相邻字段 `pauseSecond_` 的 `writePreviewPauseSecond` 就是同一套办法，
         其注释明写「这条不变量是规则、不是计数」。守卫只改期望的文件名，`sites.size() == 1` 未动。
         **守卫的正则同时放宽为匹配成员与自由函数两种写法**：先前钉死 `state_.` 前缀，导致新函数的
         参数被命名成 `state_` 只为迎合正则——那是测试反过来决定命名。正则改宽后该 spec 需排除自身
         （它在注释里写出了两种形式），与 `DebugFlagIndexSpec` 排除自身同一惯例。
      4b-2a. ~~偏好端口~~ **已完成 2026-09-02**：`session_.` 计数 49 → **31**（18 处）。
         **本主线第一次新增抽象**——此前 204 → 49 全靠删冗余、搬函数体、注入已有对象。
         新增 `src/app/v2/PlaybackPreferencesPort.h`（8 个纯虚方法），与其他窄契约同目录。
         **按能力切而非按宿主切**：这 8 个方法的最终归属散在三个宿主
         （`savePortableState` / `loadProjectRenderState` 归 `EditorHost`、`setLastOpenDirectory`
         归 `DocumentSessionHost`、渲染与音频偏好读写留在 `Session`），由 `Session` 实现——
         跨宿主编排正是它保留的正当职责（与 4.9d 第 3 步判 `setCurrentBottomTabsTabId` 同一标准）。
         三个方法因接口可见性要求从 private 改 public，函数体未动。
         **`preferences_port_spec` 是链接层证明而非文本扫描**：只链 `Qt6::Core` + `Qt6::Test`
         （连 `Gui` 都不需要），用 fake 实现该接口；谁往接口里塞进需要窗口或 Widgets 的类型，
         这个 target 就链接失败。这个 fake 也是「协调器能否脱离 `Session` 构造」的第一块拼图。
      4b-2b. ~~校验端口~~ **已完成 2026-09-02**：`session_.` 计数 31 → **25**（6 处）。
         新增 `src/app/v2/PlaybackValidationPort.h`（5 个纯虚方法），由 `ValidationHost` 实现。
         **与偏好端口是刻意的对照**：那个按能力切（8 个方法散在三个宿主，`Session` 实现），
         这个按宿主切（5 个方法今天就只有一个归属）。**不强行统一成一种**——归属散的按能力切、
         归属集中的按宿主切；强行统一会在其中一种情况下造出没必要的转发层。
         接口不带默认实参（阶段 3.5 先例），`ValidationHost` 侧保留自己的默认实参：
         虚函数的默认实参在调用点静态解析、不参与派发，只影响已持有 `ValidationHost&` 的调用方。
         构造顺序已验证：`validation_` 构造于 `SessionBootstrap.cpp:185`、协调器于 `:188`；
         且 `Session.h` 里 `validation_`(689) 声明早于 `playback_`(691)，所以析构时协调器先走，
         不存在借用引用悬空——这正是 4.9a 审计抓到过的方向。
         `validation_port_spec` 同样只链 `Qt6::Core` + `Qt6::Test`。
      4b-2c. ~~文档端口~~ **已完成 2026-09-02**：`session_.` 计数 25 → **19**（6 处）。
         新增 `src/app/v2/PlaybackDocumentPort.h`（4 个纯虚方法），由 `DocumentSessionHost` 实现，
         按宿主切（与校验端口同型）。`DocumentSessionHost::requestEditorNavigation` 现在同时
         override `DocumentBridge` 与本端口的同签名纯虚函数——一份实现服务两个契约，
         文档宿主本就同时面向 QML 桥接与播放端口。
         **`appliedWorkspaceRevision()` 是只读查询，不是把字段搬过来**：
         `appliedQmlWorkspaceRevision_` 语义上属文档域（写于 `DocumentFileFlow.cpp:748`），
         挂在 `Session` 上是早于域拆分的历史；但有两个文本扫描 spec 钉着它作为 Session 成员的
         字面拼写，搬迁属文档域自己的整理，不在 4.9d 范围。端口只加查询，宿主用它本就持有的
         `Session&` 作答。**字段搬迁另记为文档域待办。**
         **本 stage 第 5 次文本扫描 spec 需跟随代码移动**（前四次：`refreshPreviewSurfaces`、
         `Session::refreshPreviewSurfaces`、`ensureAuditionSceneReady`、
         `publishPreviewPlayhead` 的 emit 写法）。这次两条断言钉的是
         `FollowSync.cpp` 里调用点的确切拼写，改端口必然打断它们——**与字段搬没搬无关**。
         判据是不变量有没有变：两条要保护的是「跟随状态携带已提交的工作区 revision」与
         「`FollowSync` 走值投影」，换取值途径两者都没变，所以字面串跟随更新、强度不变。
         执行体在此拒绝了一条捷径——留一行死代码引用来骗过文本扫描，那能同时满足两条矛盾指令
         且全量测试全绿，但属于造假满足 spec。**这个拒绝是对的。**
      4b-2d. ~~预览端口~~ **已完成 2026-09-02**：`session_.` 计数 19 → **3**（16 处）。
         新增 `src/app/v2/PlaybackPreviewPort.h`（9 个纯虚方法），按能力切、由 `Session` 实现
         （8 个归 `StageMediaHost`、`preparePreviewForShutdown` 是 Session 自有编排），与偏好端口同型。
         6 个方法因接口可见性从 private 改 public；`Session` 侧未新增任何薄转发——9 个方法本就都在。
         **挂了三轮的「分支歧义」在此消解，且不是被解决的**：
         `ensurePreviewStageMediaRouteInitialized` 的实现体真正构造 `PreviewStageMediaHost`、
         接多条捕获 `session_` 的 `QObject::connect`、读文档 `videoPath`——它不是薄转发，
         合理地需要 `Session`。歧义只在「要把函数体搬进协调器」时才是问题；改走端口后函数体
         留在原处不动，协调器只说「确保已初始化」，**歧义不再相关**。
         **四个端口的切法各有依据，两种各两个，不强行统一**：
         偏好（能力/Session）、校验（宿主/ValidationHost）、文档（宿主/DocumentSessionHost）、
         预览（能力/Session）。归属散的按能力切、归属集中的按宿主切。
      4b-2e. ~~最后 3 处~~ **已完成 2026-09-02**：`session_.` 计数 3 → **0**。
         延迟沙盒 2 处：给已有的 `LatencyEngine` 契约加 `exitSandboxIfActive()`，
         协调器走它本就持有的 `services_.latencyEngine()`——**不新造端口**。
         刻意不走已有的 `LatencyEngine::sandbox()`：那会返回具体的 `LatencySandboxController*`，
         把 `src/tools/latency/` 头文件拽进 `playback/`，等于刚拆掉的耦合换个方向接回来。
         顺带删掉 `TimelineFlow.cpp` 里已成死引用的该头文件 include。
         底栏页签 1 处：函数体搬进协调器（只碰已持有的 `ui_`/`state_`），
         其中 `playback_->` 是自指绕路改 `this->`，跨宿主的那一处给校验端口加方法。
         执行体在此做了一个正确的判断：`scheduleWrappedListRelayout(QListWidget*)` **没有**
         按原样加上端口，因为端口头文件明写「Deliberately free of Session, QWidget, and QML/QSG types」
         且由只链核心库的 spec 在链接层守住；改为不带控件参数的
         `scheduleBottomTabsIssueListRelayout()`，`ValidationHost` 自己从共享的 `ui_` 取控件。
      5. ~~构造签名去掉 `Session&`~~ **已完成 2026-09-02**。
         构造签名首参改为 `QObject& owner`，成员 `Session& session_` → `QObject& owner_`；
         `PlaybackCoordinator.h` 的 `Session` **类型引用清零**（余下 14 处命中全是注释与
         `invalidateSession()` 方法名——后者指身份代次失效，与 `Session` 类型无关）。
         `runtime::shared::refreshQuickShellPreviewCompositeSurfaceState` 的第二参同样改 `QObject&`，
         `Shared.Preview.cpp` 因此不再 include `Session.h`。
         **一次方法失误的记录**：编排者曾用 `grep "session_"` 断定「剩余 14 处都不调 Session API」，
         但 `QPointer<Session> guard(&session_)` 之后的用法写作 `guard->…`，该 grep 看不见——
         实测 74 处。执行体撞上后停手升级，是对的。查清后归类：
         `guard->state_` 52 处（与协调器持有的是**同一个引用**，纯别名）、
         协调器已有的同名方法 19 处、校验端口 1 处、信号 1 处。
         执行体同时排除了一个能编译且能过测试的错误方案——guard 换 `QPointer<QObject>` 后
         再 `qobject_cast<Session*>` 找回：spec 传普通 `QObject` 时 cast 静默返回空、
         判活为真、**异步分析结果被悄悄丢弃**。那是把类型检查糊过去、留一个运行时活契约缺口。
         协调器那处 `emit documentValidationChanged()` 改走校验端口新增的
         `notifyDocumentValidationChanged()`，由本就在发该信号的 `ValidationHost` 实现——
         不新增同义信号，发射权留在已拥有它的宿主手里。
         **顺带发现（未处理，待裁决）**：`documentValidationChanged` 全仓 **6 个发射点、0 个消费方**，
         无任何 `connect` / `NOTIFY` / QML 处理器。与此前 `noteStatus` 是空函数同类——
         不报错、不挂测试，只是什么都不发生。**本轮未删也未接线**，那是产品决定。

      6. **TU 边界切分（2026-09-03 已完成）**。
         **2026-09-02 链接探测结论：协调器目前无法独立链接，4.9d 的完成判据并未达成。**
         真实链接尝试用到 33 个源文件仍剩 171 个未定义符号；符号闭包投影扩张到 246 个文件
         仍未收敛，且 `SessionBootstrap.cpp` 已进入必需集合。
         **根因**：4.9d 只把方法**体**搬进协调器，没有把**文件**按归属切开——
         `TimelineFlow.cpp`（`Session::`41 / `Coordinator::`56）、`Playback.cpp`（36/10）、
         `SurfaceContract.cpp`（17/66）、`PlaybackGlue.cpp`（11/6）、`FramePacing.cpp`（9/26）、
         `AnalysisFlow.cpp`（3/4）里两个类的方法混在同一个 TU。
         C++ 链接目标文件时必须解析该 TU 内**所有**未定义符号，与调用图可达性无关，
         所以链接协调器即链接这些文件里剩余的全部 `Session::` 方法及其依赖树。
         **教训**：「构造签名不再需要 `Session`」是必要条件而非充分条件；
         此前记为「完成判据达成」是未经验证的断言，现更正。
         剩余工作：把 117 个 `Session::` 定义从这 6 个 TU 移入 Session 侧文件
         （`SessionForwarding.cpp` 已是现成落点，那里已有 45 个），使协调器 TU 只含协调器方法。
         这同时符合仓库「不养 god file / 每文件一个职责」的既有准则。
         **完成情况**：117 个 `Session::` 定义已移入 5 个新的 `SessionForwarding.<原文件名>.cpp`，
         六个协调器 TU 内已无 `Session::` 方法定义（余下少量命中是 `Session::ValidationCacheEntry`、
         `Session::PreviewCanvasFrameRateMode` 这类**嵌套类型引用**，非方法定义）。
         Release 构建通过，全量 CTest 105 项 104 通过。
         **切分效果有数字**：重跑链接探测，未定义符号里的 `Session::` 从主导降到 **5 个**，
         且这 5 个来自探针 SOURCES 里其他文件（如 `DocumentFlow.cpp`）的引用，不再由协调器 TU 带入。
         其余 166 个是 `MuriAnalyzer`、`TimelineQuickModel`、解析器等**协调器真实的协作者**——
         性质从「被迫拽进来的无关实现」变成「本来就该一起链接的依赖」。
         **但门槛第 1 项仍未解锁，剩余障碍不再是耦合而是构建结构**：
         协调器的真实协作者闭包很大（符号闭包投影 246+ 文件，MiaCode 总共 651 个），
         把它们列进一个 spec target 意味着**每次构建多编译约 40%**。
         三条路各有代价，均已评估：
         ① 逐个加文件直到收敛——得到一份手工维护、必然腐坏的清单；
         ② spec 复用 MiaCode 的源文件列表——不动主 target，但构建时间近乎翻倍；
         ③ 把 MiaCode 拆成 OBJECT 库供两边共用——编译一次两边用，是最好的工程答案，
         **但会改变 qrc 资源的编译方式**。资源在静态/对象库里的自动初始化是经典坑，
         其失败形态是运行时资源加载不到——CTest 抓不到，而本环境无 GUI 验证能力。
         **③ 不在无人监督时做。**
         **决定：先做 4.9e 再回来重探。** 4.9e 把 canonical 状态从共享袋收进协调器私有成员，
         方向上是减少依赖而非增加，可能显著缩小闭包。届时用同一个探针重新量。
         探针 spec `playback_coordinator_construction_spec` 已留在 CMake 里（标
         `EXCLUDE_FROM_ALL`、不注册 ctest），作为证据与切分完成后的验收工具。

      **完成判据：`PlaybackCoordinator` 能在 spec 里被构造出来——即探针 spec 链接通过。**
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
