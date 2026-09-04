# QML UI v2 架构重设计

> 状态：已确认（2026-08-24）。本文是目标架构的权威描述，实现按第 8 节分阶段推进。
> 背景见 [QML_UI_V2_EXECUTION_AND_ACCEPTANCE_AUDIT_ZH.md](../../audit/QML_UI_V2_EXECUTION_AND_ACCEPTANCE_AUDIT_ZH.md)。

## 1. 为什么重设计

v2 目前是一层 QML 外壳，驱动一个**隐藏的 v1 `QMainWindow`**。这不是渐进迁移的中间态，而是稳定的双所有者结构，已经产出了可观测的后果：

- 一次打开谱面触发**两次** `documentReplaced`（2026-08-24 采集，相隔 6 ms），而 QML 投影走 `QueuedConnection`，于是存在能读到中间态的窗口。
- `syncDifficultyEditors` 只过滤不补回，配合上面的窗口，会把编辑器标签清空且无法恢复（已修，`e55b1665`）。这类洞是结构性的，逐个修补不会收敛。
- 每次 QML 编辑都会把全文 `setPlainText()` 喂给隐藏的 `PlainCodeEditor`，文档因此有两个所有者。
- `QuickShellController` 用定时器**轮询** 33 处 MainWindow 状态，状态传播没有顺序保证。

量化的耦合面：v2 调用 **69 个** MainWindow 公开方法；MainWindow 的 section 合计 **43,460 行**；`MiaCode` 目标链接 `Qt6::Widgets`。而 v2 自己的 QML 层只用到 2 个 Widgets 头——**耦合不在 QML 层，在于后端本身就是 QMainWindow**。

## 2. 目标

1. 彻底移除 `Qt6::Widgets` 依赖，以 C++ `QGuiApplication` + `QQmlApplicationEngine`
   承载完整的 Qt Quick/QML UI；QML 负责可见窗口、控件、菜单、对话框和拖放投影。
2. 按构造消除双所有者与轮询造成的竞态，而不是继续打补丁；QML 上下文不得再以隐藏
   `MainWindow` 作为应用服务总入口。
3. 复用已经与 Widgets 无关的渲染与领域代码，不重写它们。
4. 以“无主依赖清零、功能依赖可解释、可选能力可隔离”为轻依赖目标，而不是删除所有
   Multimedia、OpenGL、FFmpeg、BASS 等产品能力所需的库。

### 2.1 依赖目标

轻依赖分为三个层次：

| 层 | 目标 | 依赖口径 |
|---|---|---|
| QML 宿主基础 | 编辑器、菜单、窗口、QML 控件可启动 | `Qt6::Core`、`Qt6::Gui`、`Qt6::Qml`、`Qt6::Quick`、`Qt6::QuickControls2`，以及 QML import plugins |
| 产品能力 | 编辑、预览、音频/SFX、背景视频、普通导出、封面导出完整可用 | 按能力保留 `Qt6::Multimedia`、实际需要的 OpenGL/媒体桥、FFmpeg、BASS、SoundTouch、miniz 等 |
| 可选能力 | Net、特殊解码/导出后端不拖累默认宿主 | 通过插件、独立目标或明确的按需路径隔离；未启用的能力不能无条件进入主程序链接闭包 |

`Qt6::Widgets`、`QApplication`、`QWidget`、`QDialog`、`QStyle` 和隐藏 `MainWindow` 是
必须清零的 UI 遗留。`Qt6::Network`、`Qt6::Svg`、`Qt6::OpenGL` 和
`Qt6::MultimediaQuickPrivate` 则必须按真实使用点、平台条件和 Qt 版本兼容性记录，不能仅
因 DLL 数量少就删除。`ShaderTools` 如只用于构建期工具，不计入运行时依赖。

## 3. 已确认的决定

| 决定 | 选择 | 理由 |
|---|---|---|
| 文档/解析/校验/Muri 的所有者 | 新建非 widget 的 `ChartWorkspace` 核心；`MainWindow` 最终整体删除 | 单一所有者是消除竞态的前提；保留 MainWindow 会把 176 方法的形状继承下来 |
| timeline/预览的复用边界 | 只复用渲染与数据层；`QuickShellController` 随 v1 退役 | 它 1336 行里有 29 处 `surfaceHost_` 分支（v2 下是死代码）和 33 处轮询，属于 v1 外壳胶水而非预览逻辑 |
| v1 独占功能的存活范围 | 只保留 Net 批量、封面导出、ZIP 打包；扩展系统只保留归档契约 | 见第 7 节的代价说明 |
| 迁移路径 | 缠杀式：新核心与 MainWindow 共存，按域搬迁 | 43,460 行无法一次性重写；平行新目标会翻倍构建/测试面并有长期分叉风险 |

