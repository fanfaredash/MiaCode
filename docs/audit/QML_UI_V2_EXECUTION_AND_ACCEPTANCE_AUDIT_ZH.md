# QML UI v2 执行、修复与人工验收审计

> 状态：**五项验收门槛已于 2026-08-24 复验通过**（macOS 单平台观察，不替代跨平台回归；见文末“复验结果”）。
> 本文件记录原计划的实施提交、首次阶段完成后的缺陷修复、2026-08-24 的原生桌面验收结果、
> 针对该次失败项的根因修复轮，以及修复后的复验结论。
> 它不替代原始计划；原计划见 [QML_UI_V2_PHASE1_PHASE2_EXECUTION_PLAN_ZH.md](QML_UI_V2_PHASE1_PHASE2_EXECUTION_PLAN_ZH.md)。

## 原始计划与实施提交

原计划以 `origin/feature/qml-ui` 的 `0c58201b` 为基线，要求依次完成 Task 1–10、每个 Task 单独提交。实际记录如下：

| Task | 提交 | 结果摘要 |
| --- | --- | --- |
| 1 | `a87aeffe` | 文档诊断 revision-aware 投影与过期缓存门控。 |
| 2 | `dc42f2f6` | 同一 document presentation snapshot、完整源码两阶段预检。 |
| 3 | `269abdb3` | QML root lifecycle、ChartDrop 和 overlay 生命周期。 |
| 4 | `1b7b9679` | 阶段 1 验证记录与相关规格/构建修正。 |
| 5 | `39ff3979` | v2 Timeline 控制、tab 状态和高度持久化首次迁移。 |
| 6 | `0edd4ce4` | validation/Muri 对齐分析面板及 revision-safe 导航。 |
| 7 | `80da128a` | v1/v2 共用 `SimaiTextEditPolicy`。 |
| 8 | `74aff5e4` | QML editor controller、IME/paste bridge 与无焦点 completion popup。 |
| 9 | `298c0c8b` | 查找替换、书签、caret/touch-pad 路径与 QML handler fallback 防护。 |
| 10 | `a8b30dac` | 阶段 2 文档、完整 Release 构建与针对性 CTest 记录。 |

Task 10 当时没有把桌面 GUI 手工矩阵标记为通过；这是正确的保守记录。随后原生桌面验收发现运行时断链，进入以下修复阶段。

## 首次阶段完成后的根因审计与修复

| 提交 | 修复的根因与范围 |
| --- | --- |
| `2990a9c0` `fix(qml-ui): synchronize document replacement analysis` | 统一 document replacement 的 QML 状态发布；静默 reset editor tabs，避免重复选择 difficulty 使 revision/慢分析失效；显式 validation 调度同 revision Muri 分析；validation、Muri 与 static refs 写齐 provenance 后才通知。 |
| `a6f57953` `fix(qml-ui): restore timeline follow controls` | 恢复四个 follow 状态及 `followProgress` 持久化；移除底栏 340px 硬上限；将 divider 高度回写 controller；恢复 Timeline header 安全边界和正确坐标；后台 Tab 不再阻断播放期间的 editor-follow 调度。 |
| `714bbdf3` `fix(qml-ui): restore editor navigation and authoring` | 建立 backend → QML editor 的难度/revision-safe navigation；补齐 completion 键盘路由、正文右键菜单、统一 caret 居中、按难度书签侧栏与标题难度；使 Ctrl+preview authoring 走 focus/IME/revision gate、单次 undo 和 preview seek；导航仅在可见、非 metadata、匹配难度/revision 的编辑器中确认。 |

这些提交均有定向 CTest、Release `MiaCode` 构建与 `git diff --check` 证据；自动证据不能代替下列原生 GUI 观察。

## 2026-08-24 原生桌面验收结果

以下结果来自实际桌面运行。`通过` 仅表示本次观察没有发现问题，并不替代跨平台回归。

