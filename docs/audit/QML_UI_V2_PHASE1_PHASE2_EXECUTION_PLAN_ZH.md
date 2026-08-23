# QML UI v2 阶段 1 与阶段 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不扩张 `MainWindow` 职责、不退回 Widgets 主壳的前提下，使 QML UI v2 具备可验证的文档状态边界，以及可日常制谱的 timeline、诊断和文本编辑工作流。

**Architecture:** `MainWindow` 继续是文档、timeline、preview、validation/Muri、export 的唯一编排者；QML 只通过值型快照、窄命令和 revision-aware 投影读取/修改状态。Timeline 继续使用已有的 `TimelineQuickItem` 与 `QuickShellController`，编辑器继续使用 QML `TextArea`，两者均不复制解析、Muri、渲染或音频逻辑。

**Tech Stack:** Qt 6.8+、C++20、Qt Quick/QML、Qt Quick Controls、既有 `TimelineQuickItem`/QSG、`SimaiDocument`、`SimaiCompletionCatalog`、CTest Release specs。

---

## 0. 当前基线、范围与完成定义

本计划以远端 `feature/qml-ui` 的 `bd663a53`（2026-08-24）为基线。它已合入 P0 共同后端，并已完成下列原始阶段 1 工作：

- `QmlDocumentModel` 不再 friend 直写 `MainWindow`，经 `MainWindow.DocumentBridge.cpp` 的公开 API 修改；
- `syntaxIssues`、错误/警告/物件数已从现有 validation cache 投影到 QML；
- QML 预览负时间 scrub、三态 Muri 显示等近期接线已经存在。

因此本计划不重复这些实现；阶段 1 的剩余目标是让已存在的边界成为**有 revision、不会过期回写、可被自动验证的事务边界**，并补齐 v2 root-window/ChartDrop 生命周期。阶段 2 是“编辑器与 timeline 可用于日常制谱”，不包含片头音导出、系统全屏产品决策或 Latency 原生化（均留给后续阶段）。

### 不可破坏的约束

- 不重新引入 DComp/D3D11 或 out-of-process preview；实时预览和导出均继续走进程内 QSG。
- 不能把 QML 状态作为第二个 document、timeline、validation 或 Muri 真相源；`SimaiDocument` 与 `MainWindow` 仍是权威。
- 任何影响 `&first`、timing metadata、正文的修改必须走既有 quick timeline → slow refresh → validation/Muri → paused-preview 的链；过期慢任务不得覆盖新文本。
- v1 QuickShell 与 Widgets 是迁移期对照实现，不能在本阶段删除；QML 只复用其 Controller/API，不宿主整个主窗口。
- 文本输入必须同时支持鼠标、键盘、IME；补全弹窗不能抢走编辑器焦点，所有新增控件需要可见键盘焦点和 Escape 退出路径。

### 预定文件边界