## 4. 目标分层

```
QML UI (MiaCode.UI)            纯 Qt Quick，零 C++ UI 类型
        ↓  窄 QObject 门面
v2 应用层  src/app/v2/
   ApplicationServices  服务装配 + 生命周期（非 Widgets）
   ChartWorkspace    文档 + 难度 + revision + dirty（唯一所有者）
   AnalysisService   校验 + Muri，输出带 revision 的快照
   PreviewSession    预览运行时生命周期、传输、媒体绑定
   TimelineSession   时间轴模型/桥接、跟随
   EditorService     编辑策略、撤销、书签、补全
   ExportService     视频 / 封面 / ZIP / Net 作业
   ShortcutService   ShortcutRegistry 投影
        ↓
领域与引擎层（不动，本就无 widgets）
   core/chart · timeline · preview
   tools/{muri, video_export, zip_export, net, cover_export}
```

**三条硬规则：**

1. 依赖单向：QML → 应用层 → 领域层。领域层不得回指应用层，应用层不得回指 QML。
2. 无轮询。所有状态变更以信号推送，附带显式 revision。
3. 每次跨域交接都携带 `(difficultyId, revision)` 身份，接收方据此丢弃过期数据。这是现有 `QmlEditorNavigationRequest` 的门控规则，推广为全局约定。

## 5. 组件契约

每个组件回答三件事：做什么、怎么用、依赖什么。

### ChartWorkspace
- **做什么**：拥有 `SimaiDocument`、活动难度、dirty 状态和单调递增的 `revision`。所有文档变更都经由它。
- **怎么用**：`open(path) → Result`、`save() / saveAs(path) → Result`、`apply(EditTransaction) → Result`、`selectDifficulty(id)`、`addDifficulty/removeDifficulty`、`replaceSource(text) → Result`。读取以值返回快照。
- **依赖**：`core/chart/*`。不依赖任何 UI 类型。
- **不变量**：一次变更 = 一个事务 = **恰好一次** `changed(revision)`；外部永远观察不到中间态。

### AnalysisService
- **做什么**：对 workspace 的快照跑校验与 Muri，产出带 revision 的分析快照。
- **怎么用**：订阅 `ChartWorkspace::changed`，异步分析，完成时发 `analysisReady(revision, snapshot)`。消费者比对 revision，过期即丢。
- **依赖**：`ChartWorkspace`、`core/chart/parser`、`tools/muri`。

### PreviewSession
- **做什么**：预览运行时的生命周期、传输控制（播放/暂停/seek/速率）、媒体绑定（BGM、PV）。
- **怎么用**：`play/pause/stop/seek(second)/setRate(r)`；发 `positionChanged`、`stateChanged`。跟随装饰以值发布。
- **依赖**：`preview/runtime/*`、`ChartWorkspace`。

### TimelineSession
- **做什么**：维护 `TimelineQuickModel` 与 `TimelineQuickStateBridge` 的接线、跟随状态、缩放与亮度设置。
- **怎么用**：QML 侧 `TimelineQuickItem` 绑定其 bridge；导航与光标同步经由它。
- **依赖**：`timeline/*`、`ChartWorkspace`、`PreviewSession`。

### EditorService
- **做什么**：文本编辑策略（半角、括号、hold、补全）、撤销栈、书签。已存在为 `QmlEditorController`，迁入应用层后不再依赖 MainWindow。
- **依赖**：`editor/SimaiTextEditPolicy`、`editor/SimaiCompletionCatalog`、`ChartWorkspace`。

### ExportService
- **做什么**：视频导出（已有 `QmlExportSession`，片头音选择/导入/独立音量已贯通现有
  snapshot/worker）、封面导出、ZIP 打包、Net 批量上传/下载，统一为作业模型（提交 → 进度 → 结果）。
- **依赖**：`tools/video_export`、`tools/cover_export`、`tools/zip_export`、`tools/net`。这三项存活功能的引擎层需要先去 widgets 化：`zip_export` 已干净（0/2 文件耦合），`net` 一半耦合（4/8），`cover_export` 大部分是 Widgets 对话框（9/13）。
- **依赖边界（2026-08-31 修正）**：`zip_export`、`cover_export` 和视频导出属于主产品能力；Net
  当前只有引擎保留、入口暂时移除，不应因为仍在源码列表中就无条件把 `Qt6::Network` 链进默认
  UI 进程。恢复 Net 页面时可重新纳入，或者先将其放到插件/独立目标。

