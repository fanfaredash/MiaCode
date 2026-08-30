# 导出页「区间导出无法正常播放」静态审查

> 所有者报告（2026-08-30）：导出页偶发无法播放，**在正上方难度选择处刷新难度后恢复正常**；
> 不好复现。所有者要求**先静态审查、暂不修复**。本文只给结论与证据。
>
> 基线：`dd85b9cc` + 本轮工作树。

## 结论

不是竞态"运气不好"，是**一个没有恢复路径的门**。导出页的播放就绪判定一旦失配，
**永远不会自己恢复**——只有重新安装试听场景（也就是刷新难度）才能重新对齐。
所以"偶发"的是**触发**，不是"有时能自愈"。

## 门在哪里

`MainWindow.PreviewPlaybackGlue.cpp:37-40`，`preparePreviewStartState()`：

```cpp
if (state_.latencySandboxAuditionActive_ || state_.exportPreviewAuditionActive_) {
    return state_.latestTimelinePreviewSnapshotReady_
        && state_.latestTimelinePreviewRevision_ == state_.timelineRevision_;
}
```

对照**同一函数**里普通难度那条分支（`:51-57`）：

```cpp
if (ready && revision == timelineRevision_) return true;
requestTimelineSlowRefresh();   // ← 失配就去重建，下一次 Play 就好了
return false;
```

**试听分支没有这一行。** 失配即 `return false`，没有任何东西会去重新生成快照或重新对齐 revision。
按下播放没有反应，且一直没有反应。

## 名词：什么是「试听分支」

**试听（audition）** 是这份代码里的既有说法：导出页与延迟检测页都需要让预览**播放一份
不是"正在被编辑的那个难度"的谱面**。它们没有另造一个播放器，而是把那份谱面**同步安装**
成常规预览的场景，然后用**同一套**走带（播放/暂停/定位）驱动它——
`installExportPreviewAuditionScene()` / `installSandboxScene()`，
对应两个标志 `exportPreviewAuditionActive_` / `latencySandboxAuditionActive_`。
导出页的试听还额外套上导出的 WYSIWYG 强制项，所以听到看到的就是成片的样子。

**分支** 就是字面意义上的分支：`preparePreviewStartState()`——**每一次播放都要过的那道门**
——开头的第一个 `if`。这道门有两条路：

| | 试听分支 | 常规难度分支 |
|---|---|---|
| 前提 | 场景由页面**同步装好**了 | 正文由用户在编辑 |
| 做的事 | 只回答"快照就绪吗" | 提交脏字段 → 查 `hasActiveDifficulty()` → 查快照 |
| 失配时 | **直接 `return false`** | `requestTimelineSlowRefresh()` 去重建 |

分支存在的理由是正当的：常规分支那些检查（字段脏就先提交、必须有活动难度、否则请求慢刷新）
问的全是"**正在被编辑的那个难度**"的事，而试听场景压根不是被编辑出来的，它是被页面装上去的。

## 代价：它没有恢复路径，而且是**两条**都没有

常规播放其实有**两层**兜底：

1. `preparePreviewStartState()` 内部的 `requestTimelineSlowRefresh()`——去重建快照。
2. 调用方 `startQtPreviewPlayback()`（`TimelinePlayback.cpp:482-488`）在被拒绝时记下
   `pendingPreviewPlaybackStart_`，等慢刷新完成后**自动补一次播放**
   （`PreviewTimelineFlow.cpp:1035-1042`）。

导出页**两条都没有**：

- 第 1 条被跳过了（试听分支直接 return）。
- 第 2 条被写死成假：`state_.pendingPreviewPlaybackStart_ = hasActiveDifficulty();`
  ——导出页上 `hasActiveDifficulty()` 为假，**所以连"待播放"都不会被记下**。
  就算记下了也没用：慢刷新本身在导出页上也不会运行。

所以按下播放没有反应之后，**没有任何机制会再试一次**。这就是"卡住之后只能刷新难度"的全部原因。

## 谁会让它失配

`latestTimelinePreviewRevision_` 只在 `installExportPreviewAuditionScene()`
（`ExportSnapshot.cpp:467-468`）被设成当时的 `timelineRevision_`。此后：

**`timelineRevision_` 全树只有两处自增**：

| 位置 | 说明 |
|---|---|
| `TimelineSection::scheduleTimelineRefresh()`（`PreviewTimelineFlow.cpp:706-711`） | 开头有 `if (!hasActiveDifficulty()) return;`。导出页 `activeDifficultyId_ == 0`（决策 D4），所以**这条在导出页上打不到**。 |
| `MainWindow::invalidateDocumentValidationRevision()`（`TimelineAnalysisFlow.cpp:48-52`） | **这条打得到。** |

`invalidateDocumentValidationRevision()` 的调用方：