| 责任 | 新建/修改文件 |
| --- | --- |
| 文档事务与 revision 快照 | `src/app/mainwindow/MainWindow.h`、`MainWindowMemberStorage.inc`、`sections/document/MainWindow.DocumentBridge.cpp`、`sections/validation/MainWindow.ValidationRuntime.cpp`、`sections/timeline/MainWindow.TimelineAnalysisFlow.cpp` |
| QML 文档/分析投影 | `src/app/qml_ui/QmlDocumentModel.*`；新建 `QmlAnalysisModel.*`、必要时 `QmlDocumentProjection.*`；`QmlApplicationContext.*`、`CMakeLists.txt` |
| QML root 和拖放 | `src/app/qml_ui/QmlUiBootstrap.*`；复用 `src/app/ui/ChartDropOverlay.*` 与 `MainWindow.WindowInteraction.cpp` |
| timeline 操作面 | `src/app/qml_ui/timeline/BottomPanel.qml`、`BottomTabBar.qml`、`src/app/qml_ui/layout/MainSplitView.qml`、`src/app/qml_ui/ViewState.qml`、`QmlUiSettings.*`；复用 `QuickShellController.*` 与 `TimelineQuickItem` |
| QML 编辑策略与界面 | 新建 `src/editor/SimaiTextEditPolicy.*`、`src/app/qml_ui/QmlEditorController.*`；修改 `QmlApplicationContext.*`、`SourceEditor.qml`、`EditorPane.qml`、`LineNumberGutter.qml`，必要时新建 `editor/CompletionPopup.qml`、`FindReplaceBar.qml`、`BookmarkModel` QML 组件 |
| 自动化验证 | 新建 `src/tools/qml_ui/*Spec.cpp`；修改 `CMakeLists.txt`；保留并扩展 `src/tools/editor/PlainCodeEditorSpec.cpp`、`SimaiCompletionCatalogSpec.cpp`、`TimelineModelSpec.cpp`、`MuriSpec.cpp`、`TouchPadAuthoringStateSpec.cpp` |
| 维护文档 | `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md`、`.agents/skills/miacode-dev-guide/references/{feature-index,cross-chain-linkage}.md`（只在入口/跨链契约确有变化时） |

## 阶段 1：完成单一文档与状态边界（P0）

### Task 1：先固化文档 mutation → revision → validation 投影契约

**Files:**

- Modify: `src/app/mainwindow/MainWindow.h`、`src/app/mainwindow/MainWindowMemberStorage.inc`
- Modify: `src/app/mainwindow/sections/document/MainWindow.DocumentBridge.cpp`
- Modify: `src/app/mainwindow/sections/validation/MainWindow.ValidationRuntime.cpp`
- Modify: `src/app/mainwindow/sections/timeline/MainWindow.TimelineAnalysisFlow.cpp`
- Create: `src/app/qml_ui/QmlDocumentProjection.{h,cpp}`
- Test: `src/tools/qml_ui/QmlDocumentProjectionSpec.cpp`

- [ ] **Step 1: 编写失败 spec，覆盖 revision 门控。**

  构造“正文 A 触发 revision 41、正文 B 触发 revision 42、A 的慢分析后到”的场景。断言投影拒绝 41，只暴露 42；无活动难度或 cache 文本/timing 不匹配时投影必须是 `available=false`，不能显示旧问题。

- [ ] **Step 2: 在 `CMakeLists.txt` 用 `miacode_add_dev_tool(... TEST ...)` 注册 `qml_document_projection_spec`，并先运行失败用例。**

  Run: `ctest --test-dir build -C Release -R qml_document_projection_spec --output-on-failure`

  Expected: 缺少 projection/revision 行为而失败；不得以 sleep 或依赖 GUI 事件循环制造测试时序。

- [ ] **Step 3: 新建纯值型 `QmlDocumentProjection`，实现输入/输出契约。**

  输入至少包含 `difficultyId`、正文/timing 签名、timeline revision、validation revision、issues；输出包含 `available`、`pending`、`revision`、错误/警告数、物件数和带 line/column/endColumn/severity/message 的行。它只能投影既有 `DocumentValidationSnapshot`/Muri 结果，不能重新 parse。

- [ ] **Step 4: 将 revision 写入共同后端快照。**

  复用 `timelineRevision_` 和慢刷新结果的 revision：文档正文、`&first`、extra timing、完整源码替换、难度切换或删除后都递增/失效；后台回调仅在 difficulty、正文、timing 和 revision 同时匹配时发布 cache。将当前 revision 写进 `DocumentValidationSnapshot`，而不是让 QML 自行猜测 signal 顺序。

- [ ] **Step 5: 运行 spec，并回归既有分析 spec。**

  Run: `ctest --test-dir build -C Release -R 'qml_document_projection_spec|timeline_model_spec|muri_spec' --output-on-failure`

  Expected: 三项通过；revision 不匹配时没有任何旧诊断进入 QML 投影。