### ShortcutService
- **做什么**：把 `ShortcutRegistry` 投影给 QML（portable 与 native 两种拼写）。已存在为 `QmlShortcutModel`，无需改动。
- **依赖**：`app/ui/ShortcutRegistry`（QtCore + QtGui，无 Widgets）。

## 6. 关键数据流

**一次编辑**
```
QML 输入 → EditorService 产出 EditTransaction
        → ChartWorkspace.apply(tx)          单一所有者写入
        → changed(rev)                      恰好一次
        ├→ TimelineSession   按 rev 增量更新
        ├→ PreviewSession    按 rev 刷新快照
        └→ AnalysisService   异步分析，完成后 analysisReady(rev)
```
无回写、无往返、无隐藏副本。

**一次切换谱面**
```
ChartWorkspace.open(path)
   内部：解析 → 选定初始难度 → 提交
        → replaced(rev, identity)           恰好一次
        └→ 各 session 依 identity 重建
```
QML 收到的第一眼就是终态，不存在"活动难度尚未设定"的可观察窗口——这正是当前编辑器标签被清空的成因。

## 7. 错误处理

失败是**值**，不是对话框。`open` / `save` / `apply` / `replaceSource` 返回结果对象，携带 `accepted`、`revision` 与结构化诊断（沿用现有 `DocumentSourceReplaceResult` 的形状）。应用层不弹窗、不阻塞——这是移除 Widgets 的前提，也让每条失败路径都可测。

## 8. 迁移阶段

每阶段结束时程序都可构建、可运行、可回归。

| 阶段 | 内容 | 完成标志 |
|---|---|---|
| 0 | 删除 v1 shell、扩展宿主（3,141 行，v2 未使用）、被舍弃的三组页面（偏好设置、延迟检测、媒体处理/音轨工具） | `--ui=v1` 与 `MIACODE_UI_SKIN` 消失；体量显著下降 |
| 0a ✅ | **已完成 2026-08-25**：v1 外壳、原生表面再宿主、主题桥、macOS 表面支持、全部 v1 QML 与皮肤入口 | 源码净 **−7,770 行**（39 文件，+284/−8,054）。`QuickShellController` 保留待阶段 2。全量 CTest 71/72（唯一失败 `qtavplayer_platform_spec` 为既有问题，在本阶段起点即复现） |
| 0b ✅ | 删除扩展宿主 | **已完成 2026-09-01**：删除 `ExtensionManager`、`EmbeddedExtensionRuntime`、`ExtensionOpenBridge`、扩展 watcher/偏好页/事件发布/手势分发/预览 overlay 及 bundled deployment；保留 `ExtensionManifest`、schema、SDK 声明、文档、示例和 API registry 作为归档与离线校验面。 |
| 0c ✅ | ~~删除~~ **补成 QML**：偏好设置、延迟检测、媒体处理/音轨工具页面 | **方向已于 2026-08-29 由所有者改判：三页不删，重建为 QML 原生页**，2026-08-29 全部完成。`PreferencesDialog.qml` + `QmlPreferencesModel`（四页签，快捷键录制内置）、`LatencyPage.qml` + `QmlLatencyModel`、`MediaToolsDialog.qml` 一族 + `QmlMediaToolsModel`。`WindowContainer` 与整套 widget 宿主机制随之删除。**音视频处理工具仍未通过原生桌面验收。** |
| 1 ✅ | 建立 `ChartWorkspace` 与 `AnalysisService`，文档域搬迁 | **生产 QML 文档与分析真相已迁移（2026-08-26）：** `ChartWorkspace` 提供 Widgets-free 的严格预检、单次 revision 与 save-point dirty；`ChartWorkspaceFileService` 负责 BOM/系统编码与 `QSaveFile`；`AnalysisService` 订阅 workspace，先发布 pending，再异步发布同一 `(difficultyId, revision)` 的验证、偏移 markers、Muri 与静态引用整包。QML 文档/分析消费者会丢弃过期整包，显式验证不再调用 `MainWindow`。隐藏 `MainWindow` 只适配 committed 文档值并暂供 timeline/preview 与 legacy 页面。**完成标志已于 2026-08-30 达成：`src/editor/PlainCodeEditor.*` 与第二份文档副本已删除**，应用不再链接任何 Widgets 文本编辑器。 |
| 2 ✅ | 建立 `PreviewSession` 与 `TimelineSession`，`QuickShellController` 退役 | **已完成 2026-08-30**（`TimelineQuickModel` 所有权除外，所有者决定延后）。落点是三个对象而非两个：`QmlTimelineModel` / `QmlPreviewModel` / `QmlShellLifecycle`。轮询定时器消失，离散状态由 `MainWindow::shellPresentationChanged` 推送、播放头由 `shellPreviewPlayheadChanged` 推送；`surfaceHost` 分支归零 |
| 3 | `ExportService`：视频沿用，封面/ZIP/Net 重建为 QML 并去除其引擎层的 widgets 耦合 | Widgets 对话框归零 |
| 3.5 | 应用宿主脱钩与依赖分层：抽出 `ApplicationServices`，让 QML context 不再依赖 `MainWindow`；明确 Net、SVG、OpenGL、Multimedia private bridge 的归属 | `QmlApplicationContext` 不再持有 `MainWindow&`；默认目标依赖 allowlist 可解释 |
| 4 | 删除 `MainWindow`，切换 `QGuiApplication`，并完成产品 Widgets 脱钩 | **宿主切换与 GUI 初步验收通过（2026-09-04）；产品 Widgets 清零完成（2026-09-05）：** `main.cpp` 与 CLI entry 使用 `QGuiApplication`，唯一 `QQmlApplicationEngine` 由 `QmlUiBootstrap` 持有；页面/文档未保存确认、保存路径选择和时间轴提示均由 QML 承载。`MiaCode` 产品源集与链接行不再包含 `Qt6::Widgets`，Release 构建及依赖/生命周期专项测试通过；Widgets 仅留给 dev-tool spec。音视频工具等 v2 页面仍需按第 7 章逐项原生桌面验收。 |

