# QML UI v2 补完清单

> 文件名沿用历史（多处文档与仓库指南指向它），内容已重写为**当前唯一在推进的工作清单**。
> 目标架构与阶段定义见 [QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md](QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md)；
> 本文只登记「还差什么、完成标志是什么、什么待复核」。
>
> **基线：`4f30a2d2`（2026-08-30）**。本文所有数字均为该基线上的源码实测，不是估算。
> 上一版基线是 `117a76a1`（2026-08-29）；阶段 1、阶段 2 与三个「暂未更新支持」入口
> 都在这两个基线之间落地，第 2 章的实测表已按新基线整体重测。

## 1. 目标边界

只有三件事在范围内：

1. **功能对齐补完** —— v2 QML 能原生完成 `dev` 上的既有工作流。
2. **删除 v1** —— 隐藏的 `MainWindow`、隐藏的 `PlainCodeEditor`、`QuickShellController` 兼容层与全部 Widget 页面/对话框。
3. **从链接中移除 `Qt6::Widgets`** —— `CMakeLists.txt:101` 的 `COMPONENTS` 与 `:844` 的 `target_link_libraries` 都不再出现 `Widgets`，`src/app/main.cpp:436` 由 `QApplication` 改为 `QGuiApplication`。

与这三件事无关的重构一律不做（见架构文档第 11 节非目标）。

### 判定规则

- 一个条目只有同时满足「v2 QML 原生可用」**且**「不再需要任何 Widgets 类型」才能勾掉。仅"能通过隐藏 Widget 达成"记为**未完成**。
- 自动 spec 通过 **不等于** 验收。原生桌面验收单独记录在第 6、7 章，构建与 CTest 不能替代。
- 失败是**值**不是对话框：应用层不得弹窗、不得阻塞（架构文档第 7 节）。新代码引入 `QMessageBox` / `QFileDialog` 视为反向进度。

## 2. 基线实测：还挂着多少 Widgets

| 项 | 实测（`4f30a2d2`） | 位置 |
|---|---|---|
| 链接声明 | `COMPONENTS … Widgets …` + `Qt6::Widgets` | `CMakeLists.txt:101`、`:844` |
| 应用对象 | `QApplication` | `src/app/main.cpp:436` |
| 隐藏 `MainWindow` | **72 文件 / 42,750 行**（上一基线 73 / 48,732，−5,982） | `src/app/mainwindow/` |
| ~~隐藏 `PlainCodeEditor`~~ | **已删除（2026-08-30）**。树上只剩三处提到这个名字：两条注释与 `ExtensionOpenBridge.cpp:337` 的 lint 规则名 | — |
| ~~`QuickShellController`~~ | **已退役（2026-08-30）**。`src/app/quick_shell/` 只剩 **246 行**预览合成表面管道（`QuickShellPreviewCompositeSurface.*` + 三个策略头），与外壳控制器无关 | — |
| QML 仍消费的 controller 属性/方法 | **0 个**（`shellController` 在 `*.qml` 里零命中） | — |
| Widget UI 代码量 | cover_export **8,587 行 / 25 文件**，其中 CoverStudio 组 **5,925**<br>*（2026-08-29–30 已删除：export_page 738、BatchExportPanel 组 947、MainWindow 嵌入面板机制 479、`VideoExportDialog` 组 4,691、`NetBatch*Dialog` 1,479、`PvBatchCompressionDialog` 393、`PlainCodeEditor` 组 2,272、`QuickShellController` 组 993）* | `src/tools/cover_export` |

### v2 自身新代码里的 Widgets 泄漏（优先清）

| 位置 | 内容 |
|---|---|
| ~~`src/app/qml_ui/export/QmlExportSession.cpp`~~ | ~~`QFileDialog` ×5、`QMessageBox` ×5~~ —— **已清零（2026-08-29）**。`src/app/qml_ui` 全目录现已无 Widgets 对话框 |
| ~~`src/app/qml_ui/QmlEditorPageHost.*`~~ | ~~`QWidget` 宿主表面 + `WindowContainer`~~ —— **宿主机制已删除（2026-08-29）**。文件仍在（231 行），但只记录「当前显示哪一页」，不再收养任何 `QWidget`；`src/app/qml_ui` 全目录无 `QWidget` |
| `src/app/ui/ChartDropOverlay.h:7` | `class ChartDropOverlay final : public QWidget`，被 v2 root window 拖放路径使用 |

`MainView.qml` 的对话框已是 `QtQuick.Dialogs`（非 Widgets），这条不用改。

## 3. 功能对齐缺口

按「v2 用户能不能原生做到」分三类。

### A. 无 QML 实现，入口弹「暂未更新支持」（0 项，已清空）

三处触发点全部补上了真实实现（2026-08-30）。`unavailableFeatureRequested` 在 `*.qml` 里
**已无任何发出方**——`MainToolBar` / `MainMenuCommands` 的信号声明与 `MainView` 的处理器
仍留在树上作为通用机制，尚未删除；若确认不再需要，随阶段 4 一并清掉。

