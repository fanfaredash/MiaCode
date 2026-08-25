# QML UI v2 一阶段 Todo

> 历史实施与验收清单。架构或入口变化时，同步更新目标架构文档与仓库指南。
>
> **2026-08-24 起本文的范围被架构重设计取代。** 本文保留为"v2 外壳驱动隐藏 MainWindow"形态的
> 实施与验收历史；目标架构和后续工作以
> [QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md](QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md) 为准。未完成项只作为
> 风险与验收记录，在迁移到对应域时处理，不再按本文单独排期。
>
> **2026-08-25 校正：** 阶段 0a 已删除 v1 QuickShell 的入口、bootstrap、原生表面再宿主和全部壳层
> QML；`--ui=v1`、`MIACODE_UI_SKIN` 已不存在。`QuickShellController` 仍由 v2 暂时使用，按目标架构
> 在阶段 2 退役。
>
> **同日排期调整：** 阶段 0b（扩展宿主）和 0c（被裁剪页面）均延后；`TimelineQuickModel` 正文增量更新
> 与播放期高位内存平台也延后。QML 功能缺口的范围和入口策略见
> [QML_UI_V2_CAPABILITY_GAP_RESEARCH_ZH.md](QML_UI_V2_CAPABILITY_GAP_RESEARCH_ZH.md)：保留入口，但对尚无
> QML 实现的功能明确提示“暂未更新支持”。

## 当前基线

- 分支：`feature/qml-ui`
- 上游：阶段 0a 已完成（`85d6dc78`）；最近 v2 性能修复为 `3a0ce116`
- 默认界面：`QmlUiBootstrap`（v2）
- 构建目录：`build/`
- 原型参考：`../MashiroEditor/src/ui`

## 范围与契约

- v2 方向为纯 QML 主壳、C++ 域服务和已有 `QQuickItem` 时间轴/预览。
- 视频导出页已经使用纯 QML `ExportVideoPage.qml`，由 `QmlExportSession` 提供业务状态和操作。
- `QmlEditorPageHost` 负责全页导航，并通过局部 `WindowContainer` 宿主 v1 `LatencyDetectionPage`。
- v2 共享隐藏的 `MainWindow` 与 `QuickShellController(surfaceHost=nullptr)`。
- 工作区模式由 `MainWindow` 切换，v2 从 `QuickShellController` 读取底栏显隐、预览画幅和导出页状态。
- v2 时间轴保留 `TimelineQuickModel` → `TimelineQuickStateBridge` → `TimelineQuickItem` 的 QSG 渲染链，重点补齐 QML 主壳的交互与编辑器接入。
- 实时预览和视频导出共用进程内 QSG 渲染路径。
- v1 壳层、入口和表面再宿主已移除；当前 QML 主壳仍通过隐藏的 `MainWindow` 与
  `QuickShellController` 取状态。这是阶段 1/2 的迁移边界，不应再扩展这套兼容胶水。

## 已完成

### 启动与壳层

- [x] 删除 v1 QuickShell 文件和入口；应用收口为单一 v2 QML 启动路径（阶段 0a，`85d6dc78`）。
- [x] 接入 `src/app/qml_ui/`、`QmlUiBootstrap`、`MiaCode.UI` 模块与皮肤切换入口。
- [x] v2 暂以隐藏 `MainWindow` 和无原生 surface host 的 `QuickShellController` 运行；该兼容层在阶段 1/2
      迁移后删除。
- [x] 关窗流程接入 `confirmClose` → `notifyRootCloseAccepted` → `preparePreviewForShutdown`。
- [x] Windows 客户区标题栏接入 `QmlUiWindowChrome`，品牌图使用 `resources/icons/app.png`。
- [x] 设置入口调用现有 `MainWindow::onPreferences()`。

### 预览与时间轴

- [x] 预览接入 `QuickShellPreviewSurface`，时间轴完成 `TimelineQuickItem` 基础渲染接入。
- [x] v2 根窗口显示后提交 Quick UI ready，释放 PV/BG 首次加载门闩。
- [x] 普通预览和全屏预览移除人工画面缩进。
- [x] `QmlPreviewModel::renderMode` 接入三态切换、工具栏文字和预览标题。
- [x] 预览传输条接入负时间下界和完整 scrub 生命周期。
- [x] 清理 v2 QML 中已删除渲染后端的状态与资源。

