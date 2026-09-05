# `feature/qml-ui` 相对 `dev` 的功能差距与补完清单（初版）

> **⚠️ 本文已作废（2026-08-29）。**
> 审计基线为 `origin/feature/qml-ui` @ `ea31fee6`，其中 A/B/C/D/E 各表的 P0 与多数 P1 均已在
> `112434b2`…`117a76a1` 之间完成；「共同基线之后必须同步的 29 个 dev 提交」也已合入。
> 现行工作清单以 [../specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md](../specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md) 为准。
> 本文仅保留为历史差距审计记录，不得据此排期。

> 目的：为 `feature/qml-ui`（QML UI v2）回归 `dev` 的功能面提供可排序的清单和初步补完路线。
> 本文是静态代码与提交历史审计，不替代 Windows/macOS 的实际 GUI 回归。

## 审计基线与判定方式

- 比较对象：`dev` `61c0ec9f`（2026-08-21）与 `origin/feature/qml-ui` `ea31fee6`。
- 共同基线：`c68baa34`（2026-08-13）。因此 QML 分支在提交历史上少了 29 个 `dev` 提交；这只是“后续同步缺口”，并不等于全部 UI 功能差距。
- 本文中的“缺少”指 **v2 可见 UI 无法完成、只做了展示、或没有遵守 `dev` 既有联动契约**。后台代码仍存在、但只能通过 Widgets/v1 页面使用的项目，单列为“复用/待原生化”，不误报成后端缺功能。
- 优先级：P0 = 会导致数据、预览/导出结果或基本入口错误；P1 = 日常工作流明显降级；P2 = 原生化、体验或长期维护项。

## 结论

v2 已经不是空壳：基础打开/保存、难度切换、QML 文本编辑、真实 `TimelineQuickItem`、预览运行时、单个/批量视频导出都有入口。但它目前属于“可启动的工作台原型”，还不能替代 `dev`/QuickShell：

1. 文档编辑到 timeline、校验、Muri、预览、导出的状态链没有统一边界，部分 QML setter 直接写 `MainWindow` 私有状态；
2. 编辑器和 timeline 只覆盖基本显示，缺少高频编辑与诊断工作流；
3. v2 独有的预览/导出、启动和关闭路径尚有未接线项；
4. 分支尚未并入共同基线之后的 preview/audio/timeline/export 修复，先继续开发会放大冲突和回归风险。

推荐顺序是：**先同步 `dev` → 收紧 QML 后端边界并补 P0 联动 → 补编辑器/timeline/诊断 → 完成预览与导出细节 → 再逐步去除 Widgets 页面宿主。**

## 已可用且不应重复建设的能力

| 能力 | v2 状态 | 说明 |
| --- | --- | --- |
| 谱面文件打开/保存、难度增删、基础元数据与正文编辑 | 已有 | `QmlDocumentModel` + `QmlCommandService` 已直接复用 `SimaiDocument`/`MainWindow`。 |
| 预览绘制和基础播放 | 已有 | 复用 `QuickShellController`、`PreviewRuntime` 与 `QuickShellPreviewSurface`。 |
| 时间轴主体 | 已有 | `BottomPanel.qml` 已装载真实 `TimelineQuickItem` 与 `timelineStateBridge`。 |
| 单个与批量视频导出 | 已有但须回归 | `QmlExportSession` 复用现有 seed、snapshot、worker 和 batch 管线。 |
| 延迟校准、媒体处理、规范化、Net、封面、ZIP | 功能可达，非原生 | 分别打开既有 Widgets 页面/对话框；它们是迁移对象，不是应重写的后端。 |

## 功能缺口清单

### A. 启动、窗口与文档生命周期

| 优先级 | 缺口 | 证据与影响 | 初步补法 |
| --- | --- | --- | --- |
| P0 | v2 root window 未登记给 `MainWindow` | `QmlUiBootstrap::start()` 没有调用 `setQuickShellRootWindow(rootWindow)`；分支既有 Todo 也明确列出。隐藏的 Widgets 后端因此无法可靠地把对话框 transient parent、关闭和窗口级状态绑定到可见 QML 根窗。 | QML root 创建后立即登记，析构/关闭时清除；对脏文档、播放中、模态对话框三种关闭路径做手工回归。 |
| P0 | 音频拖放建谱/ChartDrop 未接线 | v1 有 `ChartDropOverlay`、`handleChartAudioDrop` 和 root-window 事件过滤；v2 bootstrap 没有相应安装和视觉投影。 | 将拖放事件、取消和 overlay 可见状态接入 `QmlUiBootstrap`/QML；复用现有 `MainWindow` 逻辑，不新建第二套建谱流程。 |
| P1 | File 菜单和命令面过窄 | QML 命令服务只覆盖 open/save/save-as/discard/validate/难度；菜单没有新建、备份恢复/历史等 v1 文件工作流。 | 先列出 v1 QAction→QML command 映射；先接“新建、恢复备份、最近/重开”高频项，其他统一从同一个 command service 进入。 |
| P1 | 全局菜单/快捷键大量缺失或是死控件 | `MainMenu.qml` 的查找、选择当前行、关于为 disabled；工具栏的音频/预览设置也为 disabled。 | 无后端入口的先隐藏；有既有 `MainWindow` slot 的接到 command service，避免留灰色伪功能。 |

