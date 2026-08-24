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

1. 彻底移除 `Qt6::Widgets` 依赖，只保留 Qt Quick 相关。
2. 按构造消除双所有者与轮询造成的竞态，而不是继续打补丁。
3. 复用已经与 widgets 无关的渲染与领域代码，不重写它们。

## 3. 已确认的决定

| 决定 | 选择 | 理由 |
|---|---|---|
| 文档/解析/校验/Muri 的所有者 | 新建非 widget 的 `ChartWorkspace` 核心；`MainWindow` 最终整体删除 | 单一所有者是消除竞态的前提；保留 MainWindow 会把 176 方法的形状继承下来 |
| timeline/预览的复用边界 | 只复用渲染与数据层；`QuickShellController` 随 v1 退役 | 它 1336 行里有 29 处 `surfaceHost_` 分支（v2 下是死代码）和 33 处轮询，属于 v1 外壳胶水而非预览逻辑 |
| v1 独占功能的存活范围 | 只保留 Net 批量、封面导出、ZIP 打包 | 见第 7 节的代价说明 |
| 迁移路径 | 缠杀式：新核心与 MainWindow 共存，按域搬迁 | 43,460 行无法一次性重写；平行新目标会翻倍构建/测试面并有长期分叉风险 |

## 4. 目标分层

```
QML UI (MiaCode.UI)            纯 Qt Quick，零 C++ UI 类型
        ↓  窄 QObject 门面
v2 应用层  src/app/v2/
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
- **做什么**：视频导出（已有 `QmlExportSession`）、封面导出、ZIP 打包、Net 批量上传/下载，统一为作业模型（提交 → 进度 → 结果）。
- **依赖**：`tools/video_export`、`tools/cover_export`、`tools/zip_export`、`tools/net`。这三项存活功能的引擎层需要先去 widgets 化：`zip_export` 已干净（0/2 文件耦合），`net` 一半耦合（4/8），`cover_export` 大部分是 Widgets 对话框（9/13）。

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
| 1 | 建立 `ChartWorkspace` 与 `AnalysisService`，文档域搬迁 | 隐藏 `PlainCodeEditor` 删除；文档不再有第二个所有者 |
| 2 | 建立 `PreviewSession` 与 `TimelineSession`，`QuickShellController` 退役 | 轮询定时器消失；`surfaceHost` 分支归零 |
| 3 | `ExportService`：视频沿用，封面/ZIP/Net 重建为 QML 并去除其引擎层的 widgets 耦合 | Widgets 对话框归零 |
| 4 | 删除 `MainWindow`，从链接中移除 `Qt6::Widgets` | 目标达成 |

## 9. 测试策略

- 应用层全部是无 widget 的 `QObject`，直接进 spec，不需要 GUI。
- QML 侧沿用已建成的 `MiaCode.UI` spec 导入镜像 + `Qt6::Test`，驱动**真实组件**与**真实事件**，不用源码字符串扫描代替。
- 每个域搬迁**之前**先补该域的契约回归（单一所有者、单次通知、revision 门控），确认红态后再搬。
- 保留现有 17 项定向 spec 作为回归基线。

## 10. 本次范围裁剪的代价

被删除的功能及其后果，明确记录以免日后误以为是回归：

| 删除项 | 后果 |
|---|---|
| 偏好设置对话框 | 编辑器字体/行距/输入策略、预览音频与渲染设置、快捷键自定义**失去 UI**，只能手改 `shortcuts.json` 与 portable state |
| 延迟检测页 | 功能消失；同时移除 v2 中唯一嵌 `WindowContainer` 宿主 v1 widget 的混合渲染树 |
| 媒体处理 / 音轨工具 | 前置静音、PV 黑场、背景视频压缩、采样率转换、从音轨读标题/艺术家等消失 |

以上均为用户明确授权（"哪怕功能会缺失也要做"），后续再补。

## 11. 非目标

- 不重构 `TimelineQuickItem` / `PreviewRuntime` 内部（4,000+ 行 QSG 与时序，风险最高且与本次目标无关）。
- 不改变渲染架构决策：进程内 QSG 仍是唯一渲染路径。
- 不做与本目标无关的重构。