### 页面与控件

- [x] 视频导出中心迁移为 `ExportVideoPage.qml` + `QmlExportSession`。
- [x] Latency 页面通过编辑区局部 `WindowContainer` 接入。
- [x] 导出工作区状态接入底栏显隐、预览画幅和导出模式。
- [x] 工具侧栏接入音视频处理、规范化、Net、批量导出、封面导出和打包入口。
- [x] 完成 `AppTextField`、`AppTextArea`、`AppButton`、`AppComboBox`、`AppSlider`、`AppMenu*` 共享皮肤。
- [x] 完成 `HoverChrome`、`NavRow`、`AppSwitch`、IconButton glyph 的悬停和选中样式。
- [x] 标题栏菜单在窄窗口中折叠到溢出菜单，文档标题保持居中。
- [x] Activity Bar、小窗口紧凑覆盖层、QML 多标签编辑器、元数据双视图、谱面字段侧栏和底栏 Tab 已接入。

### 文档模型与诊断

- [x] `QmlDocumentModel` 通过 `MainWindow.DocumentBridge.cpp` 的公开查询与操作访问文档，已移除对 `MainWindow` 私有字段的 friend 访问。
- [x] v2 启动目标改用公开 `openStartupTarget()`，已移除 `friend class QmlUiBootstrap`。
- [x] `DocumentValidationSnapshot` 将现有验证缓存投影为 QML 数据，包含问题位置、显式严重度、错误数、警告数和 strict 解析物件数。
- [x] 同步检查、后台分析和缓存清理统一发布 `documentValidationChanged`；底栏列表、问题跳转和编辑器波浪线共用 `syntaxIssues`。

### 一阶段 P0 文档事务与验证收尾（自动验证，2026-08-24）

- [x] 文档状态发布已收口为同一份 `activeDifficultyId`、dirty、`documentRevision`、
      `validationRevision`、available/pending 投影。`qml_document_projection_spec` 验证的是该
      投影/预检状态契约：输入 revision 一致时的状态、缓存 revision/难度/谱面/timing 不匹配时隐藏
      旧诊断并标为 pending，以及完整源码预检失败时保留 committed source/revision 与全局行列诊断。
      它不直接调用正文、`&first`、额外 timing、等级/谱师或完整源码替换的真实 QML/MainWindow 变更路径。
- [x] 完整源码替换的生产路径先对全部难度作严格预检；失败时保留已提交文档和 revision，仅将尝试源码与
      带全局行列位置的诊断投影给编辑器。通过后才加载文档并请求时间轴刷新（本次为源码路由审阅，非 GUI
      端到端执行）。`simai_document_spec` 同时覆盖 `&first` 可保存序列化、独立/难度谱师及
      `&clock_count` 的 load/save round-trip。
- [x] Release 证据：`cmake --build build-devtools --config Release --parallel 2` 成功；
      `ctest -C Release -R 'qml_document_projection_spec|simai_document_spec|timeline_model_spec|muri_spec|plain_code_editor_spec'`
      5/5 通过。该组覆盖文档投影/诊断、序列化、时间轴模型、Muri 和编辑器回归。
- [x] `batch_export_panel_spec` 的开发工具目标显式链接其 `VideoExportSettings` 与
      `VideoExportRuntimePolicy` 依赖；编辑器规格以确定性的 popup press/release 路径覆盖候选项提交。
      Cocoa 原生焦点转移与光标几何不属于该合成事件规格，保留为 GUI 手工验证。

### 一阶段 P0 手工验证矩阵（仍待执行）