### B. 文档边界与各种联动

| 优先级 | 缺口 | 证据与影响 | 初步补法 |
| --- | --- | --- | --- |
| P0 | QML 直接写入 `MainWindow` 私有数据 | `QmlDocumentModel` 以 friend 方式写 `document_`、`activeDifficultyId_`、`documentDirty_` 等。它绕过 `applyCurrentFieldToDocument`、统一脏状态和部分页面同步入口。 | 先定义窄的公开“文档编辑事务” API（正文、元数据、完整源码、难度与选择）；QML model 只调用它，`MainWindow` 仍为 timeline/preview/export 的唯一编排者。 |
| P0 | `&first` 的 v2 编辑位置和实时读值冲突 | v2 表单把 `metadataFirst` 放在元数据页；`dev` 的权威交互是难度页 `firstEdit_`。`parsedRawFirstSeconds()` 在有活动难度时优先读旧的 `firstEdit_`，而 v2 setter 只改 `document_.first`，所以时间轴/预览可继续使用旧偏移。 | 统一到 `MainWindow` 的 offset 更新 API：同步 document、隐藏 v1 header、latency 页、时间轴快/慢刷新、SFX/媒体偏移；v2 UI 再决定是否在难度 header 展示。 |
| P0 | 原始元数据/完整源码编辑缺少原子校验与错误反馈 | `setMetadataSourceText()` 直接 `SimaiDocument::fromText()` 并清空错误；`metadataSourceError` 没有真正的失败路径。 | 引入“解析→验证→原子替换/保留旧文档”的事务结果；向 QML 返回结构化错误与位置，并覆盖不合法源码、未保存修改、外部重载。 |
| P0 | 编辑后的下游刷新没有契约化 | 正文当前只 `setEditorText()` + `scheduleTimelineRefresh()`；元数据各 setter 又各自处理。`dev` 的链是编辑→quick 增量/重建→slow parse→预览快照→校验/Muri。 | 把字段按影响面分类（正文、`&first`、timing metadata、媒体路径、展示性元数据），每类由一个后端入口完成所有必要更新；用 revision 防止 QML 回写循环和过期慢刷新覆盖。 |
| P1 ✅ | 统一谱师只覆盖已有难度 | ~~QML `designerCandidates()` 和 `enableUnifiedDesigner()` 遍历 `difficultyIds()`~~ | 已完成：模式下沉为 `ChartWorkspace` 会话不变量（`setUnifiedDesignerEnabled`/`designerSlots`），候选与广播都走 `perDifficultyDesigners()`（含 chart-less slot）；UI 换成 `DesignerSlotsDialog.qml`，偏好只在对话框提交与加载自愈两处写盘。 |
| P1 | 文档状态投影不完整 | QML tab 的 dirty 只按当前 active difficulty/metadata 粗略标记，且缺少完整的 autosave、光标/选择、动作可用性投影。 | 建立只读 `DocumentUiState`（活动难度、dirty 原因、可预览/可导出、undo/redo、selection/cursor）；所有按钮都绑定它。 |

### C. 文本编辑器

