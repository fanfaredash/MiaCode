# QML UI v2 补完清单

> 文件名沿用历史（多处文档与仓库指南指向它），内容已重写为**当前唯一在推进的工作清单**。
> 目标架构与阶段定义见 [QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md](QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md)；
> 本文只登记「还差什么、完成标志是什么、什么待复核」。
>
> **基线：`117a76a1`（2026-08-29）**。本文所有数字均为该基线上的源码实测，不是估算。

## 1. 目标边界

只有三件事在范围内：

1. **功能对齐补完** —— v2 QML 能原生完成 `dev` 上的既有工作流。
2. **删除 v1** —— 隐藏的 `MainWindow`、隐藏的 `PlainCodeEditor`、`QuickShellController` 兼容层与全部 Widget 页面/对话框。
3. **从链接中移除 `Qt6::Widgets`** —— `CMakeLists.txt:101` 的 `COMPONENTS` 与 `:845` 的 `target_link_libraries` 都不再出现 `Widgets`，`src/app/main.cpp:436` 由 `QApplication` 改为 `QGuiApplication`。

与这三件事无关的重构一律不做（见架构文档第 11 节非目标）。

### 判定规则

- 一个条目只有同时满足「v2 QML 原生可用」**且**「不再需要任何 Widgets 类型」才能勾掉。仅"能通过隐藏 Widget 达成"记为**未完成**。
- 自动 spec 通过 **不等于** 验收。原生桌面验收单独记录在第 6、7 章，构建与 CTest 不能替代。
- 失败是**值**不是对话框：应用层不得弹窗、不得阻塞（架构文档第 7 节）。新代码引入 `QMessageBox` / `QFileDialog` 视为反向进度。

## 2. 基线实测：还挂着多少 Widgets

| 项 | 实测 | 位置 |
|---|---|---|
| 链接声明 | `COMPONENTS … Widgets …` + `Qt6::Widgets` | `CMakeLists.txt:101`、`:845` |
| 应用对象 | `QApplication` | `src/app/main.cpp:436` |
| 隐藏 `MainWindow` | **73 文件 / 48,732 行** | `src/app/mainwindow/` |
| 隐藏 `PlainCodeEditor` | 仍在 `FrameBootstrap.cpp:291` 构造；**20 个文件**仍引用 | `src/editor/`、`src/app/mainwindow/sections/*` |
| `QuickShellController` | 1,251 行（`src/app/quick_shell/` 全目录）；`surfaceHost_` 分支已归零；**`refreshTimer_` 轮询仍在**（`QuickShellController.cpp:71/82-84/693-699`） | — |
| QML 仍消费的 controller 属性/方法 | **25 个**（`shellController.*`） | `src/app/qml_ui/**/*.qml` |
| Widget UI 代码量 | cover_export **5,780** / latency **1,040** / media **393**<br>*（2026-08-29 已删除：export_page 738、BatchExportPanel 组 947、MainWindow 嵌入面板机制 479、`VideoExportDialog` 组 4,691、`NetBatch*Dialog` 1,479）* | `src/tools/*` |

### v2 自身新代码里的 Widgets 泄漏（优先清）

| 位置 | 内容 |
|---|---|
| ~~`src/app/qml_ui/export/QmlExportSession.cpp`~~ | ~~`QFileDialog` ×5、`QMessageBox` ×5~~ —— **已清零（2026-08-29）**。`src/app/qml_ui` 全目录现已无 Widgets 对话框 |
| `src/app/qml_ui/QmlEditorPageHost.*` | `QWidget` 宿主表面 + `WindowContainer` 适配（`MainSplitView.qml:192`） |
| `src/app/ui/ChartDropOverlay.h:7` | `class ChartDropOverlay final : public QWidget`，被 v2 root window 拖放路径使用 |

`MainView.qml` 的对话框已是 `QtQuick.Dialogs`（非 Widgets），这条不用改。

## 3. 功能对齐缺口

按「v2 用户能不能原生做到」分三类。

### A. 无 QML 实现，入口弹「暂未更新支持」（3 项）

`MainView.qml:304` 的 `unavailableFeatureDialog` 当前只被三处触发：