| 功能 | 状态 |
|---|---|
| ~~关于 MiaCode~~ | **已完成（2026-08-30）**：`components/AboutDialog.qml`，事实来自 `QmlUiSettings::aboutInfo()`（版本宏 + `QSysInfo` + UiText）。v1 的图标彩蛋（连点三次）未搬。 |
| ~~音频设置~~ | **已完成（2026-08-30）**：`preview/AudioSettingsDialog.qml` + `QmlAudioSettingsModel`。10 条通道（音量 + 静音）与 Break 星星尾判音开关，写入即应用到运行中的预览并持久化；「设为/恢复本地默认」两个预设按钮保留。**试听已于同日补齐**：模型自带 `QtPreviewSfxRuntime`（首次试听时才创建，页面关闭时释放），220 ms 静默去抖，拖动中不响、松手才响，预览正在播放时不试听。顺带修好两处：主静音会带着其余通道一起变（v1 行为），以及行不再随每次改动整体重建（原先会在拖动中把滑块本身销毁）。 |
| ~~预览设置~~ | **已完成（2026-08-30）**：`preview/PreviewSettingsDialog.qml` + `QmlPreviewSettingsModel`，视频 / 玩法 两个页签。值住在 `MainWindow::previewRenderSettings()` / `setPreviewRenderSetting()`（每个键各自的 canvas setter / 舞台媒体重路由 / 轮廓重算 / muri 重应用，写入即持久化）；文案全部取自 `dialog.render_settings.*` 的 UiText 键，与 v1 同源。v1 的第三个页签 **性能** 只有「预览刷新率」一项，v2 已在 偏好设置 → 性能 里，未在此重复。判定效果显示由 v1 的下拉勾选改为四个并排复选框（四个状态同时可见）。 |

### B. 有入口，但落到 Widget 对话框/页面（8 项中剩 1 项：封面导出）

| 功能 | v2 入口 | 落点 |
|---|---|---|
| ~~延迟校准~~ | `ToolsSidebarPage.qml` → `openLatencyPage()` | **已完成（2026-08-29）**：`LatencyPage.qml` + `QmlLatencyModel`，`LatencySandboxController` 原样复用；`WindowContainer` 与 `QmlEditorPageHost` 的整套 widget 宿主机制已删除 |
| ~~音视频处理~~ | → `openMediaProcessingTools()` | **已完成（2026-08-29）**：`MediaToolsDialog.qml` 启动页 + `PrependBlankDialog.qml` + `PvBatchCompressionDialog.qml`（QML），`QmlMediaToolsModel` 承接；`src/tools/media` 已无 Widgets |
| ~~整谱规范化~~ | → `openNormalizeWholeChart()` | **已完成（2026-08-29）**：`NormalizeOptionsDialog.qml` + `QmlDocumentModel::normalizeChartSelection`，结果作为编辑器事务应用（undo 覆盖）；Widget 对话框与 `DocumentSection::onNormalizeWholeChart` 已删除 |
| ~~Net 批量下载 / 上传~~ | —— | **功能已暂时移除（2026-08-29）**：两个对话框与全部入口删除；引擎（`NetClient`、workers、scanner、diagnostics，均无 Widgets）保留在树上，恢复时直接补 QML 页面 |
| 封面导出 | → `openCoverExport()` | `CoverStudioWindow` 全家（组内 5,925 行；`src/tools/cover_export` 全目录 8,587 行 / 25 文件）。**本批最末一项，所有者决定放到最后做** |
| ~~打包 ZIP~~ | → `packAsZip()` | **已完成（2026-08-29）**：走 `UiRequestService` 选路径与提示、`JobProgressService` + `JobProgressOverlay.qml` 显示进度与取消 |
| ~~偏好设置~~ | `QmlCommandService::openPreferences()` | **已完成（2026-08-29）**：`PreferencesDialog.qml`（界面/编辑器/性能/快捷键四页签，快捷键录制内置为页签而非二级弹窗），`QmlPreferencesModel` 承接；扩展页签与背景功能已删除 |
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
> `Qt6::Widgets` 的最终摘除在 B 类里只剩**封面导出**一项挡着（音视频处理 / 偏好设置 /
> 延迟校准三项已于 2026-08-29 补成 QML）。B 类之外还有 §3.C 的保存/退出确认弹窗、
> `ChartDropOverlay`，以及阶段 4 的 `MainWindow` 与 `src/app/ui/` 辅助件。

### C. 已知功能缺失（v1 有、v2 尚未补）

- [x] **音频设置的试听**（2026-08-30 记录，同日补齐）。`QmlAudioSettingsModel` 自带一个
      `QtPreviewSfxRuntime`：首次试听时才创建（构造会起一条音频工作线程，多数会话根本不开
      这个页面），页面关闭时 `releaseAudition()` 释放，与 v1 对话框本地运行时同一生命周期。
      样本未就绪时先发一次异步加载，由它的 completion 播放等待中的那一声。
- [ ] **预览刷新率的 120 FPS 选项在 v2 未按屏幕刷新率过滤**（2026-08-30 记录）。v1 只在
      检测到 ≥119.5 Hz 时才把 120 FPS 放进菜单，并把「屏幕最大刷新率」一项显示成
      `屏幕最大刷新率 (N Hz)`；v2 偏好设置 → 性能 的 `QmlPreferencesModel::frameRateOptions`
      两者都没做。留到「v1 & v2 文案逐项对比」那一轮一起处理。