| 优先级 | 缺口 | 证据与影响 | 初步补法 |
| --- | --- | --- | --- |
| P0 | 语法诊断 UI 是空数据 | `QmlDocumentModel::{syntaxIssues,syntaxIssueCount,syntaxErrorCount,syntaxWarningCount,parsedNoteCount}` 全部返回 0/空列表；底栏“语法”因此总会显示未发现问题。 | 将 `runValidateSimaiSilently` 的结构化结果导出为 `QAbstractListModel`/QVariant list，带 revision、severity、起止位置；点击问题定位到 QML editor。 |
| P1 | 查找/替换、选择当前行未实现 | QML 菜单已展示但 disabled；这是长谱编辑高频操作。 | 先在 QML editor 实现查找栏、上下一个、替换/替换全部和当前行选择；与多标签的焦点/当前文档绑定。 |
| P1 | 输入增强缺失 | QML `TextArea` 没有 `PlainCodeEditor` 的半角转换、IME 策略、覆写模式、括号自动闭合/跳过/成对删除、`h` 持有时值建议、BPM 候选补全。 | 把“纯文本编辑策略”和 Simai completion catalog 提炼为可被 QML 调用的服务；补全弹窗由 QML 绘制，偏好仍走既有设置。 |
| P1 | 书签工作流缺失 | v1 有文件内 `&miacode_bookmarks`、gutter 创建/拖动、侧栏树、跳转/重命名/删除；QML gutter/侧栏只显示行号和难度。 | 先暴露 bookmark model 与 CRUD/jump action；再补 gutter 标记、右键和侧栏分组，复用现有文档存储与迁移。 |
| P1 | 编辑器选择→timeline/触控板创作未接通 | v1 依赖 caret token、selection 事件、`TimelineQuickModel::resolveTimelineSecondForCursor` 和 Ctrl+触控区编辑；QML editor 没有 selection 事件桥。 | 将 caret/selection revision 传入后端；完成触控板点击插入/删除和离散 seek 的同一条链。 |
| P2 | 编辑器显示与无障碍偏好未对齐 | 字体大小、行距、自动完成、IME 等既有偏好仅作用在隐藏的 Widgets editor；QML 自己有少量 `QSettings` 几何/字号。 | 收敛成一份偏好投影，QML 只消费；键盘导航、文本输入焦点和快捷键优先级纳入验收。 |

### D. Timeline、验证与 Muri

| 优先级 | 缺口 | 证据与影响 | 初步补法 |
| --- | --- | --- | --- |
| P0 | QML timeline 缺少缩放与亮度设置入口 | v1 `TimelineTabSurface.qml` 给 `TimelineQuickItem` 提供 zoom/menu 与 brightness settings 的命中控件，并设置 header 左右避让；v2 `BottomPanel.qml` 只有 item 本体，默认 header 限制使这些交互不可达。 | 在 v2 创建等价的 QML 控件/命中层，接 `openTimelineZoomMenu`、`openTimelineBrightnessMenu` 和 header limits；不要重做 QSG timeline。 |
| P0 | “跟随代码”开关缺失 | v1 BottomTabs 把 follow-code 开关绑定 `timelineStateBridge.followPreviewEnabled`；v2 虽接收 item 信号，却没有用户入口。 | 在时间轴 tab 条添加该开关，并把状态完全绑定 bridge，不在 `ViewState` 建平行副本。 |
| P0 | 验证与 Muri 底栏没有接入 | v1 有 timeline/validation/muri 三 tab 并为后两项挂现有 native surface；v2 只有 timeline 与一个空的“语法”列表。 | 第一阶段先以结构化 QML 模型替代 validation 列表，并决定 Muri 是临时宿主现有面板还是直接建 model；两者都要复用同一 slow-refresh revision。 |
| P1 | timeline 高度/内容缩放持久化与 v1 契约未对齐 | v2 独立保存 `bottomPanelHeight`，没有使用 v1 的 content-scale、header-scale 与 timeline 两层缩放规则。 | 将 QML divider 接 `setBottomTabsHostHeight`/现有缩放状态，或在明确迁移后删除旧 state；不能两套配置长期漂移。 |
| P1 | 校验问题定位链未闭环 | QML 已有 `revealSyntaxIssue`，但源数据为空，也没有选择后同步 timeline/preview 的策略。 | 诊断 model 完成后，定位应切换正确难度、选中范围、保留 caret，并对过期 revision 丢弃。 |

### E. 预览、音频与导出联动