| 功能 | 触发点 |
|---|---|
| 关于 MiaCode | `chrome/MainMenu.qml:337` |
| 音频设置 | `chrome/MainToolBar.qml:58` |
| 预览设置 | `chrome/MainToolBar.qml:63` |

### B. 有入口，但落到 Widget 对话框/页面（8 项）

| 功能 | v2 入口 | 落点 |
|---|---|---|
| 延迟校准 | `ToolsSidebarPage.qml` → `openLatencyPage()` | `LatencyDetectionPage`（`WindowContainer` 宿主，1,040 行） |
| 音视频处理 | → `openMediaProcessingTools()` | `MainWindow::onMediaProcessingTools()` → `PvBatchCompressionDialog` |
| ~~整谱规范化~~ | → `openNormalizeWholeChart()` | **已完成（2026-08-29）**：`NormalizeOptionsDialog.qml` + `QmlDocumentModel::normalizeChartSelection`，结果作为编辑器事务应用（undo 覆盖）；Widget 对话框与 `DocumentSection::onNormalizeWholeChart` 已删除 |
| ~~Net 批量下载 / 上传~~ | —— | **功能已暂时移除（2026-08-29）**：两个对话框与全部入口删除；引擎（`NetClient`、workers、scanner、diagnostics，均无 Widgets）保留在树上，恢复时直接补 QML 页面 |
| 封面导出 | → `openCoverExport()` | `CoverStudioWindow` 全家（5,780 行） |
| ~~打包 ZIP~~ | → `packAsZip()` | **已完成（2026-08-29）**：走 `UiRequestService` 选路径与提示、`JobProgressService` + `JobProgressOverlay.qml` 显示进度与取消 |
| 偏好设置 | `QmlCommandService::openPreferences()` | `MainWindow::onPreferences()` |
| ~~批量导出~~ | `openBatchExport()` | **已完成（2026-08-29）**：QML `ExportVideoPage` 的 batch 页是唯一批量界面，`BatchExportPanel` 组已删除 |

> 0b（扩展宿主）仍是已延后状态，不得记为"已删除"或"功能不可用"。
>
> **0c 已于 2026-08-29 由所有者重新定向：偏好设置 / 延迟检测 / 音视频处理三页「补成 QML」，不删除。**
> 这推翻了架构文档第 10 节记录的相反决定（"哪怕功能会缺失也要做"）——那节与第 8 节的 0c ⏸ 行
> 必须在本批工作中改齐，否则只读架构文档的人会做反。
>
> **排期决定（2026-08-29 所有者）：**
> - **Net：功能暂时移除。** 入口与两个 Widget 对话框已删除；`src/tools/net` 的引擎部分保留。
> - **封面导出：放到最后做**（本批最末一项，不是取消）。
>
> `Qt6::Widgets` 的最终摘除现在只剩封面导出、音视频处理、偏好设置、延迟校准四项挡着。

### C. 已是 QML 页面，但仍借 Widgets 完成子流程（1 项）

- 视频导出页：文件选择与结果提示走 `QFileDialog` / `QMessageBox`（见 §2）。

### D. 影子 Widget 状态（无用户入口，但仍在链上）

- [x] `ExportLauncherPage`（738 行）已删除（2026-08-29）。导出难度的真相移到 `QmlExportSession`：
      `resolveToolsMenuExportDifficultyId()` 现在读 `pageSessionActive() + selectedDifficultyId()`，
      原先的 `menuActionDifficultyId()` / `selectedDifficultyId()` 两级回退只差一个会话门控，合并为一处。
      同时删掉恒真的 `qmlExportCenterActive_` 标志——它只在 `QmlUiBootstrap` 里被置 true，v1 外壳删除后
      它守护的每个 `else` 分支都是死代码。隐藏 `editorStack_` 里换成一个空 `QWidget` 占位：
      `PreviewPlaybackGlue.cpp:42` 等 7 处仍在用 `currentWidget() == chartPage_/metadataPage_`
      判断"当前是哪个字段"，若导出页不再占据栈位，这些判断会在导出页上误判。
      **本项没有新增行为 spec**（`MainWindow` 无法实例化）；保证来自编译、`export_page` 零引用，
      以及全量 CTest 未新增失败。导出难度在 Tools 菜单各动作里的实际取值仍需原生桌面确认。