- [x] **预览走带的时间条停在 0**（2026-08-30 用户报告，同日修复）。阶段 2 退役
      `QuickShellController` 时，它那条轮询计时器是 shell 唯一的预览状态来源；替代方案
      把底栏那一半改成推送，预览这一半却换成了一个以 `playing_` 为闸门的采样计时器——
      而没有任何东西告诉模型播放已经开始，闸门永不开启，于是时间轴跟随一切正常、走带
      却一直停在 0。现在播放头由**移动它的那个函数**（`applyQtPreviewPosition`）通过
      `shellPreviewPlayheadChanged` 播报，播放标志由**唯一的写入者**
      （`MainWindow::setPreviewPlayingFlag`）播报，模型不再采样任何东西——顺带修好了
      暂停时拖动时间轴/键盘定位不会移动走带滑块的问题。
      漂移守卫：`preview_transport_push_spec`。
- [x] **数值读数不能输入**（2026-08-30 用户要求，同日完成）。v1 的 `EditableValueLabel`
      单击即可就地输入；v2 现在是 `components/EditableValue.qml`，**双击**读数（音量的
      「50%」、预览设置的各滑块、偏好设置字号）变成输入框，提交时钳制到区间并对齐步进，
      与拖动走同一条应用路径。Esc 取消，失焦提交。
- [x] **设置弹窗不能拖动**（2026-08-30 用户要求，同日完成）。`components/DialogDrag.qml`
      把一块透明抓取区挂到样式自己画的标题栏上——外观零改动——首次拖动时释放居中锚点，
      位置在本次会话内跨开关保留，并始终钳制在窗口内。已用于 音频设置 / 预览设置 / 偏好设置。
      **仍是 modal**：遮罩会把预览压暗一半，且弹窗开着时无法按播放。用户只要求「可拖动」，
      所以没动 modal；若要边放边调，需要把这两个预览类弹窗改成 modeless。
- [ ] **导出区间的可视化进度条**（2026-08-29 记录）。导出页的区间只有起止秒数输入框，
      缺少 v1 那条能直观看出区间落在整曲何处的进度条 / 区间条。
- [x] **启动崩溃恢复弹窗仍是 Widgets**（2026-08-29 已修）：`MainWindow.DocumentAutosaveFlow.cpp`
      的三处 `UiDialogs::showMessageBox` 已改走 `UiRequestService`；
      `restoreBackupFilePath` 在确认处切开为 `restoreBackupFilePath` + `applyBackupFile` 续延。
- [x] **保存 / 退出确认弹窗仍是 Widgets**（2026-08-29 记录，用户报告；2026-08-30 已修）。
      关闭流程改为两段式：`Main.qml` 的 `onClosing` 一律先拒绝，再问
      `QmlShellLifecycle::requestClose()`，答案经 `closeDecided` 回来后窗口自己再关一次。
      C++ 侧 `confirmShellClose()` 换成 `requestShellClose(continuation)`，问句之下的所有清理
      原样搬进续延（**顺序契约不变**：破坏性清理只在确认为 true 之后）。提示本体是
      `UiRequestService::requestChoice`——新增的三选一请求，未列出的答案一律解析为 dismissal，
      所以「关掉窗口」永远不会被当成保存或放弃。
      同一轮里另外三处 v2 能走到的 Widgets 弹窗一并处理：**音频拖放建谱**（预览确认 + 未保存
      决策 + 三个结果提示）、**保存失败**的三条、**启动目标缺失**的两条。
      还顺带修了一个双弹窗缺陷：删除难度时 `DifficultyList.qml` 已经问过一次，
      `deleteDifficultyField` 又在后面弹了一个 Widgets 确认——现在 v2 桥接传
      `alreadyConfirmed=true`。以及删掉了没有任何调用方的 `confirmDeleteBookmark`。
      **留在树上的 `showMessageBox`**：`onNewFile` / `onOpenFile` / `openRecentFile` /
      `switchToWelcomePage` / 扩展 `showMessage` / 删除难度的 v1 与扩展路径——全部只挂在隐藏
      `MainWindow` 自己的 File 菜单 `QAction` 或扩展宿主上，QML 外壳没有任何入口能走到，
      随阶段 4 一起消失。同理 `showUnsavedChangesDialog` 与 `maybeSaveBeforeContinue()` 同步版。
- [x] **打开文件的未保存提示是平台原生弹窗**（2026-08-30 记录并修复）。`MainView.qml` 的三个
      `QtQuick.Dialogs.MessageDialog` 在 macOS 上渲染成 NSAlert——不是 Widgets，但同样不是
      应用自己的界面。现在都是 `components/ChoiceDialog.qml`，`src/app/qml_ui` 已无
      `MessageDialog`。
- [x] **控件高亮有记忆**（2026-08-29 报告，同日修复）。所有者已确认所指即 `AppComboBox`。
      成因是 `background.border.color` 读 `activeFocus`：ComboBox 点击即取焦点并保持，
      重开弹窗时焦点被恢复，边框继续是重音色。改用 `visualFocus`——只有键盘到达的焦点才画指示。
      同一属性同步应用到 `AppCheckBox` / `IconButton` 的焦点环。文本输入控件（`AppTextField` /
      `AppTextArea`）**保持 `activeFocus`**：正在输入时边框就该亮着。