| 项目 | 本次结果 | 备注/后续动作 |
| --- | --- | --- |
| Validation | 通过 | 本次未发现检测问题。 |
| 切换 Chart | 通过 | 本次未发现状态滞留。 |
| Timeline ↔ preview | 部分通过 | 播放时代码跟随可移动光标；暂停时不能正确移动。`Command`+点击文本仅移动 Timeline，预览未同步。 |
| Timeline 缩窄/缩放 | 待复测 | 已修正高度/几何链；本轮没有明确确认最终视觉和命中效果。 |
| Completion popup | 失败 | 弹出位置与样式不符合预期；Up/Down/Tab/Enter/Escape 没有可感知的高亮/选择反馈。 |
| 编辑器正文右键菜单 | 失败 | 菜单没有正常工作。 |
| 书签 | 通过 | 本次未发现问题。 |
| 标题当前难度 | 通过 | 标题已包含当前难度。 |
| `Command+Z` | 失败 | 某些情形失效并输入字母 `z`。 |
| IME：输入 `a` 后直接 Enter | 失败 | 会插入大量重复 `a`，属于 P0 文本损坏问题。 |

## 2026-08-24 验收失败项的根因修复轮

本轮针对上表的失败项定位根因并修复。按分支既定要求，每项都先补可执行的失败回归（真实 QML
组件、真实 `QKeyEvent` / `QInputMethodEvent` / `QTest` 鼠标事件，不用源码字符串扫描代替），
确认回归在修复前为红、修复后为绿，再单独提交。

为此新增了两项测试基础设施，二者都只在 `MIACODE_BUILD_DEV_TOOLS` 下构建：

- `MiaCode.UI` 模块被镜像到 `build/qml_spec_imports/MiaCode/UI/`（qmldir + `configure_file` 复制），
  spec 因此可以 `import MiaCode.UI` 并实例化**真实的** `SourceEditor.qml` / `CompletionPopup.qml`，
  而不是在测试里重抄一份副本。模块的 C++ 元素由 spec 自行 `qmlRegisterType`。
- dev-tools 引入 `Qt6::Test`，用于把真实鼠标/键盘事件投递进 `QQuickWindow`。