## 4. 阶段任务与完成标志

### 阶段 1（收尾）—— 文档域单一所有者

已落地：`ChartWorkspace`、`ChartWorkspaceFileService`、`AnalysisService`（`src/app/v2/`），生产 QML 的文档事务、save-point dirty、revision-stamped 分析快照均不再以 `MainWindow` 为真相。

剩余：

- [ ] 删除隐藏 `PlainCodeEditor` 与第二份文档副本（阶段完成标志）。当前仍有 20 个文件引用它。
- [ ] 修复 `qml_document_lifecycle_contract_spec`（**当前为红**，见 §7.1）。

### 阶段 2 —— `PreviewSession` / `TimelineSession`，退役 `QuickShellController`

- [ ] 把 25 个 `shellController.*` QML 消费点搬到两个新会话对象。
- [ ] `TimelineQuickModel` 所有权从 `MainWindow` 迁到 `TimelineSession`。
      *（增量解析本身已完成：`applyTextChange(QString,…)` 取代 `applyContentsChange(QTextDocument*)`，`MainWindow.TimelineQuickParse.cpp:54` 已无全量回退分支。剩下的只是所有权。）*
- **完成标志**：`refreshTimer_` 消失；`src/app/quick_shell/` 删除；QML 不再出现 `shellController`。

### 阶段 3 —— `ExportService` 与 Widget 对话框归零

已建成的共享边界（`src/app/v2/`，均无 Widgets，各自有只链 `Qt6::Core`+`Qt6::Test` 的 spec）：
`UiRequestService`（选文件 / 消息 / 带动作的消息）与 `JobProgressService`（进度 + 不确定态 +
按 token 的协作式取消）。`MainWindow` 持有唯一实例，`MainView.qml` 承载唯一的 `UiRequestHost`
与 `JobProgressOverlay`。

- [x] **进度条归一（2026-08-29）**：视频导出、批量导出、ZIP 打包、音视频处理（4 条 ffmpeg 流程）
      现在全部走同一个 `JobProgressOverlay`。`src/app` 已无 `QProgressDialog`。同时删除了两处
      并行进度表示：预览传输条上的 inline 导出进度（`videoExportUseInlineProgress_` 自嵌入面板
      删除后再无人置 true）与恒返回 -1 的 `shellVideoExportProgressSeconds()`（连带去掉
      `QuickShellController` 轮询间隔对它的依赖）。

- [x] `QmlExportSession` 的 `QFileDialog` / `QMessageBox` 换成 QML 侧 `QtQuick.Dialogs` + 结果值
      （2026-08-29）。落点是可复用的 `miacode::v2::UiRequestService` + `components/UiRequestHost.qml`，
      而不是导出页就地改写——B 类的 Net / 音视频处理 / 偏好设置页同样需要选文件与提示，共用这一条边界。
      `ui_request_service_spec` 只链 `Qt6::Core` + `Qt6::Test`，所以任何把 Widgets 对话框放回这条边界的
      改动都会**链接失败**，不依赖字符串扫描；`qml_export_video_page_spec` 驱动真实 `FileDialog` /
      `FolderDialog` / `MessageDialog` 走完请求—应答回环。仍需原生桌面确认实际弹窗外观与取消语义。
- [ ] 封面导出（暂缓）、Net 上传/下载、ZIP、整谱规范化重建为 QML 页面 + 无 Widgets 作业 API。
- [ ] 删除 `CoverStudioWindow` 组、`NetBatch*Dialog`。
      *（`ExportLauncherPage`、`BatchExportPanel` 组、`VideoExportDialog` 组已于 2026-08-29 删除。）*
- [x] **废弃 `VideoExportDialog` 的扩展 API 形态**（2026-08-29）。扩展命令 `export.video.start`
      与方法 `export/startVideoExport` 原本打开模态导出对话框，现在改为打开 QML 导出中心的单个导出页
      （`onExportPreviewVideo()` 保留同名但换成 QML 路由）。扩展面能力未削减，Widgets 对话框消失。
      `FontLibrary`、`HudFontSettings`、`CardFontSettings` 被 cover_export 与 MainWindow 对话框复用，
      **未随组删除**。
