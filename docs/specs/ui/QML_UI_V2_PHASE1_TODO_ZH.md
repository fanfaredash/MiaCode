# QML UI v2 一阶段 Todo

> 当前工作清单。完成或调整条目时，同步更新本文件；架构与入口发生变化时，同步更新仓库指南。

## 当前基线

- 分支：`feature/qml-ui`
- 上游：已合入 `origin/dev` @ `0e85b0b2`
- 默认界面：`QmlUiBootstrap`（v2）
- v1 入口：`--ui=v1` / `MIACODE_UI_SKIN=v1` → QuickShell
- 构建目录：`build/`
- 原型参考：`../MashiroEditor/src/ui`

## 范围与契约

- v1 / v2 共存，保留 QuickShell 再宿主路径：`NativeSurfaceHost`、`StyleBridge`、`QuickShellMain.qml`。
- v2 方向为纯 QML 主壳、C++ 域服务和已有 `QQuickItem` 时间轴/预览。
- 视频导出页已经使用纯 QML `ExportVideoPage.qml`，由 `QmlExportSession` 提供业务状态和操作。
- `QmlEditorPageHost` 负责全页导航，并通过局部 `WindowContainer` 宿主 v1 `LatencyDetectionPage`。
- v2 共享隐藏的 `MainWindow` 与 `QuickShellController(surfaceHost=nullptr)`。
- 工作区模式由 `MainWindow` 切换，v2 从 `QuickShellController` 读取底栏显隐、预览画幅和导出页状态。
- v2 时间轴保留 `TimelineQuickModel` → `TimelineQuickStateBridge` → `TimelineQuickItem` 的 QSG 渲染链，重点补齐 QML 主壳的交互与编辑器接入。
- 实时预览和视频导出共用进程内 QSG 渲染路径。
- 上游 v1 壳层改动与 v2 主壳分离；共享的 `MainWindow`、Controller、Preview、Timeline API 仍需同步。

## 已完成

### 启动与壳层

- [x] 恢复并保留 v1 QuickShell 文件和入口。
- [x] 接入 `src/app/qml_ui/`、`QmlUiBootstrap`、`MiaCode.UI` 模块与皮肤切换入口。
- [x] v2 默认使用隐藏 `MainWindow` 和无主壳宿主的 `QuickShellController`。
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

### 二阶段桌面手工回归矩阵（已开始，尚未通过）

本机以 `QT_QPA_PLATFORM=offscreen` 启动 QML 桌面壳会触发既有 macOS 原生主题/平台插件崩溃，不能把
offscreen 自动化结果当成桌面视觉或输入验证。以下状态来自 2026-08-24 的原生桌面验收；未列为通过的项目不得由构建或 CTest 推断为通过：

| 流程 | 自动/静态证据 | 原生 GUI 状态（2026-08-24 修复轮后） |
|---|---|---|
| 1280×720、窄窗口、Large system font、浅/深色、中文/英文、错误/警告非仅颜色 | QML 静态审阅；主题令牌与文本/图标语义已接入；`qml_editor_controller_spec` 载入真实 `CompletionPopup.qml` 断言高亮、宽度与锚点翻转 | 仍未观察。completion popup 三项成因已修（`5ee23d38`），窄窗口 Timeline 缩放仍待复测。 |
| 连续编辑与分析 | revision 投影、`qml_document_projection_spec`、`qml_analysis_model_spec` | Validation 上一轮通过；Muri 未重新观察，不能从自动测试推断桌面通过。 |
| timeline 缩放、亮度、follow、拖拽/滚轮/播放与三 tab | `timeline_model_spec`、`timeline_marker_offset_spec`；暂停跟随装饰的路由与 QML 渲染回归在 `qml_editor_controller_spec` | 暂停跟随成因已修（`ca943a82`，装饰通道接入可见 QML 编辑器）。缩放/命中/四个 follow 状态仍待桌面观察。 |
| IME、半角、括号、hold、查找替换、书签、undo/redo、诊断跳转 | 编辑器四项 specs；真实 `TextArea` + 真实 `QKeyEvent` 的命令修饰键回归 | 书签上一轮通过。正文右键菜单（`2a27630d`）、命令修饰键写入字面字符（`75c89635`）已修。**IME commit+Enter 重复插入未修复**：本机无法复现，改为落地诊断（`bbd5e3b8`），需带真实输入法的机器以 `--debug` 复现一次。 |
| caret/selection→timeline 与 Ctrl+touch 创作 gate | `qml_editor_controller_spec`、`touch_pad_authoring_state_spec` | `Command`+点击 seek 预览已接入（`f451ae09`），回归投递真实 `Ctrl+左键`；桌面复验仍未执行。 |
| root 拖放、脏文档关闭、播放中关闭、ChartDrop cancel | 生命周期规格与静态路由审阅 | 未执行。 |