### D. 已是 QML 页面，但仍借 Widgets 完成子流程（0 项）

已清零。视频导出页的文件选择与结果提示于 2026-08-29 改走 `UiRequestService` +
`components/UiRequestHost.qml`；`src/app/qml_ui` 全目录再无 Widgets 对话框
（`QmlShortcutModel.h` 里唯一的 `QtWidgets` 字样是一条注释）。

### E. 影子 Widget 状态（无用户入口，但仍在链上）

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

### 4.0 排期（2026-08-29 所有者指定）

**本批**（做完一项提交一次）：

1. ~~阶段 1 + 阶段 2~~ —— **已完成（2026-08-30）**。隐藏 `PlainCodeEditor` 已删、
   `qml_document_lifecycle_contract_spec` 已绿、25 个 `shellController` 消费点已迁往
   `QmlTimelineModel` / `QmlPreviewModel` / `QmlShellLifecycle`、`QuickShellController`
   与轮询已退役。唯一留在阶段 2 里的是 `TimelineQuickModel` 所有权，**所有者已决定延后**。
2. ~~关于 MiaCode / 音频设置 / 预览设置~~ —— **已完成（2026-08-30）**，三个「暂未更新支持」入口全部补上真实实现；`unavailableFeatureDialog` 在 A 类已无触发点。
3. ~~侧边栏书签折叠重新设计~~ —— **已完成（2026-08-30）**。右侧那个独立折叠按钮已删除，
   难度行本身就是折叠控件：点击非当前难度＝切换难度（行为不变），点击**当前**难度＝
   展开 / 收起它的书签；折叠指示器在**左侧**，与「难度」分组标题的箭头对齐，
   槽位恒占位所以各行色块仍然对齐。**只有当前难度显示书签**——行既然是折叠控件，
   非当前行按下去是切换而不是折叠，留在别的难度上的展开态就会是一个屏幕上没有东西能收起的列表。
   漂移守卫：`qml_editor_controller_spec` 新增一条断言（`foldsBookmarks` 存在、`foldButton` 不存在）。
4. ~~保存 / 退出确认弹窗改为 QML~~ —— **已完成（2026-08-30）**。关闭流程改为两段式，
   未保存提示走 `UiRequestService::requestChoice`（新增的三选一请求）+ `ChoiceDialog.qml`；
   §3.C 列出的调用点里**所有 v2 能走到的**都已迁移，v1 菜单专属的那些留给阶段 4。

**本批四项全部完成。** 下一轮见下。

**批次中途插入并已完成（2026-08-30，用户报告 / 要求）**：预览走带时间条停在 0（回归，见 §3.C）、
数值读数双击输入、设置弹窗可拖动。

**下一轮**（难度更高，本批完成后才开始）：

1. 导出区间的可视化进度条。
2. **v1 / v2 文案逐项比对**。v1 文案经过多轮迭代，v2 重构时出现大量偏差，
   怀疑多语言模块（`UiText`）没有完整搬迁。若属实需要重建，并尽可能恢复原文案。
3. 封面导出功能重构。


### 阶段 1（收尾）—— 文档域单一所有者

已落地：`ChartWorkspace`、`ChartWorkspaceFileService`、`AnalysisService`（`src/app/v2/`），生产 QML 的文档事务、save-point dirty、revision-stamped 分析快照均不再以 `MainWindow` 为真相。

剩余：

- [x] 删除隐藏 `PlainCodeEditor` 与第二份文档副本（2026-08-30）。`src/editor/PlainCodeEditor.*`
      与 `BracketCompletionPopup.*` 已删除，应用不再链接任何 Widgets 文本编辑器。
      过程中修好了两个原本已坏的功能：扩展 `editor/*` API 与 14 个谱面变换命令，
      两者此前都作用在一个从未被 QML 选区同步过的隐藏光标上。
- [x] 修复 `qml_document_lifecycle_contract_spec`（2026-08-29，见 §7.1）。
- **阶段 1 除延后项外已收尾。**
- [ ] **增量时间轴解析已实际失效**（2026-08-30 记录）。`applyTimelineQuickChange` 只由隐藏编辑器的
      `contentsChange` 驱动，而每次 v2 写入都经 `setEditorText` 抑制脏跟踪，所以它从未运行——
      v2 的每一次编辑都是全量重建。隐藏编辑器删除后这条路彻底断了。重新接上的正确位置是工作区提交
      （它知道确切编辑范围），属于阶段 2 `TimelineQuickModel` 所有权那一项，**所有者已决定继续延后**。

### 阶段 2 —— `PreviewSession` / `TimelineSession`，退役 `QuickShellController`

> **2026-08-30 所有者决定：`TimelineQuickModel` 所有权迁移继续延后。**
> 阶段 2 的其余部分（`shellController.*` 消费点迁移、MainWindow 改推送、删 `refreshTimer_`
> 与 `src/app/quick_shell/`）按「一次做到底」推进。