- [ ] `ChartDropOverlay` 改为 QML 覆盖层。
- **完成标志**：`grep -rl "QtWidgets\|QDialog\|QMessageBox\|QFileDialog" src/tools src/app/qml_ui` 为空。

### 阶段 4 —— 删除 `MainWindow`，摘掉 `Qt6::Widgets`

- [ ] 搬迁/删除 `src/app/mainwindow/` 全部 73 文件。
- [ ] `src/app/ui/` 的 widget 辅助件（`UiComponents`、`EditableValueLabel`、`FlowLayout`、`BusySpinner`、`AppBackgroundPainter`）随之处理。
- [ ] `main.cpp` 换 `QGuiApplication`；`CMakeLists.txt` 去掉 `Widgets`。
- **完成标志**：上述两处 CMake 声明不含 `Widgets`，且 Release 构建 + 全量 CTest 通过。

### 0b / 0c（延后，未取消）

- [ ] 0b 扩展宿主删除 —— 保持现状。
- [ ] 0c 偏好设置 / 延迟检测 / 音视频处理页 —— **2026-08-29 决定补成 QML 原生页（不删除）**，与阶段 3
      的 B 类条目同批推进；同批必须改齐架构文档第 8、10 节。

## 5. 已完成（事实登记，不再逐条展开）

- 阶段 0a：v1 QuickShell 外壳、入口、原生表面再宿主、皮肤切换（`resolveUiSkin`）全部删除，`--ui=v1` / `MIACODE_UI_SKIN` 不复存在。
- 文档域：`ChartWorkspace` + `ChartWorkspaceFileService` + `AnalysisService` 接管文档事务、save-point dirty 与 revision 分析快照。
- 编辑器：共享 `SimaiTextEditPolicy`、补全、书签、查找替换、诊断跳转、语法高亮、快捷键（`QmlShortcutModel` 复用 `ShortcutRegistry`）。
- 同步链：`EditorSyncController` 统一播放跟随、离散导航、编辑器上下文与触控创作，全部经队列边界投递；`TimelineView`（QWidget 时间轴，约 3,470 行）已删除。
- 时间轴/底栏：缩放、亮度、follow-code、validation / Muri 三 tab、底栏高度联动、工作区面板互换。
- 预览：三态渲染模式（`PreviewRenderModeMenu.qml`）、负时间传输条、surface 互斥生命周期、统计投影去热路径。
- 导出：`ExportVideoPage.qml` + `QmlExportSession`，含片头音文件与 0–200% 音量，写入单个/批量 snapshot 与 worker。
- ChartDrop：root window 登记 + 事件过滤器 + overlay 同步。

## 6. 验收规则

1. 每个域搬迁**之前**先补该域的契约回归，确认红态再搬（架构文档第 9 节）。
2. QML 回归驱动**真实组件与真实事件**，不用源码字符串扫描代替。
   —— §7.1 正是源码扫描式契约的失效案例。
3. `QT_QPA_PLATFORM=offscreen` 在本机会触发既有 macOS 平台插件崩溃，**不能**用作桌面视觉/输入验收。
4. 原生桌面验收按平台分别记录；macOS 通过不能推断 Windows 通过。

## 7. 遗留问题与更新后待复核

**本章是交接面。** 每条写明：问题是什么、当前状态、以及在 `117a76a1` 之后**必须重新核实什么**。
未列为"已复核"的条目，一律不得由构建、CTest 或历史记录推断为通过。

### 7.1 `qml_document_lifecycle_contract_spec` 当前为红（阻塞项）

- **现象**：断言 `initial, navigation, and follow identities stay on the committed workspace revision` 失败。
- **原因**：该断言用源码字符串扫描，要求 `appliedQmlWorkspaceRevision_ > 0` 同时出现在
  `MainWindow.PreviewTimelineFlow.cpp` 与 `MainWindow.ValidationFlow.cpp`。`117a76a1` 重写了这两个文件：
  前者改为把 revision 直接交给 `editorSyncController_->requestNavigation(...)`（`PreviewTimelineFlow.cpp:1518`），
  后者的诊断跳转整段移走，只剩 `clearPreviewFollowDecoration()` 里的 `publishFollow`。