- [ ] **Step 6: 提交。**

  `git commit -m "feat(qml-ui): make document diagnostics revision-aware"`

### Task 2：让 QML 文档模型只发布同一事务的可呈现状态

**Files:**

- Modify: `src/app/qml_ui/QmlDocumentModel.{h,cpp}`
- Modify: `src/app/qml_ui/QmlApplicationContext.{h,cpp}`
- Modify: `src/app/qml_ui/editor/SourceEditor.qml`、`EditorPane.qml`
- Test: `src/tools/qml_ui/QmlDocumentProjectionSpec.cpp`

- [ ] **Step 1: 先为 QML 文档快照补失败测试。**

  覆盖五类 mutation：正文、`&first`、extra timing fields、完整源码、难度 level/designer。每类结果均应携带/反映同一 active difficulty、dirty、revision、诊断 `pending/available` 状态；完整源码替换在任意 difficulty 的 strict parse/validation 产生 error 时，必须保留旧 document、旧编辑器文本和旧 revision，并返回带 line/column/message 的失败结果。

- [ ] **Step 2: 为完整源码替换定义显式的两阶段后端事务结果。**

  将当前 `replaceDocumentSourceText(const QString&) -> bool` 扩展/替换为窄的 result API：先把文本解析为候选 `SimaiDocument`，对候选的每个 difficulty 使用现有 strict parser/validation 预检，再在没有 error 时一次性 `loadDocument()`/刷新 timeline。result 至少提供 `accepted`、`revision` 和结构化 issues；warning 可展示但不阻止替换。`QmlDocumentModel::metadataSourceError` 只在 rejected result 时写入，成功时才清除，绝不能在预检前清空。

- [ ] **Step 3: 在 `QmlDocumentModel` 增加只读 `documentRevision`、`validationRevision`、`validationPending`（或等价的单一 `validationState`）属性。**

  `markDocumentChanged()` 与 `emitDocumentStateChanged()` 必须以一个后端快照为源头批量发信号，避免 `chartTextChanged` 早于 active difficulty 切换或旧 `syntaxIssues` 短暂闪现。不要向 QML 暴露 `MainWindow` 指针、可写 cache 或 QWidget。

- [ ] **Step 4: 让 `SourceEditor.qml` 仅在 editor 文本对应的 revision 上绘制问题。**

  问题列表在 `pending` 时展示“正在分析”而非“未发现问题”；点击仅接受匹配 revision 的问题，并保持 `chartPosition()` 的 1-based line/column 协议。选择问题时先切换正确难度，再设置选区和 caret。

- [ ] **Step 5: 回归完整源码和偏移链。**

  手工用例：输入含 error 的完整源码后，确认旧文档/时间轴/预览不变且错误带定位显示；随后输入仅有 warning 的有效源码，确认完整原子替换。再修改 `&first`、`&wholebpm`、`&whole_time_signature` 和正文，确认 quick timeline 立即更新，慢分析期间显示 pending，最终 validation/Muri/paused preview 使用同一版本；连续两次快速输入后，第一次分析不能回写第二次输入。

- [ ] **Step 6: 运行自动化验证并提交。**

  Run: `ctest --test-dir build -C Release -R 'qml_document_projection_spec|simai_document_spec|timeline_model_spec|muri_spec' --output-on-failure`

  `git commit -m "feat(qml-ui): publish coherent document state"`

### Task 3：补齐 root-window 注册与 ChartDrop，不复制建谱流程

**Files:**

- Modify: `src/app/qml_ui/QmlUiBootstrap.{h,cpp}`
- Reuse: `src/app/ui/ChartDropOverlay.{h,cpp}`
- Reuse: `src/app/mainwindow/sections/window/MainWindow.WindowInteraction.cpp`
- Test: 新建最小 QML bootstrap/drag-route spec，或将无窗口可测部分提为纯 helper 后写入 `src/tools/qml_ui/`