| 提交 | 门槛 | 确证的根因 | 回归 |
| --- | --- | --- | --- |
| `75c89635` | `Command+Z` 输入字母 `z` | 策略拒绝带命令修饰键的按键后，QML 未接受事件，`QQuickTextEdit` 落到原始插入分支。Qt 的文本控件只挡 Ctrl，不挡 Meta/Super，因此 macOS 物理 `Control+Z`（`Qt::MetaModifier`）与 Windows `Super+Z` 会把字面字符写进谱面。实测：`Meta+Z→zabc`、`Meta+C→cabc`、`Meta+A→aabc` | `SimaiTextEditPolicySpec` 固定抑制矩阵；`QmlEditorControllerSpec` 用真实 `TextArea` + 真实 `QKeyEvent` 断言文档未被改动 |
| `5ee23d38` | completion popup 位置、样式、候选高亮与键盘反馈 | delegate 声明了 `required property string modelData`，整个 delegate 切换到 required 属性模式并关闭上下文属性注入，同级的裸 `index` 因此报 `ReferenceError: index is not defined`：`highlighted` 永远为假、`selectCompletionIndex(index)` 抛错。另外竖向 `ListView` 的 `contentWidth` 恒为 -1，`implicitWidth: contentWidth` 把每次会话都压到最小宽度；锚点只绑定 caret 矩形且经过非响应式的 `mapToItem`，滚动/缩放后失效并在末行溢出遮罩底部 | 载入真实 `CompletionPopup.qml`，断言高亮跟随 `completionIndex`、宽度由候选文本测得、锚点在 caret 下方并在底部翻转。旧文件上复现同样的 `index is not defined` |
| `2a27630d` | 编辑器正文右键菜单 | 正文用 `TapHandler` 承接右键，但 `TextArea` 在 press 时取走鼠标 grab，`ReleaseWithinBounds` 手势在其内部永不完成。隔离实验：纯 `Rectangle` 上 `TapHandler` 命中 1 次、`TextArea` 内 0 次，而 `MouseArea` 两处都是 1 次（行号 gutter 菜单一直用的就是 `MouseArea`） | 载入真实 `SourceEditor.qml`，用 `QTest` 投递真实右键；另断言 Menu 键 / `Shift+F10` 键盘路径 |
| `f451ae09` | `Command`+点击文本未同步预览 | v1 的 ctrl-click 跳转装在**隐藏** widget viewport 的事件过滤器上，v2 可见的 QML 编辑器没有对应路径，点击只经 caret 桥移动了时间轴光标 | 真实 `SourceEditor.qml` 上投递真实 `Ctrl+左键`，断言普通点击不 seek、Ctrl 点击 seek 到刚落下的 caret 行列 |
| `ca943a82` | 暂停状态的 preview-follow | 跟随有两种模式：播放且开启代码跟随时移动光标（走 QML navigation 请求），暂停或关闭时只画装饰——而该装饰此前只渲染在隐藏的 v1 `PlainCodeEditor` 上，所以 v2 暂停后完全没有可见跟随 | 路由值断言身份、可见性与 metadata 模式的门控；真实 `SourceEditor.qml` 上驱动装饰信号，断言解析出的偏移、过期 revision 丢弃与清除 |
| `bbd5e3b8` | IME commit 后 Enter 重复插入 | 当轮未确证：合成 preedit→commit 序列只得到 1 次事务、1 个字符，说明触发点在平台输入法序列。先落诊断，不猜修复 | 记录每个事件的 commit/preedit 长度、replacement 区间、attribute 种类、composing 状态，以及在 QML 事务两侧采集的 commit 深度 |
| `f4251ca0` | 同上，**由诊断日志确证并修复** | 递归是自己造成的：桥在事件过滤器内同步改文档时，`QQuickTextEdit` 仍持有本次事件的 preedit；在活动 preedit 下移动光标使 `QQuickTextControl` 通过平台输入法上下文提交该 preedit，平台随即把**同一段 commit** 再投递回过滤器，桥又应用一次 | 用真实 `QInputMethodEvent` 驱动真实桥接，并在 applied 回调里按平台方式重投递（上限 40 次，使未加护栏的桥以 `applied=41` 失败而不是爆栈） |

### IME 重复插入的日志证据（2026-08-24 用户复现）

一次按键（输入 `a` 后回车）在 `Chart/Axeria/.miacode/logs/miacode_runtime_debug.log` 中留下：

- `editor/ime_event` 632 条、`editor/ime_commit` 631 条；
- `seq=1` 是 preedit，`seq=2` 是唯一真实提交（`reentrant=0`）；
- `seq=3` 起 `depth` 依次为 1、2、3 … 一路到 **630**，全部 `reentrant=1`、`commit_len=1`；
- 提交行按 `632→2` 的**逆序**回落，是典型的递归展开；
- 全过程约 1.2 秒，一次击键写入 631 个字符。

修复后的不变量：**一次平台提交 = 一次文档事务**。再入的提交由下层帧负责，本层剥离 commit 后丢弃；
`depth` 仍然记录，因此后续采集依然能看出平台是否再入，只是再入不会再复制文本。

### 本轮证据

- Release `MiaCode` 构建成功（Qt 6.10.2，macOS）。
- `ctest -C Release -R 'qml_.*_spec|simai_text_edit_policy_spec|plain_code_editor_spec|simai_completion_catalog_spec|timeline_model_spec|timeline_marker_offset_spec|muri_spec|touch_pad_authoring_state_spec|simai_document_spec'`：14/14 通过。
- 每个修复都单独验证过“修复前回归为红、修复后为绿”。

### 本轮未能执行的验证