- **待复核**：身份门控是否真的完整地搬进了 `EditorSyncController`（而不是丢失）。确认后**改写 spec 为行为回归**，不要把字符串塞回去。

### 7.2 QV4 GC 崩溃（Windows `0xC0000005` @ `Qt6Qml.dll+0x169A39`）

- 根因与复现见 [QML_RUNTIME_CRASH_AUDIT_ZH.md](../../audit/QML_RUNTIME_CRASH_AUDIT_ZH.md)。
- 审计推荐的 A/B/C 已落地：A —— `centerCursorInView()` 改为单一可合并的 `cursorCenterTimer.restart()`（`SourceEditor.qml:214`）；
  B —— `CompletionPopup.qml` 的光标/布局 `Connections` 已 `enabled: root.visible`；
  C —— `EditorSyncController` 队列边界。
- **待复核（D 项，未执行）**：`QV4_MM_AGGRESSIVE_GC=1` 下，代码跟随开启完整播放 10 轮、跨多 token、反复开关跟随、暂停/继续/停止/重播、弹窗开与关两种状态；Windows Release 与 Linux Release 各一轮。**这是本次重构最关键的验收，缺它则崩溃只能算"推测已修"。**

### 7.3 二阶段五项桌面验收已因同步链重写而失效

- 2026-08-24 记为通过的五项（`ca943a82` 暂停跟随装饰、`2a27630d` 书签/右键菜单、`75c89635` 命令修饰键、`f4251ca0` IME 提交递归、`f451ae09` Command+点击 seek，以及 `5ee23d38` 窄窗口 completion popup）针对的是旧跟随/导航链。
- `117a76a1` 把整条链换成值对象 + 队列投递，并重写了 caret 显隐、系统周期闪烁与播放中暂停语义；上游 merge 提交自述「Native GUI acceptance remains pending」。
- **待复核**：五项全部重跑，外加新语义——普通点击只移动 caret 不居中；Ctrl/Command+点击先暂停再 seek 并居中 playhead；播放中指针按下立即暂停；播放期普通 caret 隐藏、跟随光标显示，暂停后由焦点恢复；caret 闪烁跟随 `Application.styleHints.cursorFlashTime`。

### 7.4 本次 UI 重构无任何 GUI 验收记录

`04401157` 合入的这批改动尚未被任何人在原生桌面上看过：

- 底栏（时间轴）UI 重构、`TimelineZoomMenu` / `TimelineBrightnessMenu`
- `PreviewRenderModeMenu`（sticky popup，非 `Menu`）
- 时间轴高度随底栏拖拽联动、工作区面板互换
- macOS 自定义菜单栏位置修正、响应式预览尺寸修正
- 新增共享控件 `AppCheckBox`、`AppDropDownButton`

**待复核**：窄窗口与 1280×720 下的命中区与视觉；浅/深色；中文/英文；Large system font。

### 7.5 底栏高度配置键变更，无迁移

`QmlUiSettings` 的 `ui/bottomPanelHeight`（像素）已换成 `ui/bottomPanelHeightRatio`（比例，默认 0.35，`QmlUiSettings.cpp:33-36`），**没有写迁移**。老用户升级后底栏高度会回到默认值。
**待复核**：确认这是有意为之；若否，补一次性迁移。

### 7.6 撤销栈 dirty 联动（已修，待 GUI 复核）

dirty 真相已迁到 `ChartWorkspace` 的完整文档 save point，`Ctrl+Z` / `Ctrl+Y` 回到已打开或已保存内容会自动回 clean。
**待复核**：脏文档关闭流程的原生桌面回归（自动 spec 不覆盖对话框）。

### 7.7 切换文档后 PV 异常（延后）

多次复现失败，需求延后。埋点保留在树上（`editor/document_replaced`、`editor/document_shown`）。
**待复核**：仅在再次复现时重启排查，不预先投入。

