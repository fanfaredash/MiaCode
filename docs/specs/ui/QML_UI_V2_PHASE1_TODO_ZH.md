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

### 上游同步

- [x] 合入 `origin/dev` @ `0e85b0b2`。
- [x] 接受 DComp、`src/render`、`src/sources` 删除，保留进程内 QSG 路径。
- [x] 保留 `d3d11`、`dxgi`，MinGW 链接 `d3dcompiler`。
- [x] 合入预览音频 Worker facade、设备监听和启动诊断改动。

## 一阶段待办

### P0 — 时间轴接入

- [x] Step 1：底栏已接入的时间轴与语法标签统一使用 `QuickShellController` 当前标签、显隐和文案状态，移除 `ViewState.activeBottomTab` 局部副本。
- [ ] Step 2：接入时间轴顶部的缩放、亮度、视图锁定、进度同步与 Follow Code 交互。
- [ ] Step 3：将 v2 可见编辑器的光标行列接入时间轴光标与 Follow Code。
- [ ] Step 4：将时间轴导航结果回写到 v2 可见编辑器，补齐跳转、选区和跟随视觉状态。
- [ ] Step 5：将 QML 正文编辑接入 `TimelineQuickModel` 增量更新路径，取消每次输入对整份正文的全量刷新。
- [ ] Step 6：接入 Muri 标签、列表与问题跳转，与 v1 共用诊断状态。
- [ ] Step 7：补齐底栏前台生命周期、面板高度同步、缩放与主题刷新。

### P1 — 文档模型与诊断契约（已完成）

- [x] 收紧 `QmlDocumentModel` 边界，将对 `MainWindow` 私有字段的直接读写迁移到窄公开 API。
- [x] 将 v2 启动目标迁移到公开 `openStartupTarget()`，清理 `friend class QmlUiBootstrap`。
- [x] 将 `runValidateSimaiSilently` 的结果接入 `syntaxIssues`、错误数、警告数和音符数。
- [x] 让底栏检查列表、诊断跳转和编辑器错误波浪线消费同一份诊断数据。

### P2 — 文本编辑器逻辑移植

- [ ] 文本编辑器主要逻辑从 v1 移植到 v2。
  - [ ] 共享编辑规则：保留 v2 QML `TextArea` 与 `QTextDocument`，从
        `PlainCodeEditor.Input.cpp` / `PlainCodeEditor.BracketCompletion.cpp` 抽取控件无关的
        文本编辑事务、半角转换、括号处理与补全状态，供 v1 / v2 共用。
  - [ ] v2 编辑控制器：连接 `TextArea` / `QQuickTextDocument`，统一管理光标、选择区、
        编辑事务、覆盖模式、补全状态和现有编辑偏好，通过 `QmlApplicationContext`
        向 `SourceEditor.qml` 提供窄接口。
  - [ ] 基础输入：移植半角字符、IME commit、粘贴、Enter / Ctrl+Enter、Insert 覆盖模式、
        成对括号、右括号越过、空括号成对删除、已有 `[` 进入和 `h` hold 补全入口。
  - [ ] 智能补全：复用 `SimaiCompletionCatalog`，用 QML 候选列表承接 BPM、细分与时值候选，
        支持实时过滤、光标锚定、上下选择、Tab / Enter 接受、Esc 关闭和鼠标选择。
  - [ ] 编辑命令：接入选择区替换记录、撤销 / 重做路由、谱面变换快捷键、上下文菜单、
        选择区和光标行列同步，通过 `QmlCommandService` 与窄公开 API 连接现有后端。
  - [ ] 编辑辅助：`LineNumberGutter.qml` 接入书签显示、跳转和右键操作；补齐查找界面、
        预览跟随视觉光标、诊断跳转、错误波浪线和底栏统计。
  - [ ] 高亮规则：共享 v1 `BracketScopeHighlighter` 与 v2 `SimaiSyntaxHighlighter` 的词法规则，
        同步相关 CMake 源文件和仓库指南。

### P3 — 页面接线与产品决策

- [ ] `QmlExportSession` / `ExportVideoPage.qml` 接入片头音文件名和 `introSoundVolume`，并写入导出 snapshot。
- [ ] 将 v2 根窗口接入 ChartDrop；上游 `setQuickShellRootWindow` 和拖放覆盖层代码已经存在。
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
| 编辑器 | `src/app/qml_ui/editor/SourceEditor.qml`、`SimaiSyntaxHighlighter.*` |
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