- **原生桌面观察不是在本会话内完成的。** 会话没有显示器/截屏权限（`screencapture` 返回
  `could not create image from display`），修复轮本身只有自动证据；复验由用户在本机另行执行，
  结果记在下面的“复验结果”一节。
- **IME 重复插入随后已由该诊断确证并修复**（`f4251ca0`）：用户以 `--debug` 复现后，日志显示同一次
  提交递归到 `depth=630`、`reentrant=1`，即 QML 事务重入了输入法上下文。证据与结论见上文。
- **窄窗口 Timeline 缩放与 header 命中在本会话内未观察**（已由用户复验通过）。静态复查显示该链路已有防护：
  `TimelineSceneStateBuilder` 用 `max(headerSafeLeft, headerRightLimit)` 且对视口宽度取
  `min`，因此即使 `BottomPanel.qml` 在极窄面板下把 limit 传成倒置或越界，也会收敛而不是画错。
  但这只是读码结论，不能当作桌面观察通过。

## 验收门槛的复验结果（2026-08-24，修复轮之后）

以下五项在修复轮之后由用户在 macOS 上做了原生桌面复验，结果为**通过**。与本文其它“通过”
一样，它表示本次观察没有发现问题，**不替代 Windows 侧与跨平台回归**。

| # | 门槛 | 结果 | 对应修复 |
| --- | --- | --- | --- |
| 1 | 暂停状态的 preview-follow 与 `Command`+点击文本后的 preview seek | 通过 | `ca943a82`、`f451ae09` |
| 2 | completion popup 的光标锚定、主题样式、当前候选高亮以及 Up/Down/Tab/Enter/Escape 的视觉反馈 | 通过 | `5ee23d38` |
| 3 | 编辑器正文右键菜单的鼠标与键盘 context-menu 路径 | 通过 | `2a27630d` |
| 4 | `Command+Z` 绝不降级为文本输入；IME commit 后直接 Enter 不重复插入 | 通过 | `75c89635`、`f4251ca0` |
| 5 | 窄窗口 Timeline 缩放、header hit target 与四个 follow 状态 | 通过 | `a6f57953`（几何链），本轮仅复验 |

因此阶段 2 的这五项门槛不再阻塞后续工作。**但阶段 2 整体仍未完全验收**：同一次桌面验证
另外发现了三项功能缺口（切换文档后 PV、撤销栈、快捷键体系），见下节；Windows 侧尚未验证。

后续修复继续遵守同一流程：先添加可执行的失败回归（优先真实 QML、`QInputMethodEvent` 和快捷键事件，
不以源码字符串扫描替代），再实现最小修复；每个独立修复提交后重新运行 Release 构建、相关 CTest
和对应的原生桌面用例。

## 2026-08-24 GUI 验证新发现（本轮不处理，仅登记）

以下三项来自同一次桌面验证，不属于原五项验收门槛，**本轮明确不做**，登记以免遗失：

| 现象 | 初步判断 | 处理时机 |
| --- | --- | --- |
| 切换文档后 PV 存在问题 | PV 侧**无缺陷**（日志确证媒体路由与文档同刻切换）。实际问题是编辑器仍显示旧谱面，**多次复现失败，需求延后** | 埋点保留，待日后自然复现再取证 |
| 撤销栈存在问题 | **部分修复，仍未关闭。** 已修（`581b782b`）：`resetQmlHistory()` 在每次 controller 来源的文本同步时清空历史，而应用一次编辑正是会把新文本写回文档并同步回来——所以每一步刚记录就被抹掉；undo/redo 也从全文替换改为最小差异替换并选中所恢复的文本。**未修**：`Ctrl+Z` / `Ctrl+Y` 与文档置脏标记的联动 | 待修：见下方“撤销与置脏联动” |
| 快捷键体系缺失 | 已确证并修复（`676150e0`）：成因不是缺注册层，而是**绑定上下文**——v1 的变换/预览 QAction 用 `Qt::WindowShortcut` 挂在隐藏且永不激活的 MainWindow 上，因此在 v2 从不触发。`ShortcutRegistry` 本身零 QtWidgets 依赖（Qt 6 中 `QAction`/`QShortcut` 已属 QtGui），故复用而非另建 | 已完成 |