- [ ] **Step 1: 为 root 生命周期定义可测的失败用例。**

  断言 root `QQuickWindow` 创建后先调用 `setQuickShellRootWindow(window)`，关闭/析构时清除 transient parent 和取消 pending overlay；重复 shutdown 不得二次析构或保留悬挂 QWindow。

- [ ] **Step 2: 在 QML bootstrap 复用 v1 的 overlay 连接模式。**

  对创建出的 `QQuickWindow` 显式执行 `setAcceptDrops(true)`，并安装现有 `MainWindow` event filter（关闭/销毁时卸载）；仅 `setQuickShellRootWindow()` 只会登记 transient parent，不能取代此事件路由。然后创建 `ChartDropOverlay`、监听 `MainWindow::chartDropOverlayVisibleChanged`，并使用短周期 monitor 同步根窗 frame geometry。拖入支持的音频时由既有 `handleChartAudioDropEvent()` 接受、显示覆盖层、drop 后异步调用既有 `handleAudioDrop()`；不在 QML 新建音频筛选或谱面创建逻辑。

- [ ] **Step 3: 处理关闭与异常路径。**

  `beginAcceptedRootWindowShutdown()`、析构和 QML 加载失败路径都调用 `cancelChartAudioDrop()`，销毁 overlay/timer，再释放 QML engine/backend。不得访问已销毁的 root window。

- [ ] **Step 4: 手工验证 Windows 与 macOS。**

  拖入单/多音频、拖入非音频、拖离、在 QML 控件上拖入、关窗时拖入。检查覆盖层坐标、瞬时隐藏、生成文件流程、对话框 transient parent、播放/脏文档关闭路径。

- [ ] **Step 5: 提交。**

  `git commit -m "feat(qml-ui): connect root window chart drop lifecycle"`

### Task 4：阶段 1 收口与可发布验证

**Files:**

- Modify: `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md`
- Modify if contracts changed: `.agents/skills/miacode-dev-guide/references/{feature-index,cross-chain-linkage}.md`

- [ ] **Step 1: 用一次端到端用例验证五类 mutation。**

  打开 chart → 修改正文 → 修改 `&first`/extra timing → 切难度并改 level/designer → 替换完整源码 → 保存/另存/放弃。每一步记录 document revision、诊断状态和 timeline/preview 结果。

- [ ] **Step 2: 执行 Release 目标和阶段 1 specs。**

  Run: `cmake --build build --config Release --parallel 4`

  Run: `ctest --test-dir build -C Release -R 'qml_document_projection_spec|simai_document_spec|timeline_model_spec|muri_spec|plain_code_editor_spec' --output-on-failure`

  macOS 内存受限时使用 `--parallel 2`；仅在已配置 `MIACODE_BUILD_DEV_TOOLS=ON` 的 Release build 目录运行。不得清理或覆盖他人的 build 目录。

- [ ] **Step 3: 更新待办和跨链文档，提交。**

  `git commit -m "docs(qml-ui): record phase 1 verification"`

## 阶段 2：使 timeline 与编辑器适合日常制谱（P0/P1）

### Task 5：先让 v2 timeline 使用已有的缩放、亮度和跟随控制

**Files:**

- Modify: `src/app/qml_ui/timeline/BottomPanel.qml`、`BottomTabBar.qml`
- Modify: `src/app/qml_ui/layout/MainSplitView.qml`、`src/app/qml_ui/ViewState.qml`、`QmlUiSettings.{h,cpp}`
- Reuse: `src/app/quick_shell/qml/TimelineTabSurface.qml`、`QuickShellController.{h,cpp}`、`TimelineQuickItem`
- Test: `src/tools/timeline/TimelineModelSpec.cpp`；补一个 QML 控制映射 spec（纯 helper 时放 `src/tools/qml_ui/`）