| 场景 | 自动证据 | 本机手工状态 |
|------|----------|--------------|
| 正文、`&first`、额外 timing、等级/谱师、完整源码变更 | `qml_document_projection_spec` 断言投影/预检 revision 和诊断状态；源码审阅确认 accepted full-source transaction 才请求时间轴刷新 | 未执行真实变更路径或 GUI 观察；预览未启动 |
| 打开、编辑、切换难度、保存/另存为、放弃 | 静态路由审阅：QML `openFile/save/saveAs/discardChanges` → 窄 MainWindow API；`simai_document_spec` 覆盖序列化 round-trip | 未执行真实文件或对话框流程，不能当作端到端通过 |
| QML 诊断列表、时间轴与预览刷新 | 文档投影与 `timeline_model_spec` 通过；源码审阅确认 timing 变更刷新 waveform/metadata 并 schedule timeline | 未执行 GUI/预览观察，未验证实际画面或播放 |
| Windows/macOS 视觉、音频、拖放 | 不适用 | 未执行；不得由本次 macOS 自动构建推断通过 |

### 二阶段编辑器与联动收口（自动验证，2026-08-24）

- [x] 编辑输入的纯策略、QML controller 与 widget adapter 共用同一套半角、括号、hold 和补全事务；
      QML 路径将 IME commit、粘贴、选择区替换、Replace All、书签操作和 undo/redo 记入 controller
      事务历史。`simai_text_edit_policy_spec`、`plain_code_editor_spec`、
      `simai_completion_catalog_spec` 与 `qml_editor_controller_spec` 覆盖该边界。
- [x] `SourceEditor.qml` 的 completion popup 保持无焦点，键盘仍由编辑器路由；查找替换、书签的
      创建/删除/重命名、诊断跳转和 caret→timeline 发布均经过窄 controller/document 接口。
- [x] `QmlTouchPadAuthoringBridge` 仅在 editor 有焦点、非 IME composition、难度一致且 caret revision
      与文档 revision 一致时接受 Ctrl+touch 创作；`touch_pad_authoring_state_spec` 覆盖有效与拒绝路径。
- [x] 静态可访问性检查：窗口按钮、书签行号 gutter 与书签重命名输入都提供 `Accessible` 元数据；
      gutter 可经 Tab 聚焦，Enter/Ctrl+Shift+B/Delete/F2 和右键菜单均有对应操作。completion popup
      不获取焦点，Escape 关闭其会话，避免诊断或候选项抢占编辑器焦点。
- [x] Release 针对性 CTest（Qt 6.10.2，2026-08-24）：
      `ctest --test-dir build -C Release -R 'qml_.*_spec|simai_text_edit_policy_spec|plain_code_editor_spec|simai_completion_catalog_spec|timeline_model_spec|timeline_marker_offset_spec|muri_spec|touch_pad_authoring_state_spec' --output-on-failure`
      为 11/11 通过。

### 二阶段桌面手工回归矩阵（五项验收门槛已通过，其余仍待执行）

本机以 `QT_QPA_PLATFORM=offscreen` 启动 QML 桌面壳会触发既有 macOS 原生主题/平台插件崩溃，不能把
offscreen 自动化结果当成桌面视觉或输入验证。以下状态来自 2026-08-24 的原生桌面验收与修复后复验；
未列为通过的项目不得由构建或 CTest 推断为通过。**通过仅代表 macOS 本次观察，Windows 侧尚未验证。**

| 流程 | 自动/静态证据 | 原生 GUI 状态（2026-08-24 修复轮后） |
|---|---|---|
| 1280×720、窄窗口、Large system font、浅/深色、中文/英文、错误/警告非仅颜色 | QML 静态审阅；主题令牌与文本/图标语义已接入；`qml_editor_controller_spec` 载入真实 `CompletionPopup.qml` 断言高亮、宽度与锚点翻转 | completion popup 与窄窗口 Timeline 缩放已复验通过（`5ee23d38`）。字号/浅深色/多语言的完整矩阵仍未逐项走过。 |
| 连续编辑与分析 | revision 投影、`qml_document_projection_spec`、`qml_analysis_model_spec` | Validation 上一轮通过；Muri 未重新观察，不能从自动测试推断桌面通过。 |
| timeline 缩放、亮度、follow、拖拽/滚轮/播放与三 tab | `timeline_model_spec`、`timeline_marker_offset_spec`；暂停跟随装饰的路由与 QML 渲染回归在 `qml_editor_controller_spec` | 通过：暂停跟随（`ca943a82`）、缩放/命中/四个 follow 状态均已复验。 |
| IME、半角、括号、hold、查找替换、书签、undo/redo、诊断跳转 | 编辑器四项 specs；真实 `TextArea` + 真实 `QKeyEvent` 的命令修饰键回归；真实 `QInputMethodEvent` 的再入提交回归 | 通过：书签、正文右键菜单（`2a27630d`）、命令修饰键写入字面字符（`75c89635`）、IME 提交递归复制（`f4251ca0`，由 `--debug` 日志确证 depth 630 / 631 次插入）。**撤销栈另有问题**，见下方“待处理功能缺口”。 |
| caret/selection→timeline 与 Ctrl+touch 创作 gate | `qml_editor_controller_spec`、`touch_pad_authoring_state_spec` | 通过：`Command`+点击 seek 预览（`f451ae09`）已复验。 |
| root 拖放、脏文档关闭、播放中关闭、ChartDrop cancel | 生命周期规格与静态路由审阅 | 未执行。 |