- [x] 把 25 个 `shellController.*` QML 消费点搬走（2026-08-30）。落点是三个对象而非两个：
      `QmlTimelineModel`（时间轴与底栏页签）、`QmlPreviewModel`（原有对象，接管预览剩余项：
      画布比例、播放切换、速度步进、交互日志）、`QmlShellLifecycle`（根窗口关闭契约）。
      其中三处不需要新对象：`exportPageActive` 与 QML 已有的 `pages.activePageId === "export"`
      是同一个条件；`workspacePanelsSwapped` 就是 `preferencesModel.previewOnLeft`。
- [x] **轮询消失**（2026-08-30）。`refreshTimer_` 每隔 200ms/33ms 拉 ~25 个值、无论是否播放。
      现在离散状态由 `MainWindow::shellPresentationChanged` 推送。
      *更正（2026-08-30 同日）：这一条最初留了一个「播放时钟仍需采样、定时器只在播放期间运行」
      的例外，而那个例外本身就是缺陷——闸门 `playing_` 从来没有推送方，定时器从未启动，走带
      一直停在 0（见 §3.C）。播放头改由 `applyQtPreviewPosition` 通过
      `shellPreviewPlayheadChanged` 推送，采样定时器已删除，`QmlPreviewModel` 现在不采样任何东西。
      漂移守卫 `preview_transport_push_spec` 会拒绝把定时器放回去。*
- [ ] `TimelineQuickModel` 所有权从 `MainWindow` 迁到 `TimelineSession`。**已延后。**
      *更正（2026-08-30）：此前记的「增量解析本身已完成，剩下的只是所有权」在实现上成立、
      在运行上不成立。`applyTextChange(QString,…)` 确实取代了 `applyContentsChange(QTextDocument*)`，
      但唯一的驱动方是隐藏编辑器的 `contentsChange`，而它被 `setEditorText` 的抑制挡住，
      所以 v2 从未走过增量路径——每次编辑都是 `scheduleTimelineRefresh()` 全量重建。
      隐藏编辑器删除后连这条驱动也没有了。要恢复增量，得改由工作区提交驱动（它知道确切编辑范围），
      这正是本项要做的事，不只是搬所有权。*
- **完成标志**：~~`refreshTimer_` 消失~~✅；~~QML 不再出现 `shellController`~~✅；
      `src/app/quick_shell/` 部分删除——`QuickShellController.{h,cpp}`（866 行）与
      `QuickShellContracts.h`（127 行，三个抽象基类）已删；目录里剩下的
      `QuickShellPreviewCompositeSurface.*` / `QuickShellPopupPosition.h` /
      `QuickShellKeyboardActivation.h` / `QuickShellPreviewSurfacePolicy.h` 是预览合成表面的
      平台管道，与外壳控制器无关，随阶段 4 的 `MainWindow` 一并处理。

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

### 0b（延后，未取消）/ 0c（已改判并完成）

- [ ] 0b 扩展宿主删除 —— 保持现状。
- [x] 0c 偏好设置 / 延迟检测 / 音视频处理页 —— **三页均已补成 QML 原生页（2026-08-29）**。
      ~~架构文档第 8、10 节仍记录着相反的删除决定，需改齐。~~ **已改齐（2026-08-30）**：
      架构文档第 8 节的 0c 行改为 ✅「不删，补成 QML」，阶段 1 / 2 行同步标记完成；
      第 10 节「范围裁剪的代价」改为记录**实际**删除项（Net 暂移除、应用背景、扩展页签），
      并写明「哪怕功能会缺失也要做」那条授权已被所有者收回。

## 5. 已完成（事实登记，不再逐条展开）

- 阶段 0a：v1 QuickShell 外壳、入口、原生表面再宿主、皮肤切换（`resolveUiSkin`）全部删除，`--ui=v1` / `MIACODE_UI_SKIN` 不复存在。
- 文档域：`ChartWorkspace` + `ChartWorkspaceFileService` + `AnalysisService` 接管文档事务、save-point dirty 与 revision 分析快照。
- 编辑器：共享 `SimaiTextEditPolicy`、补全、书签、查找替换、诊断跳转、语法高亮、快捷键（`QmlShortcutModel` 复用 `ShortcutRegistry`）。
- 同步链：`EditorSyncController` 统一播放跟随、离散导航、编辑器上下文与触控创作，全部经队列边界投递；`TimelineView`（QWidget 时间轴，约 3,470 行）已删除。
- 时间轴/底栏：缩放、亮度、follow-code、validation / Muri 三 tab、底栏高度联动、工作区面板互换。
- 预览：三态渲染模式（`PreviewRenderModeMenu.qml`）、负时间传输条、surface 互斥生命周期、统计投影去热路径。
- 导出：`ExportVideoPage.qml` + `QmlExportSession`，含片头音文件与 0–200% 音量，写入单个/批量 snapshot 与 worker。
- ChartDrop：root window 登记 + 事件过滤器 + overlay 同步。
- 行高亮：`ChromeRow.qml`（2026-08-29）把「HoverChrome 背景 + 内容内缩」封成一个类型，13 处
  `background: HoverChrome` 调用点迁移完毕。剩余直用者只有 `AppMenuItem`（必须是 MenuItem）
  与四个纯图标按钮（内容居中，没有会贴边的文字）。