- **`MainWindow::selectDocumentDifficulty()`**（`DocumentBridge.cpp:321-327`）——v2 侧**任何**难度选择都会走到，包括侧边栏、难度标签、以及导出页顶部那个难度选择器本身。
- `deleteDifficultyField()` 删除当前难度时（`DocumentUi.cpp:405`）。

而且 `selectDocumentDifficulty()` 做的**不止**是自增。它先调
`switchToDifficultyField()`，后者内部第 1209 行调用 `clearTimelineAndPreview()`，
那里（`DocumentUi.cpp:1371-1372`）把

```cpp
state_.latestTimelinePreviewRevision_ = 0;
state_.latestTimelinePreviewSnapshotReady_ = false;
```

**两个条件同时被破坏**：ready 被清掉、revision 被清零、`timelineRevision_` 再 +1。
而 `exportPreviewAuditionActive_` 全程保持 true——`clearTimelineAndPreview()` 不认识试听场景，
它清的是"普通难度"那套状态。

## 为什么"刷新难度就好了"

因为刷新难度会重新跑 `installExportPreviewAuditionScene()`，那里重新写
`latestTimelinePreviewRevision_ = timelineRevision_` 并把 ready 置回 true。
**修好它的不是"刷新"这个动作，是重新安装场景这个副作用。**

## 为什么"偶发"

取决于两条互不相关的路径谁在后面：

- 安装场景 → 之后再发生一次难度选择 ⇒ **卡住**（进入导出页后又碰了难度）。
- 难度选择 → 之后才安装场景 ⇒ 正常（进入导出页时顺带带上了难度切换）。

所有者的复现路径里"顶部难度选择"既能**造成**失配也能**修复**失配，取决于导出页是否随之重装场景，
这正是它看起来随机的原因。

## 追问：导出页的 `activeDifficultyId_ == 0` 本身是不是问题？

（所有者 2026-08-30 追问："导出页的难度应该继承进入时的难度"。）

### 难度**确实**继承了——只是继承到了另一个所有者

`performSwitchToExportField()`（`DocumentUi.cpp:964-1033`）的顺序是：

```cpp
const int previousActiveDifficultyId = state_.activeDifficultyId_;  // 归零之前先取
...
state_.activeDifficultyId_ = 0;
...
ui_.qmlExportSession_->enter(previousActiveDifficultyId);
```

`enter()` 把它交给 `resolveDefaultDifficultyId()`，回退链是：
进入时的难度 → 上次在导出页选过的 → 工程的 last-opened → 文档第一个难度。

所以树上有**两个**"当前难度"：

| | 含义 | 导出页上的值 |
|---|---|---|
| `MainWindow::activeDifficultyId_` | **哪个难度的正文字段正在被编辑** | 0（导出页不是难度编辑器） |
| `QmlExportSession::selectedDifficultyId_` | **这个页面是关于哪个难度的** | 进入时的难度（D4） |

归零是 `716c36d4` 引入的，和 元数据页 / 欢迎页 同一套做法：导出页是 `editorStack_` 里的一页，
把 `activeDifficultyId_` 置 0 就一次性关掉整条难度编辑链（校验装饰、字段脏、底栏页签、
时间轴刷新）。**意图是对的**：那时候确实没有任何难度字段在被编辑。

### 它和本 bug 的联系：是**成因**，不是巧合

归零带来三个连锁后果，本 bug 正是第三个：

1. `hasActiveDifficulty()` 为假 ⇒ `scheduleTimelineRefresh()` 开头就 return。
   **导出页上，常规的重建路径是死的。**
2. 正因为 (1)，`preparePreviewStartState()` 才需要一条**试听专用分支**——而它恰恰是那条
   没有恢复路径的分支。它**不能**调 `requestTimelineSlowRefresh()`，因为那条同样被
   `hasActiveDifficulty()` 挡住。
3. 但试听场景**仍然借用**普通难度那组就绪字段
   （`latestTimelinePreviewSnapshotReady_` / `latestTimelinePreviewRevision_` / `timelineRevision_`）。
   这组字段的含义是"**正在被编辑的那个难度**的预览快照"——而导出页上那个难度是**不存在的**。
   于是任何代表某个难度去动这组字段的代码（`selectDocumentDifficulty` →
   `clearTimelineAndPreview` + `++timelineRevision_`）都会直接伸进导出页试听的状态里，
   而没有任何所有者会察觉。

**一句话**：导出页在 `MainWindow` 眼里没有难度，它的播放就绪却存放在"有难度时才有意义"的字段里。

### 那"让 `activeDifficultyId_` 继承进入时的难度"能修好吗