> 修复轮本身只有自动证据（会话无显示器/截屏权限）；原生桌面复验由用户另行执行，五项验收门槛已通过。
> 详细的根因、回归、复验结果与未完成项见
> [QML_UI_V2_EXECUTION_AND_ACCEPTANCE_AUDIT_ZH.md](../../audit/QML_UI_V2_EXECUTION_AND_ACCEPTANCE_AUDIT_ZH.md)。

### 上游同步

- [x] 合入 `origin/dev` @ `0e85b0b2`。
- [x] 接受 DComp、`src/render`、`src/sources` 删除，保留进程内 QSG 路径。
- [x] 保留 `d3d11`、`dxgi`，MinGW 链接 `d3dcompiler`。
- [x] 合入预览音频 Worker facade、设备监听和启动诊断改动。

## 一阶段待办

### P0 — 时间轴接入（历史实现状态；后续由阶段 2 `TimelineSession` 接管）

- [x] Step 1：底栏已接入的时间轴与语法标签统一使用 `QuickShellController` 当前标签、显隐和文案状态，移除 `ViewState.activeBottomTab` 局部副本。
- [x] Step 2：时间轴顶部的缩放、亮度、视图锁定、进度同步与 Follow Code 交互已接入。
      窄窗口下的视觉与命中仍须列入原生桌面回归，不能由实现或 CTest 推断。
- [x] Step 3：v2 可见编辑器的光标行列已接入时间轴光标与 Follow Code；
      `Command`/`Ctrl`+点击可 seek 预览（`f451ae09`）。
- [x] Step 4：将时间轴导航结果回写到 v2 可见编辑器，补齐跳转、选区和跟随视觉状态。
      播放时走 QML navigation 请求移动光标；暂停或关闭代码跟随时改为只读跟随装饰
      （span 高亮 + 跟随光标 + 不动 caret 的滚动），见 `ca943a82`。
- [x] Step 6：Muri 标签、列表与问题跳转已接入，并复用 QML 分析投影；仍缺原生桌面观察记录。
- [x] Step 7：底栏前台显隐、面板高度回写与缩放状态已接入；主题切换的原生桌面回归仍未执行。
- [ ] Step 5（**不再按本清单推进**）：正文编辑的 `TimelineQuickModel` 增量更新优化由阶段 1 的
      `ChartWorkspace` 单一所有者设计重新评估；**已延后**，在迁移前不得继续扩大全文同步路径。

### 待处理功能缺口（2026-08-24 GUI 验证新发现，优先于 Step 5）

- [ ] **P0 — 文本控制键与 undo/save-point 迁移**：
      1. **已完成：** 修复 `Backspace` / `Delete` 的控制字符不能进入 `SimaiTextEditPolicy` 普通文本插入路径；
         保留空括号成对删除，普通删除交给 `TextArea` 原生行为，并以真实 `TextArea` + `QKeyEvent`
         回归覆盖 `\b` / `\x7f`。
      2. 以 v1 的“undo save anchor”语义为参照，但由 `ChartWorkspace` 的完整文档 save point 作为唯一
         dirty 真相；不得只给 `QmlEditorController` 增加局部 dirty 布尔值。
      3. 迁移 `QmlDocumentModel` 的正文、元数据、难度和文件操作到 `ChartWorkspace` /
         `ChartWorkspaceFileService`，再让 `AnalysisService` 的 revision 快照接管验证/Muri 投影。
         过渡期 `MainWindow` 只能消费提交后的适配值，不能继续拥有或判定 v2 文档 dirty。
      4. 回归：打开→编辑→撤销回初始、保存→编辑→撤销回保存点、元数据仍脏时正文撤销、分支编辑、
         难度/文档切换，以及脏文档关闭。该项是阶段 1 完成、关闭流程手工回归和阶段 2 的前置条件。