- [ ] **Step 1: 建立失败用例，检查 v2 操作不会创建平行状态。**

  断言 zoom、brightness、view lock、timeline sync、follow code 的点击全部进入 `QuickShellController`；状态只读取 `timelineStateBridge`/controller，QML `ViewState` 不保存重复布尔值。

- [ ] **Step 2: 从 `TimelineTabSurface.qml` 移植命中层，而不是重画 QSG header。**

  在 `BottomPanel.qml` 为已有 header 的 zoom/brightness 区放透明且可键盘操作的控制，坐标以 `mapToGlobal` 传给 `openTimelineZoomMenu()` / `openTimelineBrightnessMenu()`；保留 `TimelineQuickItem` 的 header 绘制和信号。

- [ ] **Step 3: 添加 follow 操作面并连接 tab 状态。**

  在 tab 条放一个空间不足时可折叠的 follow-code 开关/设置入口，调用 `timelineFollowPreviewToggled()`，其余 `viewportLock/timelineSync/followProgress` 通过现有 follow menu 暴露。tab 选择用 controller 的 `bottomTabsCurrentTabId`，不再以 `activeBottomTab` 整数孤立保存。

- [ ] **Step 4: 收敛底栏高度与缩放到 controller 的单一持久化状态。**

  v2 首次布局从 `shellController.bottomTabsHostHeight` 取得高度；分隔条拖动只调用 `setBottomTabsHostHeight()`，并由下一次 `shellStateChanged` 回填 QML，避免双向 binding 循环。删除/迁移 `QmlUiSettings::bottomPanelHeight` 的像素偏好（旧 key 仅一次性导入 controller 后移除），不保留第二份持久状态；所有 header 命中层按 `bottomTabsHeaderScale` 定位，使内容缩放、header 缩放和 DPI 行为与 v1 相同。

- [ ] **Step 5: 验证 timeline 交互。**

  Run: `ctest --test-dir build -C Release -R 'timeline_model_spec|timeline_marker_offset_spec' --output-on-failure`

  手工检查缩放、亮度、滚轮、拖拽、header seek、播放跟随、follow-code 开关以及窄窗口溢出；所有控制可用 Tab/Shift+Tab、Enter/Space、Escape。

- [ ] **Step 6: 提交。**

  `git commit -m "feat(qml-ui): restore timeline controls"`

### Task 6：把 validation 与 Muri 作为同一 slow-refresh 的 QML 分析页

**Files:**

- Create: `src/app/qml_ui/QmlAnalysisModel.{h,cpp}`
- Modify: `src/app/mainwindow/MainWindow.h`、`sections/validation/MainWindow.ValidationRuntime.cpp`、`sections/validation/MainWindow.ValidationRender.cpp`
- Modify: `src/app/qml_ui/QmlApplicationContext.{h,cpp}`、`timeline/BottomPanel.qml`、`BottomTabBar.qml`、`CMakeLists.txt`
- Test: 新建 `src/tools/qml_ui/QmlAnalysisModelSpec.cpp`；复用 `src/tools/muri/MuriSpec.cpp`

- [ ] **Step 1: 先写失败 spec，定义分析快照的对齐规则。**

  同一 revision 的 validation entries 与 `buildVisibleMuriPanelEntries()` 结果可显示；note-marker signature、difficulty 或 revision 不匹配时 model 必须显示 pending/empty，不能泄漏上一难度的 Muri 行。

- [ ] **Step 2: 在 MainWindow 提供窄的只读分析快照。**

  API 返回已对齐的 validation 和 Muri rows（line、column、second、severity/alert、title、detail、revision），内部继续复用 `MuriAnalysisReport`、static references 和 `buildVisibleMuriPanelEntries()`。不把 `QListWidget`、`MuriAnalyzer` 可写状态或 private members 暴露给 QML。