- 编辑器外观：`EditorTextStyle`（QML 类型）把 行距 落到 TextArea 的 QTextDocument 上（每个
  块的 bottomMargin），`QmlUiSettings::setEditorAppearance` 让 字号 / 行距 都能实时到达 QML
  编辑器——此前两者都只写到了 v2 已不显示的 Widgets 编辑器。
- 设置页（2026-08-30）：关于 MiaCode、音频设置（含试听）、预览设置三页原生化，A 类清零。
  混音十条通道与预览的二十项都以**表**的形式存在（`QmlAudioSettingsModel` 的 `ChannelSpec`、
  `MainWindow::previewRenderSettings()` 的键值对），而不是逐个属性——Widgets 对话框正是因为
  逐个手写才让滑块与静音按钮各自接线并走样。
- 共享表单控件（2026-08-30）：`LabeledCombo` / `LabeledSlider` 从偏好设置里的内联组件提升为
  组件；`EditableValue` 补回 v1 `EditableValueLabel` 的就地输入（改为双击）；`DialogDrag`
  让设置弹窗可以拖开，且不改变任何外观。
- 推送而非轮询（2026-08-30）：`shellPresentationChanged`（离散壳状态）与
  `shellPreviewPlayheadChanged`（播放头）取代了 `QuickShellController` 的轮询定时器，
  `QmlPreviewModel` 不再采样任何东西；`preview_transport_push_spec` 钉住这条契约。

## 6. 验收规则

1. 每个域搬迁**之前**先补该域的契约回归，确认红态再搬（架构文档第 9 节）。
2. QML 回归驱动**真实组件与真实事件**，不用源码字符串扫描代替。
   —— §7.1 正是源码扫描式契约的失效案例。
3. `QT_QPA_PLATFORM=offscreen` 在本机会触发既有 macOS 平台插件崩溃，**不能**用作桌面视觉/输入验收。
4. 原生桌面验收按平台分别记录；macOS 通过不能推断 Windows 通过。

## 7. 遗留问题与更新后待复核

### 7.0 应用背景功能已移除（2026-08-29 决定，已执行）

- **证据**：`AppBackgroundPainter` 构造在 `MainWindow` 上（`FrameBootstrap.cpp:1873`），而 `MainWindow`
  在 `FrameBootstrap.cpp:117` 无条件 `setAttribute(Qt::WA_DontShowOnScreen)`。painter 只被
  `setSettings()` 调用过，画的是 toolbar / statusbar / tabwidget 等 Widget chrome。
  QML 外壳的配色来自 `theme/Theme.qml` 的硬编码色值；`src/app/qml_ui` 全目录**没有任何**
  文件引用 app background 设置。
- **含义**：该页签 410 行 UI 加 14 个 overlay alpha 旋钮，配置的是一个画进不可见窗口的 painter。
- **处置**：所有者决定一并移除。`AppBackgroundPainter` / `AppBackgroundSettings`、六个
  `AppBackgroundSurface*` widget 包装、`UiTheme` 的 overlay-alpha 体系与背景页签全部删除
  （净 −1,711 行）。`PlainCodeEditor` 的三处背景绘制分支随之简化为直接填色。
  恢复该功能需要在 QML 外壳里重新实现，不是回滚这次删除。

**本章是交接面。** 每条写明：问题是什么、当前状态、以及在 `117a76a1` 之后**必须重新核实什么**。
未列为"已复核"的条目，一律不得由构建、CTest 或历史记录推断为通过。

### 7.1 `qml_document_lifecycle_contract_spec` 已恢复绿（2026-08-29 已处置）

- **现象**：断言 `initial, navigation, and follow identities stay on the committed workspace revision` 失败。
- **原因**：该断言用源码字符串扫描，要求 `appliedQmlWorkspaceRevision_ > 0` 同时出现在
  `MainWindow.PreviewTimelineFlow.cpp` 与 `MainWindow.ValidationFlow.cpp`。`117a76a1` 重写了这两个文件：
  前者改为把 revision 直接交给 `editorSyncController_->requestNavigation(...)`（`PreviewTimelineFlow.cpp:1518`），
  后者的诊断跳转整段移走，只剩 `clearPreviewFollowDecoration()` 里的 `publishFollow`。
- **已复核（2026-08-29）**：门控确实完整地在 `EditorSyncController` 里，而且比原来更强——
  每条路径在**提交时**和**投递时**各查一次 `readinessAccepts`，因此在提交后、队列跑完前
  失效的请求不会被投递，而是以 `applied=false` 收尾。
- **处置**：新增 `editor_sync_controller_spec`（`src/tools/v2/EditorSyncControllerSpec.cpp`，
  只链 `Qt6::Core` + `Qt6::Test`），六项行为断言直接驱动真实对象：未就绪拒绝、
  过期 revision / 不同难度拒绝、投递前失效则不投递并回报未应用、隐藏编辑器会结清挂起导航、
  元数据模式不接受谱面导航、caret / 指针 / 触控锚点 / 预览 seek 共用同一门控且相同 caret 不重复发布、
  follow 作为投影原样携带发布方身份（这正是本轮 代码跟随 缺陷的落点）。
- 契约 spec 里那条断言改为只主张源码层面能看见的一半：导航与 follow 两个发布方都读
  `appliedQmlWorkspaceRevision_`，且 follow 不再读验证快照的 revision。