能修好 (1) 和 (2)，但会**打开 73 个门**。`hasActiveDifficulty()` 全树 73 个使用点，
抽样看压倒性地是"**有没有一份正文可读/可编辑**"——扩展的 `editor/*` API、`editorText()`、
`selectionOffsets()`、字段脏、校验装饰。导出页并没有显示正文编辑器，让这些全部变成真，
是用一个更大的问题换掉一个小的。

真正的结论是：**`hasActiveDifficulty()` 把两个问题合并成了一个**——

- 「有没有难度**被选中**」（导出页：有，就是进入时那个）
- 「有没有难度的正文**正在被编辑**」（导出页：没有）

导出页是把这个合并暴露出来的那个用例。所以下面的方向 3 才是根因修法，
而它的第一步不是"改 `activeDifficultyId_`"，是**把这两个问题拆开**。

## 修复方向（方向 3 已于 2026-08-30 执行，见文末）

三个层次，从窄到宽：

1. **让试听分支也有恢复路径**。最小改动，但它要恢复的不是"慢刷新"——试听场景的真相在
   `installExportPreviewAuditionScene()`，所以恢复动作应当是**重装当前导出难度的试听场景**，
   而不是 `requestTimelineSlowRefresh()`（那条依赖 `hasActiveDifficulty()`，在导出页上是假）。
2. **让 `clearTimelineAndPreview()` 认识试听场景**：`exportPreviewAuditionActive_` 为真时不清
   ready/revision，或清完后立即重装。这修的是"破坏"这一半。
3. **把「有难度被选中」和「有难度正文在被编辑」拆成两个问题**，让试听场景不再共用
   普通难度的就绪字段。给试听自己的就绪标识，`clearTimelineAndPreview()` 就打不到它，
   第 2 项也不必存在；`preparePreviewStartState()` 的试听分支也就能有自己的恢复动作
   （重装场景），不必去借那条被 `hasActiveDifficulty()` 挡住的慢刷新。

**建议 3**（其余两项是对症）。但它动的是播放就绪的所有权，属于结构性改动，
应当与文档机制那批一起排期，而不是塞进导出区间这一轮。

## 顺带记录：不是本条成因，但相邻

`preparePreviewStartState()` 的试听分支同时跳过了 `currentFieldDirty_` /
`applyCurrentFieldToDocument()` 那一步（注释说明是有意的）。v2 下 `currentFieldDirty_`
恒为 false（`applyCommittedQmlDocument` 无条件写 false），所以这条跳过在 v2 上没有影响。

---

## 已执行：方向 3（2026-08-30）

**做了什么**：试听场景不再借用被编辑难度的就绪字段，改为持有自己的身份，并获得一条恢复路径。

- 新状态 `auditionSceneReady_` + 两个由**安装方**提供的回调：
  `auditionSceneStillCurrent_`（这套场景还对得上它的来源吗）与 `auditionSceneReinstall_`（重建）。
  只有安装方知道这两个答案，所以由它给。
- `MainWindow::setAuditionSceneReady()` / `clearAuditionSceneReady()` / `ensureAuditionSceneReady()`。
- `preparePreviewStartState()` 的试听分支改为 `return owner_.ensureAuditionSceneReady();`，
  **不再读** `latestTimelinePreviewSnapshotReady_` / `timelineRevision_`。
- 导出侧的 `stillCurrent` 比较的是**安装时那份正文文本**——这比原先借来的 revision 计数器
  更贴近真相：那个计数器回答的是"正在被编辑的难度"的问题，而这一页没有那个难度。
- 延迟检测页的测试谱面是当场合成的、不会在场景底下改变，所以它的 `stillCurrent` 为空
  （视为始终当前），重建只作安全网。
- 两个拆除路径都调 `clearAuditionSceneReady()`。

**为什么这样就修好了**：`clearTimelineAndPreview()` 与 `invalidateDocumentValidationRevision()`
动的仍然是那对旧字段——但播放已经不看它们了，所以够不到导出页。
而万一还有没找到的破坏路径，`ensureAuditionSceneReady()` 会重建一次而不是永远拒绝：
这一页原本**两层兜底都没有**（慢刷新被 `hasActiveDifficulty()` 挡住，
`pendingPreviewPlaybackStart_` 被写成同一个假值），现在它有自己的一层。

**没有做的**：没有去改 `activeDifficultyId_` 的值，也没有去拆 `hasActiveDifficulty()` 的
73 个使用点。那个谓词的含义仍然是"有没有正文在被编辑"，导出页的答案仍然是"没有"——
这是对的。被拆开的是**播放就绪的所有权**，那才是真正被混用的东西。

**漂移守卫**：`preview_transport_push_spec` 新增五条断言，包括
「试听分支不得再出现 `latestTimelinePreviewSnapshotReady_` / `timelineRevision_`」。

**仍需原生桌面验收**：进入导出页 → 切难度 → 播放；以及此前会卡住的路径
（进入导出页后再在顶部切难度，然后播放）。