- [ ] **Step 3: 新建 `QmlAnalysisModel` 并纳入 `QmlApplicationContext`。**

  它监听现有 `documentValidationChanged`/分析完成通知，投影为 QML list；问题点击统一走“切难度 → reveal line/column → timeline 定位到 second”，并在 revision 不匹配时拒绝定位。将 `.h/.cpp` 加入现有 `MiaCode` source list，将 `QmlAnalysisModelSpec.cpp` 作为 `miacode_add_dev_tool(... TEST ...)` 注册；不得只创建文件而遗漏生产/CTest target。

- [ ] **Step 4: 将底栏从“时间轴/语法”升级为“时间轴/校验/Muri”。**

  校验 tab 沿用诊断摘要但显示 pending/available；Muri tab 以列表展示 severity+时间+说明，颜色外必须有文字/图标语义。保持窄窗口可滚动与焦点次序，不先用 `WindowContainer` 宿主旧 panel。

- [ ] **Step 5: 运行和手工验证。**

  Run: `ctest --test-dir build -C Release -R 'qml_analysis_model_spec|muri_spec|timeline_model_spec' --output-on-failure`

  手工：连续编辑、切难度、打开/关闭底栏、选择 validation/Muri 行；确认 QML、timeline dots、paused preview 和 export seed 使用同一慢分析版本。

- [ ] **Step 6: 提交。**

  `git commit -m "feat(qml-ui): expose aligned validation and muri panels"`

### Task 7：提取无控件依赖的 Simai 文本编辑策略

**Files:**