### 切换文档排查记录（PV 已排除；编辑器停留在旧谱面仍未定位）

本轮按链路逐点核对，**以下五处均正常**，因此不做推测性改动：

1. `syncPreviewStageMediaRouteChartPath()` 在文档加载时经 `activateInitialField()` 调用，会把新
   chart path 与 `&video=` override 推给 `PreviewStageMediaHost`。
2. `quickShellStartupStageMediaLoadDeferred_` 是**一次性启动闩**（`FrameBootstrap.cpp` 置位，首次
   flush 清除），没有按文档重新置位，所以后续切换走的是立即分支而非被延迟吞掉。
3. v2 的开档入口 `QmlDocumentModel::openFile()` → `MainWindow::openStartupTarget()`，对文件路径
   直接转 `openFileAtPath(path, true, true)` —— 与 v1 `onOpenFile()` 落到同一个函数。

   > **更正：** 早前记录过“v2 未走脏文档确认、会静默丢弃未保存修改”，这是错的。v2 的确认在 QML 层
   > （`MainView.requestOpenFile()` → `unsavedChangesDialog`，含 Save / Discard / Cancel 三个分支），
   > 只是不经由 C++ 的 `maybeSaveBeforeContinue()`。当时只查了 `openFileAtPath` 的调用链就下了结论。
   > 两条分支值得在复现时留意：`Save` 分支在 `saveDocument()` 返回 false 时静默丢弃待打开的文件
   > （`clearPendingAction()`，无任何提示）；`Discard` 分支会先把当前文件重新打开一次再打开目标文件。
   > 两者都尚未被证明与本问题相关，仅作为复现时的观察点。
4. `TimelineSceneStateBuilder` 等下游对越界/倒置输入已有收敛，不会因切换产生脏几何。
5. QML 侧预览投影由 `QuickShellController` 的轮询 `refresh()` 驱动并发 `shellStateChanged`，
   不依赖 `documentReplaced`，因此不会长期停留在旧文档的投影上。

**2026-08-24 复现（Axeria → Eve Avenir，pid=78454，build `581b782b`）的日志结论：**

| 时刻 | 事实 |
| --- | --- |
| 15:20:59.380 | 日志目录绑定 Axeria；15:21:00 `set_chart_path` = Axeria |
| 15:21:19.702 | 日志目录重绑 **Eve Avenir**；`crash_recovery` 的 autosave 目录同时切到 Eve |
| 15:21:19.717 | `set_chart_path` = **Eve Avenir**，`bind_video_output attached=1` |

因此 **PV / 媒体路由在文档加载的同一毫秒就正确切换了**，之前“PV 落后 25 秒”的读法是错的：
那是只看了日志末尾几条造成的误读，15:21:44 的那批 `set_chart_path` 是用户随后手动切难度触发的。
用户观察到的“PV 变成新 PV”与日志一致，**PV 侧无缺陷**。

问题因此收敛为：**编辑器仍显示旧谱面**。但这一点日志答不了 ——

- `quick_timeline_perf lines=107` 在切换前后完全相同（Axeria 难度 5 是 109 行、Eve 是 110 行，
  解析后都落在 107），
- `muri_perf validation_issues=12` 前后也相同。

**这两个量都不能当作文档身份信号**，早前据此判断“后端已经换文档”是错的。日志里没有任何直接记录
文档身份的字段，所以本轮没有修复，改为落地一对可判定的埋点（`f...` 见下）。

**下一步需要的证据：** 带 `--debug` 复现同一路径，然后 grep 新增的两行：

```
grep -E "editor/document_replaced|editor/document_shown" <chart>/.miacode/logs/miacode_runtime_debug.log
```