## 9. 测试策略

- 应用层全部是无 widget 的 `QObject`，直接进 spec，不需要 GUI。
- QML 侧沿用已建成的 `MiaCode.UI` spec 导入镜像 + `Qt6::Test`，驱动**真实组件**与**真实事件**，不用源码字符串扫描代替。
- 每个域搬迁**之前**先补该域的契约回归（单一所有者、单次通知、revision 门控），确认红态后再搬。
- 保留现有 17 项定向 spec 作为回归基线。
- 架构完成还必须补依赖验证：检查 CMake 的直接/递归链接闭包，检查 QML import 部署结果，
  并在 macOS / Windows 分别记录冷启动、编辑预览、背景视频、普通导出和封面导出的实际加载模块。
- `Qt6::MultimediaQuickPrivate` 暂时保留时，构建矩阵必须明确实际 Qt 小版本；长期迁移到公共
  Multimedia/VideoOutput API，不把私有模块扩散到 QML 宿主或业务层。

## 10. 本次范围裁剪的代价

被删除的功能及其后果，明确记录以免日后误以为是回归：

| 删除项 | 后果 |
|---|---|
| ~~偏好设置对话框~~ | **未删除。** 2026-08-29 所有者改判为补成 QML，已完成，能力无缺口。 |
| ~~延迟检测页~~ | **未删除。** 同上；`WindowContainer` 混合渲染树随重建一并移除。 |
| ~~媒体处理 / 音轨工具~~ | **未删除。** 同上；四条 ffmpeg 流程与 PV 批量队列均已是 QML 页面（**尚未通过原生桌面验收**）。 |
| Net 批量下载 / 上传 | **功能暂时移除（2026-08-29 所有者决定）**：两个 Widget 对话框与全部入口删除；引擎（`NetClient`、workers、scanner、diagnostics，均无 Widgets）保留在树上，恢复时直接补 QML 页面。 |
| 应用背景（Preferences → 背景页签） | **已恢复（2026-09-01）**：纯 `AppBackgroundSettings` + `QmlAppBackgroundModel`，由 QML root 绘制背景并按明/暗主题套用 overlay token；不恢复 `WA_DontShowOnScreen` painter。 |
| 扩展页签与开发者工具 | **已删除（2026-08-29）**。 |

原先"哪怕功能会缺失也要做"的授权覆盖的是 0c 那三页；**该授权已于 2026-08-29 被所有者收回**，
三页改为重建。本表现在记录的是**实际**的删除与后果，不是当初的计划。

## 11. 非目标

- 不重构 `TimelineQuickItem` / `PreviewRuntime` 内部（4,000+ 行 QSG 与时序，风险最高且与本次目标无关）。
- 不改变渲染架构决策：进程内 QSG 仍是唯一渲染路径。
- 不做与本目标无关的重构。

## 12. 当前壳层已知限制（接受，不修）

低窗口高度下，底栏拖不到标称的左侧高度 65%。`MainSplitView.qml` 里 `editorHost` 仍有 `SplitView.minimumHeight: 180`，`Main.qml` 窗口最低高度 480。左侧变矮后，65% 会把编辑器压到 180 像素以下，分隔条被该下限截断。标称范围仍是 20%–65%；这是当前壳层约束，不要当布局缺陷去改 180，除非产品明确要求取消该像素下限。
