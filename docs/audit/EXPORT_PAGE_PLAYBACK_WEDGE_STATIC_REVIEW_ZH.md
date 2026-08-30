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

## 修复方向（尚未执行，供决策）

三个层次，从窄到宽：

1. **让试听分支也有恢复路径**。最小改动，但它要恢复的不是"慢刷新"——试听场景的真相在
   `installExportPreviewAuditionScene()`，所以恢复动作应当是**重装当前导出难度的试听场景**，
   而不是 `requestTimelineSlowRefresh()`（那条依赖 `hasActiveDifficulty()`，在导出页上是假）。
2. **让 `clearTimelineAndPreview()` 认识试听场景**：`exportPreviewAuditionActive_` 为真时不清
   ready/revision，或清完后立即重装。这修的是"破坏"这一半。
3. **让试听场景不共用普通难度的就绪字段**。根因是导出页试听借用了
   `latestTimelinePreview*` 这组为"当前难度"设计的状态，而导出页根本没有当前难度
   （`activeDifficultyId_ == 0`）。给试听自己的就绪标识，`clearTimelineAndPreview()`
   就打不到它，第 2 项也不必存在。

**建议 3**（其余两项是对症）。但它动的是播放就绪的所有权，属于结构性改动，
应当与文档机制那批一起排期，而不是塞进导出区间这一轮。

## 顺带记录：不是本条成因，但相邻

`preparePreviewStartState()` 的试听分支同时跳过了 `currentFieldDirty_` /
`applyCurrentFieldToDocument()` 那一步（注释说明是有意的）。v2 下 `currentFieldDirty_`
恒为 false（`applyCommittedQmlDocument` 无条件写 false），所以这条跳过在 v2 上没有影响。