- `document_replaced` 是 `QmlDocumentModel` 在替换落定时发布的（path / difficulty / revision /
  chart 字符数 / 难度数）；
- `document_shown` 是可见 `SourceEditor` 实际持有的字符数，并同时回读投影值。

`shown_chars` 与 `projected_chars` 一致即编辑器是最新的；不一致则问题在 QML 侧；若 `path=` 已经是新
谱面而 `projected_chars` 没变，则在后端投影侧。一次复现即可定位。

这三项都指向“功能完整性”而不是性能，与下节调整后的推进顺序一致。

### 撤销与置脏联动（2026-08-25 复核，未关闭）

**现象：** 把文档的全部修改逐一撤销回初始状态后，置脏标记不会复原——文档仍被标记为已修改。
`Ctrl+Z` / `Ctrl+Y` 与 dirty 状态之间没有正确的联动。

**范围界定：** 这与 `581b782b` 修掉的两点是不同的缺陷。那次修的是「历史被误清空」和
「undo/redo 的替换跨度与选区」，均已复验通过；本条是 dirty 标记本身的回退语义，未被那次改动覆盖。

**处理方向（所有者指定）：** 参考 v1 的实现算法。v1 的编辑器基于 `QTextDocument` 自带的撤销栈，
其 dirty 语义由文档自身维护；v2 的 QML 撤销栈是自建的，需要按同样的语义补齐回退时的 dirty 复原。

**未在本轮做代码分析**，按所有者要求仅登记。

## 与原计划的关系

- 原计划的 Task 1–10 实施提交保持不重写，以便追溯每项边界和规格的引入时间。
- `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md` 是当前手工验收摘要；本文件提供提交级历史和失败项。
- 当前分支在三个后续修复提交之后，又加入了本文“根因修复轮”一节的六个修复提交与一个诊断提交。
  五项验收门槛的成因均已定位、修复，并于 2026-08-24 通过 macOS 原生桌面复验。
  阶段 2 仍未整体验收：另有三项功能缺口待处理，Windows 侧未验证。

## Windows v2 预览性能审计、插桩与修复（2026-08-25）

### 触发与证据边界

用户在 Windows 上观察到切换 v2 UI 后预览帧率明显下降；后续还确认，在 v2 编辑器工作流
（2026-08-24）接入前，v2 性能更好。不能把早于 v2 的固定帧门控、D3D11VA 两设备桥或旧版
代码跟随功能直接当作版本切换根因，故本轮先在 `--debug` 下加入低扰动汇总插桩，再由用户在
`超熊猫的周遊記（ワンダーパンダートラベラー）` 进行真实播放。

首轮三次播放给出三条可重复的 v2 热路径：

1. 普通工作区同时实例化工作区、隐藏紧凑面板与隐藏全屏三份 `PreviewSurface`。三份
   `PreviewQuickSceneRoot` 都订阅 `PreviewRuntime::frameStateChanged`，所以不可见根仍收到
   `update()`；这不是视频多 sink（普通工作区只有可见 surface 附着 VideoOutput），而是 QML/QSG
   scene-root fan-out。
2. `QuickShellController` 播放期约 16 ms 的统一状态刷新会令 `QmlPreviewModel::changed` 使全部
   属性失效。原 `statistics()` 每次都新建六项 `QVariantMap`、解析字符串、生成图标 URL，并调用
   `resolvePreviewSkinDir()`；后者检查 `tap.png`、`hold.png`、`star.png` 是否存在。两个常驻
   `PreviewPane` 令一次状态刷新在日志中出现四次统计读取。
3. v2 的 QML reverse-navigation 绕过了旧 `QTextCursor` 路径的 `alreadyAtSelectionEnd` 短路。
   即使 playhead 仍在同一 token，`SourceEditor.selectBackendNavigation()` 仍执行 `select()`、
   跟随装饰与 `Qt.callLater` 居中。成功样本中每秒 47–61 次请求，大量窗口为同一目标；单次
   同步部分为 1.1–2.1 ms，最高 8.804 ms，且异步滚动/重绘不在该计时内。