| 优先级 | 缺口 | 证据与影响 | 初步补法 |
| --- | --- | --- | --- |
| P0 | v2 预览传输条不支持片头负时间域 | `PreviewTransport.qml` 固定 `from: 0`，且 `formatTime()` 不处理负值；而 export 片头预览需要 `[-introDuration, 0)`。结果是 v2 无法正确 scrub/显示片头。 | 在 QML preview model 暴露 `previewLowerBoundSeconds` 与正确的负时间格式化；所有 seek 经现有 controller，验证片头→0→谱面过渡。 |
| P0 | Muri 预览模式被压缩成 bool | `QmlPreviewModel::muriMode` 只有开/关；分支 Todo 明确要求对齐 Native / EraseByArea / MaimuriDxStyle 三态。 | 暴露 enum 与 labels，替换工具栏布尔切换；确保 renderer、Muri 分析和导出共享同一模式语义。 |
| P1 | 预览传输能力低于 v1 | 没有精细 scrub、负时域、完整快捷键/速度调整与 export-page 专用约束；播放/seek 应以音频 worker completion 为准。 | 先复用 `QuickShellController` 现有属性/命令，补模型字段；对 seek、暂停、设备热插拔、导出预览切换做状态机回归。 |
| P1 | 全屏预览不是 `dev` 的系统级全屏路径 | v2 `MainSplitView.qml` 只在主窗口内显示 `fullscreenPreview` 覆盖层；没有接 `MainWindow`/controller 的系统全屏状态、快捷键和媒体宿主联动。 | 以对齐 `dev` 的系统级全屏为默认方案：经 controller 调用现有全屏编排，并验证进入/退出、焦点、PV/BG、窗口恢复及 export 页禁用。若产品决定保留覆盖层，必须单独记录为批准的不对齐项和明确验收边界。 |
| P1 | 音频设置和预览/渲染设置入口不可用 | 工具栏两颗图标 disabled，只能绕到 Widgets Preferences。 | 短期接既有对话框，长期把设置拆为 QML 表单 + 窄设置 API；实时预览、项目持久化、导出 snapshot 的写入点必须分清。 |
| P1 | v2 导出缺片头音选择与独立音量 | `QmlExportSession`/`ExportVideoPage.qml` 未暴露 intro sound filename、`introSoundVolume`；该字段需经过单个/批量 snapshot、runtime audition 和 FFmpeg 音轨。分支 Todo 已列为最高优先级。 | 在 session 加属性/选择器，复用 `VideoExportPreferences` 与 snapshot 字段；补单个、批量、离页再进和导出预览的 round-trip 测试。 |
| P1 | 导出联动需按完整矩阵回归 | v2 虽有 `startQmlExportAudition` 和 worker 启动，但每个新设置都同时影响 live audition、`VideoExportTask`、snapshot、worker 与 batch。 | 建立设置矩阵，至少覆盖分辨率、range、clock count、皮肤/判定线、HUD、片头/片头音、size preset、不同难度 badge。 |

### F. 复用旧页面但尚未原生化的功能

这些项目当前并非“不可用”：v2 通过 `QmlEditorPageHost` 或对话框调用旧实现。它们应在 P2 按域拆 API 后迁移，避免把整个 `MainWindow` 再宿主进 v2。

- Latency Settings：目前局部 `WindowContainer` 宿主 `LatencyDetectionPage`；迁移时必须保留“主 transport audition”、BPM/offset/clock_count 和离页 SFX 隔离契约。
- Preferences、音视频工具、规范化、Net 批量下载/上传、封面导出、ZIP：可调用 Widgets 对话框/slot，缺少原生 QML 页面和一致的 QAction 可用状态。
- Export：v2 已经是纯 QML 设置页，不应回退为宿主 `VideoExportDialog`；后续只补任务 API 和字段 parity。

## 共同基线之后必须同步的 `dev` 修复

在补 v2 功能前，应先 merge/rebase `dev` 的 29 个独占提交并解决冲突。以下按受影响功能归类；文档、版本号和纯测试提交可随代码一起带入。

| 范围 | `dev` 提交 | 对 v2 的意义 |
| --- | --- | --- |
| 窗口/编辑器 | `9b557032`, `29ee561b`, `88d4a92b`, `ca1ebe59`, `809acf52` | 模态对话框置顶、查找浮层 inset、macOS 拖选/自动滚动、触控板编辑按 caret 而非 playhead。 |
| Timeline | `dbda9bf1` | QSG overlay cadence 限速与播放跟随平滑；v2 直接使用同一 `TimelineQuickItem`，必须带入对应 spec。 |
| 预览/媒体 | `554a69c8`, `ac21d628`, `9b607f43`, `f47ffbe5`, `57c01ecc`, `4503e6d6`, `53f02e46`, `61c0ec9f` | 烟花视觉、播放时钟/PV catch-up、音频预加载、片头启用时媒体 seek、首帧诊断、stale EndOfMedia 恢复、warm-up 确认、slide note-guide。 |
| 音频与导出 | `0a9c0e2d`, `df54576a`, `d510b37e`, `e4323c9c`, `fd8adb6f` | 项目 mixer 持久化、自定义片头字体、预卷 PV/暂停字形、touch-hold riser、批量页 badge 预览同步。 |
| 构建/测试/运维 | `1200470c`, `118ace78`, `8c34c6d2` 及相关 docs/release 提交 | 保持 debug flag、Release spec 构建与当前测试预期一致。 |

## 初步补完路线

### 阶段 0：先对齐共同后端（P0）