- **CTest 基线**（2026-08-30 更新）：**82 项**、唯一预期红仍是 `qtavplayer_platform_spec`，
  干净一轮是 **81/82**。这一轮新增 `editor_sync_controller_spec`、`preview_transport_push_spec`。
  数字会继续变；读的时候以本次运行的总数为准，不要以记忆中的数字判断。

### 7.2 QV4 GC 崩溃（Windows `0xC0000005` @ `Qt6Qml.dll+0x169A39`）

- 根因与复现见 [QML_RUNTIME_CRASH_AUDIT_ZH.md](../../audit/QML_RUNTIME_CRASH_AUDIT_ZH.md)。
- 审计推荐的 A/B/C 已落地：A —— `centerCursorInView()` 改为单一可合并的 `cursorCenterTimer.restart()`（`SourceEditor.qml:214`）；
  B —— `CompletionPopup.qml` 的光标/布局 `Connections` 已 `enabled: root.visible`；
  C —— `EditorSyncController` 队列边界。
- **待复核（D 项，未执行）**：`QV4_MM_AGGRESSIVE_GC=1` 下，代码跟随开启完整播放 10 轮、跨多 token、反复开关跟随、暂停/继续/停止/重播、弹窗开与关两种状态；Windows Release 与 Linux Release 各一轮。**这是本次重构最关键的验收，缺它则崩溃只能算"推测已修"。**

### 7.-1 GUI 验收结论（2026-08-29，所有者在 macOS 上走查）

所有者对本轮构建做了一次原生桌面走查，并判定：**除音视频处理工具外，全部判定为 GUI 验收通过。**

- **通过**：§7.3 的五项 + 新语义、§7.4 的底栏/时间轴菜单/渲染模式/面板互换/菜单栏位置、
  §7.6 的脏文档撤销联动、§7.9 的手工回归清单，以及 2026-08-29 两轮修复的可见结果
  （行高亮覆盖、对话框统一页脚与样式、快捷键录制、规范化下拉、侧边栏难度色块与书签折叠、
  语法列不再重叠、行距/字号实时生效、代码跟随在切换难度后仍然工作）。
- **未验收**：**音视频处理工具**（`MediaToolsDialog` / `PrependBlankDialog` /
  `PvBatchCompressionDialog` 四条 ffmpeg 流程 + PV 批量队列）。保持未验收状态。
- **这份结论覆盖不到的**：走查发生在 2026-08-29，**此后落地的一切都不在其范围内**——
  逐项清单见 §7.-0。另外本次走查在 macOS 上进行，所以
  ① §7.2 的 D 项（`QV4_MM_AGGRESSIVE_GC=1` 压测，要求 Windows Release 与 Linux Release 各一轮）
  **不因此结论而关闭**；
  ② §7.10 的「Windows 侧整体从未验证」同样不变。
  这两条继续按原状挂着，不得由本条推断为通过。

### 7.-0 2026-08-30 之后新增，全部**未验收**

§7.-1 的走查发生在 2026-08-29。此后落地的东西没有任何一项被在原生桌面上看过，
不得由构建或 CTest 推断为通过：

- [ ] **关于 MiaCode**：版本号 / 平台三元组 / 构建类型是否正确；平台与构建类型两行可选中。
- [ ] **音频设置**：十条通道的音量与静音、Break 星星尾判音开关、两个本地预设按钮；
      **试听**（松手出声、拖动中安静、预览播放时不试听）；主静音是否带动其余九行；
      拖动滑块是否不再中途中断。
- [ ] **预览设置**：视频 / 玩法两个页签共 20 项是否即时改变预览。
- [ ] **走带时间条**：播放时是否跟走；**暂停时**拖时间轴 / 键盘定位是否也移动滑块。
- [ ] **双击输入数值**：音量「50%」、预览设置各滑块、偏好设置字号。
- [ ] **弹窗拖动**：音频设置 / 预览设置 / 偏好设置拖标题栏；关掉再开是否保留位置；
      窗口缩小后是否仍被钳制在窗口内。
- [ ] **谱面变换的菜单与右键菜单入口**（2026-08-29 落地，走查在其之前，未覆盖）。
- [x] **侧边栏书签折叠**（2026-08-30 所有者验收通过）。
- [ ] **关闭确认**：脏文档关窗 → 保存 / 放弃 / 取消三条路径；取消后窗口与所有面板是否完好
      （破坏性清理必须只在确认之后发生）；Escape 与标题栏关闭按钮是否都等于取消。
- [ ] **打开文件的未保存提示**：三条路径 + 未命名文档选「保存」是否转到另存为。
- [ ] **音频拖放建谱**：预览确认 → 未保存决策 → 结果提示（失败 / 单个成功后询问切换 / 部分失败）。
- [ ] **删除难度只弹一次**（此前会连弹两个，第二个是 Widgets）。
- [ ] **保存失败 / 启动目标缺失**的提示是否为应用自己的弹窗。

### 7.3 二阶段五项桌面验收已因同步链重写而失效