- [ ] 切换文档后 PV 异常：**多次复现失败，需求延后。** 埋点保留在树上
      （`editor/document_replaced` 与 `editor/document_shown`），日后若再次出现可直接取证。
      在拿到一次成功复现之前不再投入排查。排查记录见审计文档的“切换文档排查记录”。
- [ ] 撤销栈：**部分修复，仍有问题。** 已修的是历史被误清空与 undo/redo 的替换跨度
      （`581b782b`：不再被 controller 来源的同步清空，改为最小差异替换并选中所恢复的文本）。
      **未修的是 `Ctrl+Z` / `Ctrl+Y` 与文档置脏标记的联动**：撤销掉全部修改后，置脏标记不会
      随之复原，文档仍显示为已修改。按所有者要求，**参考 v1 的实现算法**来处理这条联动。
- [x] 快捷键体系（`676150e0`）：成因是绑定上下文而非缺注册层。v2 经 `QmlShortcutModel` 复用
  `ShortcutRegistry`，变换命令按 id 走 `MainWindow::triggerShortcutCommand`，预览命令直接绑
  `QuickShellController`；菜单行通过 `AppMenuAction.shortcutText` 显示快捷键。

### v2 预览性能修复（2026-08-25）

- [x] 预览 surface 互斥生命周期：工作区、紧凑面板、全屏同一时刻只允许一个
      `PreviewSurface` 订阅 `PreviewRuntime`；不可见分支由 Loader 卸载，不再收到 frame-state
      fan-out。
- [x] 预览统计投影去热路径化：`QmlPreviewModel` 缓存统计 entries，并按 position / transport /
      playing / render mode / statistics 分离通知。播放位置不会重建统计、生成 note-icon URL 或探测
      皮肤文件。
- [x] QML 跟随去重：同一 token/已居中的 navigation 不再反复操作 `TextArea.select()`、装饰或
      排队滚动；文档/难度导致的 follow binding cache 失效同时清除 QML navigation cache。
- [ ] 播放期高位内存平台：当前证据为“跃升后稳定”而非持续泄漏（private memory 约 957 MB 平台，
      第二段播放约 1051–1064 MB）。需继续拆分 QtAVPlayer/D3D11VA 帧池、QML/Qt Quick 和私有堆；
      不得先验归咎于 preview texture cache。**已延后。**

详细登记见
[QML_UI_V2_EXECUTION_AND_ACCEPTANCE_AUDIT_ZH.md](../../audit/QML_UI_V2_EXECUTION_AND_ACCEPTANCE_AUDIT_ZH.md)。

### P1 — 文档模型与诊断契约（已完成）

- [x] 收紧 `QmlDocumentModel` 边界，将对 `MainWindow` 私有字段的直接读写迁移到窄公开 API。
- [x] 将 v2 启动目标迁移到公开 `openStartupTarget()`，清理 `friend class QmlUiBootstrap`。
- [x] 将 `runValidateSimaiSilently` 的结果接入 `syntaxIssues`、错误数、警告数和音符数。
- [x] 让底栏检查列表、诊断跳转和编辑器错误波浪线消费同一份诊断数据。

### P2 — 文本编辑器逻辑移植