1. 将 `feature/qml-ui` 合并/变基到当前 `dev`，优先处理 `main.cpp`、`MainWindow`、export、timeline、preview runtime 的冲突。
2. Release 构建后先跑现有 editor/timeline/preview/export specs；手动验证启动、打开谱面、播放、PV 到尾、设备切换一次。
3. 不在本阶段重写 QML 页面，只保证 v2 吃到共同后端的修复且没有重新引入旧渲染后端。

### 阶段 1：建立单一 QML 文档与状态边界（P0）

1. 将 `QmlDocumentModel` 的 friend 直写收敛成按影响面划分的 `MainWindow` 公开 API；一次编辑返回 revision 与可呈现状态。
2. 先补正文、`&first`、extra timing fields、完整源码替换、难度/设计者修改五类事务，并为每类规定 dirty/autosave、quick timeline、slow parse、预览、validation/Muri、export 刷新的责任。
3. 将 QML `syntaxIssues` 改为真实 revision-aware model；补“编辑→诊断→点击定位”的自动测试和手工用例。
4. 同时接 root-window、关闭与 ChartDrop，保证不可见 Widgets backend 不再拥有孤立状态。

### 阶段 2：使编辑器和 timeline 可用于日常制谱（P0/P1）

1. 先补 timeline 控件：缩放、亮度、follow-code、validation/Muri tab、内容缩放与高度持久化。
2. 再补编辑器基本生产力：查找替换、当前行、IME/半角/覆写、括号与 Simai completion、书签。
3. 接 caret/selection→timeline→Ctrl 触控区创作链；每次编辑都验证快刷新和慢刷新不会倒灌过期结果。

### 阶段 3：预览和导出字段 parity（P0/P1）

1. 传输条接负时间、精细 seek、completion 状态与三态 Muri 模式。
2. 对齐系统级全屏预览；若明确保留窗口内覆盖层，记录它的产品边界，不能把它默认为与 `dev` 等价。
3. 补片头音选择/音量，严格覆盖 preview audition、单个/批量 `VideoExportSnapshot`、worker 反序列化和 FFmpeg 混音。
4. 为 export 建“设置→实时 audition→导出任务→worker task”矩阵测试；离开 export/latency 页必须拆除其临时 preview 状态。

### 阶段 4：逐页原生化与收尾（P2）

1. 按独立域迁移 latency、preferences、工具页；每个域先拆 C++ 任务 API，再换 QML 页面。
2. 统一菜单/工具栏命令可用状态，删除永远 disabled 的占位项。
3. v2 连续覆盖关键工作流后，才讨论是否退役 v1 宿主路径；当前 v1/QuickShell 必须保留为回退与对照实现。

## 验收清单（每阶段最少覆盖）

- [ ] 打开、编辑正文/metadata/完整源码、切难度、保存/另存、取消保存、关闭脏文档。
- [ ] 修改 `&first`、`&wholebpm`、`&whole_time_signature` 后，quick timeline、slow markers、暂停预览、validation/Muri 与导出 seed 一致。
- [ ] 时间轴缩放/亮度/follow-code、拖拽/滚轮/键盘、validation/Muri tab 与 QML 壳同时正常。
- [ ] 查找替换、补全、IME/半角、书签、caret 驱动触控板编辑均不劫持普通文本输入快捷键。
- [ ] 预览 play/pause/seek/rate、片头负时间、PV EndOfMedia、设备切换暂停与下一次 cold prepare。
- [ ] 单个/批量导出：不同难度、完整/截取 range、clock_count、皮肤/HUD、片头/片头音/音量、取消与失败提示。
- [ ] Windows/macOS 的标题栏、模态对话框、拖放建谱、紧凑布局，以及系统级全屏的进入/退出、焦点、媒体与窗口恢复。

## 关键入口索引

- v2 bootstrap/服务：`src/app/qml_ui/QmlUiBootstrap.*`、`QmlApplicationContext.*`、`QmlCommandService.*`、`QmlDocumentModel.*`、`QmlPreviewModel.*`。
- v2 编辑器/timeline/预览：`src/app/qml_ui/editor/`、`timeline/BottomPanel.qml`、`preview/PreviewTransport.qml`。
- v2 导出：`src/app/qml_ui/export/QmlExportSession.*`、`ExportVideoPage.qml`。
- 共享编排与契约：`src/app/mainwindow/sections/{document,timeline,validation,preview,export}/`、`src/app/quick_shell/QuickShellController.*`、`src/timeline/quick/`。
- 既有分支内 Todo：`docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md`（仅存在于 `feature/qml-ui`，应在同步时与本文合并/更新）。