### 7.8 播放期高位内存平台（延后）

现有证据是"跃升后稳定"（private memory 约 957 MB 平台，第二段播放 1,051–1,064 MB），不是持续泄漏。
**待复核**：阶段 2 迁移 Preview/Timeline **之前**先做分层取证（QtAVPlayer/D3D11VA 帧池、QML/Qt Quick、私有堆），不得先验归因于 preview texture cache。

### 7.9 手工回归清单（全部未执行）

- [ ] 脏文档、播放中、导出任务运行时的关窗流程与 v1 一致。
- [ ] 设备热插拔暂停后，下一次播放走 cold Prepare。
- [ ] play / pause / seek 按 completion 更新界面状态。
- [ ] EraseByArea、烟花时长、BGM 过轨静音行为正确。
- [ ] 音频拖放建谱与 ChartDrop 覆盖层（含 cancel）行为正确。
- [ ] QML 导出片头音文件与音量：原生试听 + 成片确认（自动规格只覆盖到 snapshot→worker 重建）。
- [ ] `d534b393` 的 bookmark / touch input 改动 GUI 回归。
- [ ] root 拖放、播放中关闭。

### 7.10 已知且接受、不修

- 低窗口高度下底栏拖不到标称 65%：`MainSplitView.qml` 的 `editorHost` 有 `SplitView.minimumHeight: 180`，`Main.qml` 窗口最低高度 480。标称范围仍是 20%–65%，这是当前壳层约束，不要去改 180，除非产品明确要求取消该像素下限。
- Windows 侧整体从未验证。
- 诊断开关 `MIACODE_SKIP_CRASH_HANDLER`：配合 Windows LocalDumps 时跳过 `crash_recovery::install()`，让访问冲突保持 `0xC0000005` 并生成转储。

## 8. 关键路径

| 角色 | 路径 |
|---|---|
| v2 启动 | `src/app/qml_ui/QmlUiBootstrap.*` |
| 契约根 | `src/app/qml_ui/QmlApplicationContext.*` |
| 文档所有者 | `src/app/v2/ChartWorkspace.*`、`ChartWorkspaceFileService.*` |
| 分析快照 | `src/app/v2/AnalysisService.*` → `QmlAnalysisModel.*` / `QmlAnalysisProjection.*` |
| 同步控制器 | `src/app/v2/EditorSyncController.*` |
| 文档桥 | `src/app/qml_ui/QmlDocumentModel.*`、`QmlDocumentProjection.*`、`sections/document/MainWindow.DocumentBridge.cpp` |
| 预览桥 | `src/app/qml_ui/QmlPreviewModel.*` |
| 编辑器 | `src/app/qml_ui/QmlEditorController.*`、`QmlEditorInputBridge.*`、`editor/SourceEditor.qml`、`SimaiSyntaxHighlighter.*` |
| 快捷键 | `src/app/qml_ui/QmlShortcutModel.*` ← `src/app/ui/ShortcutRegistry.*` |
| 视频导出 | `src/app/qml_ui/export/QmlExportSession.*`、`export/ExportVideoPage.qml` |
| 页面宿主（待删） | `src/app/qml_ui/QmlEditorPageHost.*`、`layout/MainSplitView.qml:192` |
| 主壳 | `src/app/qml_ui/Main.qml`、`layout/MainView.qml` |
| 临时兼容控制器（阶段 2 删） | `src/app/quick_shell/QuickShellController.*` |
| 开发索引 | `.agents/skills/miacode-dev-guide/references/feature-index.md` |

## 9. 更新规则

1. 条目状态变化时**同时**更新本文与架构文档第 8 节的阶段表。
2. 架构、入口或跨模块契约变化时，同步更新仓库指南（`feature-index.md` / `cross-chain-linkage.md`）。
3. 原生桌面验收结果写进第 7 章对应条目，注明平台与日期；构建与 CTest 不能替代。
4. 不得重新引入 v1 shell；恢复被裁剪功能须单独作产品决策。
5. 新代码不得引入 `Qt6::Widgets` 类型——那是本清单唯一的终点条件。