- Create: `src/editor/SimaiTextEditPolicy.{h,cpp}`
- Modify: `src/editor/PlainCodeEditor.Input.cpp`、`PlainCodeEditor.BracketCompletion.cpp`
- Modify: `src/tools/editor/PlainCodeEditorSpec.cpp`、`SimaiCompletionCatalogSpec.cpp`
- Create: `src/tools/qml_ui/SimaiTextEditPolicySpec.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 从 v1 行为补齐参数化失败测试。**

  至少覆盖半角转换、选区替换、成对括号插入、已有 `[` 进入、右括号越过、空括号单步删除、`h` hold 候选入口、Enter 与 Ctrl+Enter、Insert 覆写。每个结果显式断言新文本、selection/caret、是否消费按键、undo grouping 和 completion session，而不是只断言字符串。

- [ ] **Step 2: 运行失败用例。**

  Run: `ctest --test-dir build -C Release -R 'plain_code_editor_spec|simai_completion_catalog_spec|simai_text_edit_policy_spec' --output-on-failure`

- [ ] **Step 3: 新建纯 `SimaiTextEditPolicy`。**

  输入为 text、selection、键/IME commit、overwrite、auto-completion、whole BPM；输出为单一 edit transaction 和 completion state。`PlainCodeEditor` 改为适配该策略，保留 QWidget 的 popup 和快捷键路由；不得将 `QTextEdit`、QML 或 `MainWindow` 依赖混入策略。

- [ ] **Step 4: 运行通过后提交。**

  `git commit -m "refactor(editor): share simai text edit policy"`

### Task 8：为 QML 编辑器提供单一 controller 与无焦点抢占的补全 UI

**Files:**

- Create: `src/app/qml_ui/QmlEditorController.{h,cpp}`、`editor/CompletionPopup.qml`
- Modify: `src/app/qml_ui/QmlApplicationContext.{h,cpp}`、`SourceEditor.qml`、`EditorPane.qml`
- Modify: `CMakeLists.txt`
- Test: `src/tools/qml_ui/QmlEditorControllerSpec.cpp`

- [ ] **Step 1: 写 controller 的失败 spec。**

  输入 `SimaiTextEditPolicy` 的 transaction 后断言 QML 侧应应用一次 text/selection、只提交一次 `document.chartText`，candidate filter/active row 正确；completion 打开时 Up/Down/Tab/Enter/Escape/鼠标选择遵守 v1 语义。

- [ ] **Step 2: 实现 `QmlEditorController`，只管理编辑事务与 presentation state。**

  公开 text mutation、caret/selection、overwrite、completion candidates/filter/index、`canUndo/canRedo` 和现有偏好投影；不把 QQuickTextDocument 或 QML object 生命周期保存在 MainWindow。定义明确的 QML 输入适配：`SourceEditor` 将 key、IME committed text 和 paste payload 分别传给 controller，再一次性应用返回 transaction；composition 未提交时不改 document。IME commit 和粘贴也必须经过同一 transaction，不得只截获普通 key press。

- [ ] **Step 3: 在 `SourceEditor.qml` 应用 transaction 并实现补全 Popup。**

  Popup 锚定 `cursorRectangle`，不取得焦点；文本编辑器始终处理输入，Popup 只接收鼠标选择。Enter 在候选打开时接受候选且不插入换行，Ctrl/Alt/Meta+Enter 保留正常换行。所有候选和错误状态使用 Theme token，中文/英文扩张不裁剪。将 `QmlEditorController.{h,cpp}` 加入 `MiaCode` sources，把 `CompletionPopup.qml` 加入现有 `qt_add_qml_module(... QML_FILES ...)`，并注册 `QmlEditorControllerSpec` 为 CTest target；三项缺一不可。

- [ ] **Step 4: 运行 spec 和手工输入矩阵。**

  Run: `ctest --test-dir build -C Release -R 'qml_editor_controller_spec|simai_text_edit_policy_spec|plain_code_editor_spec|simai_completion_catalog_spec' --output-on-failure`

  手工：中英文 IME、粘贴、选择覆盖、撤销/重做、长谱滚动、深色/浅色、键盘-only 操作。

- [ ] **Step 5: 提交。**

  `git commit -m "feat(qml-ui): add simai editor controller and completion"`

### Task 9：补齐查找替换、书签、caret → timeline → touch-pad 创作链

**Files:**

- Create: `src/app/qml_ui/editor/FindReplaceBar.qml`、必要时 `BookmarkList.qml`
- Modify: `src/app/qml_ui/editor/{SourceEditor,EditorPane,LineNumberGutter}.qml`
- Modify: `CMakeLists.txt`
- Modify/Reuse: `src/editor/{BookmarkCommentSyntax,TouchPadAuthoringEdit,PlainCodeEditor.Bookmarks}.*`
- Modify: `src/app/mainwindow/sections/{document,timeline}/` 的窄公开 command，仅在现有 API 不能表达 caret/selection 时新增
- Test: `src/tools/qml_ui/QmlEditorControllerSpec.cpp`、`src/tools/editor/PlainCodeEditorSpec.cpp`、`src/tools/preview/TouchPadAuthoringStateSpec.cpp`

- [ ] **Step 1: 写失败 spec，先锁定数据协议。**

  查找/替换必须按 active document、selection、case/whole-word 选项工作；书签只经既有 bookmark comment 语法读写，行号变动后仍定位正确；caret/selection revision 变更时，timeline resolve 和 Ctrl+触控区编辑不得使用旧位置。

- [ ] **Step 2: 加查找替换与当前行选择。**

  `Ctrl+F` 打开紧凑栏，Enter/Shift+Enter 前后查找，Replace/Replace All 均生成单次或明确分组的 undo transaction，Escape 关闭且回到 editor；不保留当前 `MainMenu.qml` 的 disabled 假入口。

- [ ] **Step 3: 加书签 UI。**

  `LineNumberGutter.qml` 绘制可访问的书签标识，右键/键盘命令支持新建、改名、删除和跳转；侧栏按难度展示。文件内持久化继续走 `BookmarkCommentSyntax`，QML 不自行序列化 `&miacode_bookmarks`。将 `FindReplaceBar.qml`、`BookmarkList.qml` 和任何新增 editor QML 文件全部登记进既有 `qt_add_qml_module(... QML_FILES ...)`，否则禁止声明任务完成。

- [ ] **Step 4: 接 caret/selection 和 touch-pad。**

  每次 QML caret/selection 变化携带 active difficulty + document revision 给后端；复用现有 cursor-to-timeline mapping 和 `TouchPadAuthoringEdit`/`TouchPadAuthoringState`。Ctrl+触控区仅在 QML 编辑器焦点、revision 匹配、非 IME composition 时生效；普通点击继续是 timeline 导航。

- [ ] **Step 5: 运行回归。**

  Run: `ctest --test-dir build -C Release -R 'qml_editor_controller_spec|plain_code_editor_spec|touch_pad_authoring_state_spec|timeline_model_spec' --output-on-failure`

  手工：替换全部后 undo、书签跨保存/重开、caret 移动/选择 → timeline、Ctrl+touch 编辑、macOS 触控板拖选与自动滚动。

- [ ] **Step 6: 提交。**

  `git commit -m "feat(qml-ui): complete editor workflow and touch authoring"`

### Task 10：阶段 2 收口、可访问性与完整回归

**Files:**

- Modify: `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md`
- Modify if behavior changes: `.agents/skills/miacode-dev-guide/references/{feature-index,cross-chain-linkage}.md`

- [ ] **Step 1: 完成 QML desktop UI 检查。**

  目标是桌面矩形窗口（`Main.qml` 基线 1280×720）、既有 `Theme.qml` 令牌、中文/英文界面、鼠标/键盘/IME 输入；当前产品没有 RTL 范围。检查窄窗口、Large system font、浅/深色、错误/警告非仅颜色表达、tab 顺序和 Escape 无焦点陷阱。

- [ ] **Step 2: Release 全量 build 与针对性 CTest。**

  Run: `cmake --build build --config Release --parallel 4`

  Run: `ctest --test-dir build -C Release -R 'qml_.*_spec|simai_text_edit_policy_spec|plain_code_editor_spec|simai_completion_catalog_spec|timeline_model_spec|timeline_marker_offset_spec|muri_spec|touch_pad_authoring_state_spec' --output-on-failure`

- [ ] **Step 3: 执行手工端到端矩阵。**

  | 流程 | 必须观察到的结果 |
  | --- | --- |
  | 连续编辑与分析 | quick timeline 立即更新；旧 slow result 被拒绝；校验/Muri/preview 同 revision |
  | timeline | 缩放、亮度、四个 follow 行为、拖拽/滚轮/播放跟随、三 tab 与窄窗口可用 |
  | editor | IME、半角、括号、hold 补全、查找替换、书签、undo/redo、诊断跳转无焦点抢占 |
  | 创作联动 | caret/selection → timeline，Ctrl+touch 编辑只在有效 editor state 触发 |
  | 生命周期 | root 拖放、脏文档关闭、播放中关闭、ChartDrop cancel 不遗留 overlay |

- [ ] **Step 4: 更新 checklist、运行 `git diff --check`、提交。**

  `git commit -m "docs(qml-ui): record phase 2 verification"`

## 执行顺序与切分

严格按 Task 1 → 10 执行；每个 Task 独立提交、在通过自己的失败/通过测试后才进入下一项。Task 5 与 Task 7 可在不同工作树并行，但 Task 6 依赖 Task 1 的 revision 快照，Task 8 依赖 Task 7，Task 9 依赖 Task 8。阶段 1 通过前不得开始把诊断/Muri 的旧 Widget panel 接回 v2；阶段 2 通过前不得宣称 v2 可替代 v1。

本阶段结束后，才进入预览/导出字段 parity、全屏产品决策、菜单/工具接线与 Latency 原生化。每次修改 `&first`、timeline、validation/Muri、preview 或 export 时，必须复核 `.agents/skills/miacode-dev-guide/references/cross-chain-linkage.md` 的对应同步对。