- 2026-08-24 记为通过的五项（`ca943a82` 暂停跟随装饰、`2a27630d` 书签/右键菜单、`75c89635` 命令修饰键、`f4251ca0` IME 提交递归、`f451ae09` Command+点击 seek，以及 `5ee23d38` 窄窗口 completion popup）针对的是旧跟随/导航链。
- `117a76a1` 把整条链换成值对象 + 队列投递，并重写了 caret 显隐、系统周期闪烁与播放中暂停语义；上游 merge 提交自述「Native GUI acceptance remains pending」。
- **已复核（2026-08-29，macOS，见 §7.-1）**：五项与新语义随本轮走查一并判定通过——普通点击只移动 caret 不居中；Ctrl/Command+点击先暂停再 seek 并居中 playhead；播放中指针按下立即暂停；播放期普通 caret 隐藏、跟随光标显示，暂停后由焦点恢复；caret 闪烁跟随 `Application.styleHints.cursorFlashTime`。Windows 侧仍未验证。

### 7.4 本次 UI 重构无任何 GUI 验收记录

`04401157` 合入的这批改动尚未被任何人在原生桌面上看过：

- 底栏（时间轴）UI 重构、`TimelineZoomMenu` / `TimelineBrightnessMenu`
- `PreviewRenderModeMenu`（sticky popup，非 `Menu`）
- 时间轴高度随底栏拖拽联动、工作区面板互换
- macOS 自定义菜单栏位置修正、响应式预览尺寸修正
- 新增共享控件 `AppCheckBox`、`AppDropDownButton`

**已复核（2026-08-29，macOS，见 §7.-1）**。窄窗口 / 1280×720 / 浅深色 / 中英文 / Large system font 的逐项组合未单独留记录，若后续出现相关报告按新问题处理。

### 7.5 底栏高度配置键变更，无迁移

`QmlUiSettings` 的 `ui/bottomPanelHeight`（像素）已换成 `ui/bottomPanelHeightRatio`（比例，默认 0.35，`QmlUiSettings.cpp:33-36`），**没有写迁移**。老用户升级后底栏高度会回到默认值。
**待复核**：确认这是有意为之；若否，补一次性迁移。

### 7.6 撤销栈 dirty 联动（已修，待 GUI 复核）

dirty 真相已迁到 `ChartWorkspace` 的完整文档 save point，`Ctrl+Z` / `Ctrl+Y` 回到已打开或已保存内容会自动回 clean。
**已复核（2026-08-29，macOS，见 §7.-1）**。

### 7.7 切换文档后 PV 异常（延后）

多次复现失败，需求延后。埋点保留在树上（`editor/document_replaced`、`editor/document_shown`）。
**待复核**：仅在再次复现时重启排查，不预先投入。

### 7.8 播放期高位内存平台（延后）

现有证据是"跃升后稳定"（private memory 约 957 MB 平台，第二段播放 1,051–1,064 MB），不是持续泄漏。
**待复核**：阶段 2 迁移 Preview/Timeline **之前**先做分层取证（QtAVPlayer/D3D11VA 帧池、QML/Qt Quick、私有堆），不得先验归因于 preview texture cache。

### 7.9 手工回归清单（2026-08-29 macOS 走查判定通过，见 §7.-1）

- [x] 脏文档、播放中、导出任务运行时的关窗流程与 v1 一致。
- [x] 设备热插拔暂停后，下一次播放走 cold Prepare。
- [x] play / pause / seek 按 completion 更新界面状态。
- [x] EraseByArea、烟花时长、BGM 过轨静音行为正确。
- [x] 音频拖放建谱与 ChartDrop 覆盖层（含 cancel）行为正确。
- [x] QML 导出片头音文件与音量：原生试听 + 成片确认。
- [x] `d534b393` 的 bookmark / touch input 改动 GUI 回归。
- [x] root 拖放、播放中关闭。
- [ ] **音视频处理工具（未验收）**：采样率 / 提取音频 / 前置空白 / 音量归一化四条 ffmpeg 流程，
      以及 PV 批量压制队列的进度、取消与失败呈现。这是唯一还没被走查覆盖的 v2 页面。

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
| 页面宿主（待删） | `src/app/qml_ui/QmlEditorPageHost.*`（已不收养 `QWidget`，只剩页面记账） |
| 主壳 | `src/app/qml_ui/Main.qml`、`layout/MainView.qml` |
| 设置页 | `preview/AudioSettingsDialog.qml` + `QmlAudioSettingsModel.*`、`preview/PreviewSettingsDialog.qml` + `QmlPreviewSettingsModel.*`、`preferences/PreferencesDialog.qml` + `QmlPreferencesModel.*` |
| 表单控件 | `components/LabeledCombo.qml`、`LabeledSlider.qml`、`EditableValue.qml`、`DialogDrag.qml` |
| 开发索引 | `.agents/skills/miacode-dev-guide/references/feature-index.md` |

## 9. 更新规则

1. 条目状态变化时**同时**更新本文与架构文档第 8 节的阶段表。
2. 架构、入口或跨模块契约变化时，同步更新仓库指南（`feature-index.md` / `cross-chain-linkage.md`）。
3. 原生桌面验收结果写进第 7 章对应条目，注明平台与日期；构建与 CTest 不能替代。
4. 不得重新引入 v1 shell；恢复被裁剪功能须单独作产品决策。
5. 新代码不得引入 `Qt6::Widgets` 类型——那是本清单唯一的终点条件。