- [x] 文本编辑器主要逻辑从 v1 移植到 v2。
  - [x] 共享编辑规则：保留 v2 QML `TextArea` 与 `QTextDocument`，从
        `PlainCodeEditor.Input.cpp` / `PlainCodeEditor.BracketCompletion.cpp` 抽取控件无关的
        文本编辑事务、半角转换、括号处理与补全状态，供 v1 / v2 共用。
  - [x] v2 编辑控制器：连接 `TextArea` / `QQuickTextDocument`，统一管理光标、选择区、
        编辑事务、覆盖模式、补全状态和现有编辑偏好，通过 `QmlApplicationContext`
        向 `SourceEditor.qml` 提供窄接口。
  - [x] 基础输入：移植半角字符、IME commit、粘贴、Enter / Ctrl+Enter、Insert 覆盖模式、
        成对括号、右括号越过、空括号成对删除、已有 `[` 进入和 `h` hold 补全入口。
  - [x] 智能补全：复用 `SimaiCompletionCatalog`，用 QML 候选列表承接 BPM、细分与时值候选，
        支持实时过滤、光标锚定、上下选择、Tab / Enter 接受、Esc 关闭和鼠标选择。
  - [x] 编辑命令：接入选择区替换记录、撤销 / 重做路由、谱面变换快捷键、上下文菜单、
        选择区和光标行列同步，通过 `QmlCommandService` 与窄公开 API 连接现有后端。
  - [x] 编辑辅助：`LineNumberGutter.qml` 接入书签显示、跳转和右键操作；补齐查找界面、
        预览跟随视觉光标、诊断跳转、错误波浪线和底栏统计。
  - [x] 高亮规则：共享 v1 `BracketScopeHighlighter` 与 v2 `SimaiSyntaxHighlighter` 的词法规则，
        同步相关 CMake 源文件和仓库指南。

### P3 — 页面接线与产品决策（历史缺口；迁入目标架构的 `ExportService`）

- [ ] `QmlExportSession` / `ExportVideoPage.qml` 接入片头音文件名和 `introSoundVolume`，并写入导出 snapshot。
- [x] 将 v2 根窗口接入 ChartDrop；`QmlUiBootstrap` 注册 root window、安装拖放事件过滤器并创建/同步
      `ChartDropOverlay`，释放或取消时清理 overlay 与 root 绑定。
- [ ] 手工确认 v2 工具箱的批量上传入口能够打开 `net.batchUpload.open`。
- [x] 缺失 QML 实现的功能保留可发现入口并弹出“暂未更新支持”；已有后端的安全操作继续直接接入，
      不再从界面移除。具体范围见 `QML_UI_V2_CAPABILITY_GAP_RESEARCH_ZH.md`。
- [x] 全屏预览采用工作区覆盖层；v1 OS 全屏路径已随 v1 shell 删除。导出工作区中禁用进入全屏，
      以规避硬件解码驱动问题。

### P4 — 长期页面迁移（已被架构决策替代）

- [ ] 0c 已延后：继续保留 Latency 的 `WindowContainer` 与既有 Widget 页面；不在本阶段将它迁移为 QML
      或删除。恢复 0c 时再依据产品决策重新立项。

## 手工回归清单

- [ ] 脏文档、播放中、导出任务运行时的关窗流程与 v1 一致。
- [ ] 设备热插拔暂停后，下一次播放走 cold Prepare。
- [ ] play、pause、seek 按 completion 更新界面状态。
- [ ] EraseByArea、烟花时长和 BGM 过轨静音行为正确。
- [ ] 音频拖放建谱和 ChartDrop 覆盖层行为正确。
- [ ] QML 导出片头音文件和音量进入最终任务。
- [ ] `d534b393` 的 bookmark / touch input 改动完成 GUI 回归。

## 关键路径

| 角色 | 路径 |
|------|------|
| 皮肤切换 | `src/app/main.cpp` → `resolveUiSkin()` |
| v2 启动 | `src/app/qml_ui/QmlUiBootstrap.*` |
| 契约根 | `src/app/qml_ui/QmlApplicationContext.*` |
| 文档桥 | `src/app/qml_ui/QmlDocumentModel.*` |
| 预览桥 | `src/app/qml_ui/QmlPreviewModel.*` |
| 编辑器 | `src/app/qml_ui/QmlEditorController.*`、`QmlEditorInputBridge.*`、`editor/SourceEditor.qml`、`SimaiSyntaxHighlighter.*` |
| 视频导出 | `src/app/qml_ui/export/QmlExportSession.*`、`ExportVideoPage.qml` |
| Latency 页宿主 | `src/app/qml_ui/QmlEditorPageHost.*` |
| 主壳 | `src/app/qml_ui/Main.qml`、`layout/MainView.qml` |
| 标题栏 | `src/app/qml_ui/QmlUiWindowChrome.*` |
| 工具侧栏 | `src/app/qml_ui/sidebar/ToolsSidebarPage.qml` |
| 工作区状态 | `layout/MainSplitView.qml` / `preview/PreviewPane.qml` ← `shellController.*` |
| 临时 v2 兼容控制器 | `src/app/quick_shell/QuickShellController.*`（阶段 2 删除） |
| 开发索引 | `.agents/skills/miacode-dev-guide/references/feature-index.md` |

