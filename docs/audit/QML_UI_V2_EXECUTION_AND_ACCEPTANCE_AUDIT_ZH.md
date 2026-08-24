# QML UI v2 执行、修复与人工验收审计

> 状态：**未验收通过**。本文件记录原计划的实施提交、首次阶段完成后的缺陷修复、2026-08-24 的原生桌面验收结果，
> 以及针对该次失败项的根因修复轮。它不替代原始计划；原计划见 [QML_UI_V2_PHASE1_PHASE2_EXECUTION_PLAN_ZH.md](QML_UI_V2_PHASE1_PHASE2_EXECUTION_PLAN_ZH.md)。

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

- **原生桌面观察未执行。** 本次会话没有显示器/截屏权限（`screencapture` 返回
  `could not create image from display`），因此上述修复**都没有**经过原生 GUI 复验。自动证据不能
  替代它，下节的验收门槛因此仍然全部开启。
- **IME 重复插入未修复**，只落了诊断。需要在装有真实输入法的机器上以 `--debug` 复现一次，
  查看 `miacode_runtime_debug.log` 的 `[runtime/editor/ime_event]` 与 `[runtime/editor/ime_commit]`：
  若同一次提交出现多条 `ime_event`，问题在平台投递；若 `reentrant=1`，则是 QML 事务重入了输入法上下文。
- **窄窗口 Timeline 缩放与 header 命中未观察。** 静态复查显示该链路已有防护：
  `TimelineSceneStateBuilder` 用 `max(headerSafeLeft, headerRightLimit)` 且对视口宽度取
  `min`，因此即使 `BottomPanel.qml` 在极窄面板下把 limit 传成倒置或越界，也会收敛而不是画错。
  但这只是读码结论，不能当作桌面观察通过。

## 未关闭的验收门槛

在以下项目均通过原生桌面复验前，QML UI v2 **不得**宣称阶段 2 已完全验收：

1. 暂停状态的 preview-follow 与 `Command`+点击文本后的 preview seek。
2. completion popup 的光标锚定、主题样式、当前候选高亮以及 Up/Down/Tab/Enter/Escape 的视觉反馈。
3. 编辑器正文右键菜单的鼠标与键盘 context-menu 路径。
4. `Command+Z` 绝不降级为文本输入；IME commit 后直接 Enter 不重复插入（成因已确证并修复，仍需桌面复验）。
5. 窄窗口 Timeline 缩放、header hit target 与四个 follow 状态的桌面观察。

后续修复必须先添加可执行的失败回归（优先真实 QML、`QInputMethodEvent` 和快捷键事件，不以源码字符串扫描替代），再实现最小修复；每个独立修复提交后重新运行 Release 构建、相关 CTest 和对应的原生桌面用例。

## 2026-08-24 GUI 验证新发现（本轮不处理，仅登记）

以下三项来自同一次桌面验证，不属于原五项验收门槛，**本轮明确不做**，登记以免遗失：

| 现象 | 初步判断 | 处理时机 |
| --- | --- | --- |
| 切换文档后 PV 存在问题 | 疑似 document replacement 后预览运行时/媒体绑定的状态未随新文档重建。与既有 `documentReplaced` 延迟投影链相关，需要单独定位 | 五项门槛复验通过后，优先级最高的一项 |
| 撤销栈存在问题 | `QmlEditorController` 的 QML undo 栈在 `syncTextFromController()` 触发 `resetQmlHistory()` 时被整体清空（切换难度、文档重载、元数据模式切换都会触发）；且 `undoQmlTransaction()` 以全文替换的形式回放，与 `onTextChanged` 记录的增量条目混用 | 与 PV 一并作为下一批功能修复 |
| 快捷键体系缺失 | v2 主壳没有统一的快捷键注册层；v1 走 `ShortcutRegistry` + `QAction`，v2 目前只有 `SourceEditor.qml` 内的零散 `Keys.onPressed` 分支。菜单项也普遍不显示快捷键 | 需要先定产品决策（v2 的快捷键归属与可配置性），再实现 |

这三项都指向“功能完整性”而不是性能，与下节调整后的推进顺序一致。

## 与原计划的关系

- 原计划的 Task 1–10 实施提交保持不重写，以便追溯每项边界和规格的引入时间。
- `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md` 是当前手工验收摘要；本文件提供提交级历史和失败项。
- 当前分支在三个后续修复提交之后，又加入了本文“根因修复轮”一节的五个修复提交与一个诊断提交。
  五项验收门槛的代码侧成因已定位并修复（IME 重复插入除外，仅落诊断），但门槛本身仍需原生桌面复验才能关闭。