一次开启跟随后的播放（pid 27796）在约 258 ms 后非正常终止；下一进程消费
`abandoned_session_marker`。该次没有 `fatal`、WER/AppCrash 或 dump，且紧接着的相同开启跟随
播放可持续完成，因此不能把这一次归因成已经定位的 C++/驱动崩溃；本轮修复消除其高频触发条件，
但不宣称闪退根因已关闭。

### 已落地修复

- `PreviewPane` 的 chart `PreviewSurface` 改为 Loader 生命周期；紧凑预览也由 Loader 创建，
  全屏激活时工作区 surface 卸载，保证同一时刻只有一个 root 订阅运行时。
- `QmlPreviewModel` 改为缓存的细粒度投影：播放位置、transport、playing、渲染模式与统计使用
  独立通知；统计仅在统计文本或皮肤变动时重建。播放位置不再触发文件存在性检查或 QML model
  重设。
- `TimelineSection` 缓存已被 QML 接受的跟随导航目标；相同 selection/已完成居中的请求直接返回。
  `SourceEditor` 也保留同 selection 的防御性短路。跟随绑定缓存失效时同步使导航 cache 失效，
  不复用过期文档/难度目标。

### 修复后真实播放复验

pid 28064 的两段开启跟随播放均正常 pause/close，`exit_code=0`。第二段的关键汇总为：

| 指标 | 修复前样本 | 修复后样本 |
| --- | --- | --- |
| 运行时 scene root | 3（1 visible + 2 hidden） | **1 visible** |
| 隐藏 root 分发 | 每可见分发另有 2 次 | **0** (`v2ui_root_dispatch_hidden=0`) |
| 统计投影 | 147 s 内 20,496 次读取（每 controller refresh 4 次） | 71 s 内 399 次重建；皮肤解析 0 ms |
| QML 跟随 | 47–61 次/s，重复目标占多数 | 3–32 次/s，已发请求 `same_target=0` |
| 播放期 present | — | **59.9504 FPS**（60 Hz 目标） |

`qml_ui_bootstrap_lifecycle_spec` 与 `quickshell_preview_surface_policy_spec` 在 Windows Release
通过，Release `MiaCode` 构建成功。`qml_editor_controller_spec` 在本机 Windows 环境挂起；其
可执行文件未因本轮 app/QML 改动重建，故不将该结果记为本轮通过或回归，需另行处理其测试依赖。

### 仍未关闭：高位但稳定的播放内存

本轮日志不支持“播放时每帧持续泄漏”。第一段暂停后 private memory 从 1006 MB 降到约 958 MB，
随后在 120–330 s 保持 957 MB；第二段开始后从该平台跃升到 1060 MB，随后的 30/60 s 样本为
1051/1064 MB，暂停时为 1049 MB。即“首次播放/恢复后跃升到高平台，再在窄范围内稳定”。
两段 `d_play_kb` 仍为约 +180 MB / +87 MB，远高于历史健康阈值；但 QSG 纹理仅约 22–26 MB，且
GPU local usage 在暂停后下降，下一轮应拆分 QtAVPlayer/D3D11VA 帧池、QML/Qt Quick 资源和其它
私有堆的保留关系，不能把它误报为已证实的纹理泄漏。

## Windows v2 工具箱批量上传入口手工验证（2026-08-26）

使用 `build/Release/MiaCode.exe` 在本机 Windows 桌面原生窗口运行验证，未使用 offscreen、静态审阅
或 CTest 代替 GUI 观察。应用启动后先处理“上次未正常退出”提示，随后通过 Windows UI Automation
依次选择 Activity Bar 的“工具”、工具侧栏的“Net 批量上传”。操作后观察到独立 `QDialog` 窗口，
窗口标题为“Net 批量上传”，证明入口已实际打开 `net.batchUpload.open`。