## 历史清单的遗留风险

- 撤销/重做与 dirty 标记的联动尚未修复；它属于阶段 1 `ChartWorkspace` + `EditorService` 的事务/保存点
  契约，不能继续依赖隐藏 `PlainCodeEditor` 的实现细节。
- 切换文档后的 PV 报告未能复现，已延后；保留现有埋点，只有再次复现才重新排查。
- 播放期高位内存目前证据为平台而非持续泄漏；在阶段 2 迁移 Preview/Timeline 前先做分层取证，不能先验
  归因于 preview texture cache；**本项已延后**。
- 阶段 0b（扩展宿主）和 0c（被裁剪页面）均已延后；不得将“延后”误记为已删除或功能不可用。

## 后续推进顺序（以目标架构为准）

原则：**先消除双所有者与轮询，再补页面；优化类条目一律靠后。**

### 阶段 1 当前证据（2026-08-25）

- `ChartWorkspace` 已覆盖严格完整源码预检、活动难度、单次 revision 发布与 dirty save point；
  `ChartWorkspaceFileService` 覆盖 BOM/系统编码打开和 `QSaveFile` 原子保存；`AnalysisService` 从同一
  workspace revision 生成验证、偏移 marker、Muri 和静态引用快照。
- Release `MiaCode` 构建通过；`qml_document_projection_spec`、`chart_workspace_spec`、
  `chart_workspace_file_service_spec`、`analysis_service_spec`、`v1_shell_removal_spec` 与
  `debug_flag_index_spec` 共 6/6 通过。
- 以上服务仍未接管生产 QML 会话；现有 `QmlDocumentModel` 对 `MainWindow.DocumentBridge` 的依赖是
  下一步唯一允许收缩的 v1 耦合边界，不得为过渡期新增第二套可写文档。

1. 阶段 1（进行中）：`ChartWorkspace`、`ChartWorkspaceFileService` 与 `AnalysisService` 的 Widgets-free
   契约规格和实现已落地；完整源码预检已从 `QmlDocumentProjection` 下沉复用，分析服务已产出
   revision-stamped 的验证/Muri 快照。下一步迁移生产文档事务与分析调度，消除 `MainWindow` 的第二所有者与缓存。
2. 先完成 P0 文本控制键修复；随后完成上述 QML 文档会话迁移及 undo/save-point 回归。该工作不依赖
   0b/0c、性能平台、导出或阶段 2，但阶段 2、脏文档关闭回归依赖它。
3. 将 `AnalysisService` 接入迁移后的文档会话，并以其 revision 快照替换 `MainWindow` 验证/Muri 缓存投影。
4. 阶段 2：建立 `PreviewSession` 与 `TimelineSession`，删除 `QuickShellController` 的轮询和剩余兼容 API。
5. 阶段 3：将导出、封面、ZIP 和 Net 收敛为 `ExportService` 作业模型；片头音、批量上传和禁用控件在此
   域解决。
6. 阶段 4：删除 `MainWindow` 与 `Qt6::Widgets`；原生桌面回归作为每阶段验收，而非替代迁移契约。

## 更新规则

1. 仅对仍适用于隐藏 `MainWindow` 兼容形态的历史事实更新本文件；新工作写入目标架构的对应阶段。
2. 架构、入口或跨模块契约变化时，同步更新仓库指南。
3. 手工回归结果记录在对应条目中，不能由构建或 CTest 替代。
4. 不得重新引入 v1 shell；如需恢复被裁剪功能，须单独作产品决策。