> 本轮所有修复都只有自动证据：本机没有显示器/截屏权限，原生 GUI 复验一次都没有执行。
> 详细的根因、回归与未完成项见
> [QML_UI_V2_EXECUTION_AND_ACCEPTANCE_AUDIT_ZH.md](../../audit/QML_UI_V2_EXECUTION_AND_ACCEPTANCE_AUDIT_ZH.md)。

### 上游同步

- [x] 合入 `origin/dev` @ `0e85b0b2`。
- [x] 接受 DComp、`src/render`、`src/sources` 删除，保留进程内 QSG 路径。
- [x] 保留 `d3d11`、`dxgi`，MinGW 链接 `d3dcompiler`。
- [x] 合入预览音频 Worker facade、设备监听和启动诊断改动。

## 一阶段待办

### P0 — 时间轴接入

- [x] Step 1：底栏已接入的时间轴与语法标签统一使用 `QuickShellController` 当前标签、显隐和文案状态，移除 `ViewState.activeBottomTab` 局部副本。
- [ ] Step 2：接入时间轴顶部的缩放、亮度、视图锁定、进度同步与 Follow Code 交互。
      控件与 header 限位已接入；窄窗口下的视觉与命中仍待桌面观察。
- [ ] Step 3：将 v2 可见编辑器的光标行列接入时间轴光标与 Follow Code。
      `Command`/`Ctrl`+点击已可 seek 预览（`f451ae09`）。
- [x] Step 4：将时间轴导航结果回写到 v2 可见编辑器，补齐跳转、选区和跟随视觉状态。
      播放时走 QML navigation 请求移动光标；暂停或关闭代码跟随时改为只读跟随装饰
      （span 高亮 + 跟随光标 + 不动 caret 的滚动），见 `ca943a82`。
- [ ] Step 5：将 QML 正文编辑接入 `TimelineQuickModel` 增量更新路径，取消每次输入对整份正文的全量刷新。
- [ ] Step 6：接入 Muri 标签、列表与问题跳转，与 v1 共用诊断状态。
- [ ] Step 7：补齐底栏前台生命周期、面板高度同步、缩放与主题刷新。

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

### P3 — 页面接线与产品决策

- [ ] `QmlExportSession` / `ExportVideoPage.qml` 接入片头音文件名和 `introSoundVolume`，并写入导出 snapshot。
- [x] 将 v2 根窗口接入 ChartDrop；`QmlUiBootstrap` 注册 root window、安装拖放事件过滤器并创建/同步
      `ChartDropOverlay`，释放或取消时清理 overlay 与 root 绑定。
- [ ] 手工确认 v2 工具箱的批量上传入口能够打开 `net.batchUpload.open`。
- [ ] 梳理禁用菜单和按钮：已有后端的接入现有操作，暂缺能力的从界面移除。
- [ ] 确定全屏预览采用工作区覆盖层或 v1 OS 全屏，并记录最终行为。

### P4 — 长期页面迁移

- [ ] 将 Latency 页面迁移为 QML 页面和窄业务 API，移除 v2 编辑区剩余的局部 `WindowContainer`。

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
| v1 壳 | `src/app/quick_shell/` |
| 开发索引 | `.agents/skills/miacode-dev-guide/references/feature-index.md` |

## 推进顺序

1. P0 时间轴接入，按 Step 1 至 Step 7 分批提交。
2. P2 文本编辑器逻辑移植。
3. P3 页面接线与产品决策。
4. P4 Latency 页面迁移。

## 更新规则

1. 完成任务后勾选对应条目，并将稳定结果归入“已完成”。
2. 架构、入口或跨模块契约变化时，同步更新仓库指南。
3. 手工回归结果记录在对应条目中。
4. v1 退役需要单独决策。
