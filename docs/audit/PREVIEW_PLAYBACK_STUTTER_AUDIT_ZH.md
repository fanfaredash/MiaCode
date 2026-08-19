# 预览播放卡顿审计（PV 中段起播 / 启动后首次播放）

- 审计日期：2026-08-16（Asia/Shanghai），2026-08-16 第二轮复核后修订
- 审计对象：`d510b37e`（dev，1.1.0-beta.10 线）
- 对照基线：`v1.0.0` = `e9aa9484`（2026-06-25）、`0.5.1-beta` = `64f0aa84`（2026-06-11）
- 日志来源：`/Users/caoyusen/Desktop/Files/`（`logs` … `logs 19`，2026-08-04 … 2026-08-12）
- 性质：代码 + 提交历史 + 既有日志的静态审计，外加一次本地 spec 实跑；**未**为本报告新增运行时采样

> **第二轮复核修订了第一版的两处结论。** 见 §0.1。

---

## 0. 结论速览

| # | 现象 | 根因 | 责任提交 | 证据 |
|---|---|---|---|---|
| 1 | 带 PV 的谱从中间起播，进度条卡一下 | **分支顺序错误**：`currentPreviewAuthoritativeAudioClockSecond()` 把「等待 PV 晚启动」的冻结分支放在了「正在播放」分支**之上**，commit 之后权威时钟仍返回冻结值，最长 800 ms 后跳变 | 潜伏缺陷 `d65de51e`（2026-04-21）；`0d013404`（2026-08-09）让它进入常用路径 | 高 |
| 1b | 同上，叠加 | `commitPreparedPlaybackStart` 对 PV 做了一次**冗余重复 seek**（prepare 刚 seek 过同一位置） | 结构性，`3bf5b886` 起即有 | 高 |
| 2 | 启动后首次播放卡顿 | 烟花 PSO 预热完成判据改为「确认绘制」后，预热在空闲期无法完成、一路存活到首次播放；存活期间**每帧**触发 O(markers) 增删 + `sceneContentRevision++` → 整帧重建 prepared-scene 缓存（含 20 次 `stable_sort`） | `688e97b1`（2026-06-19） | 高 |

### 0.1 对第一版的修订

1. **`0d013404` 不该被回退。** 它是「把所有实时预览后端调用移出 GUI 线程」这条正确工作的一部分
   （见 §1.2）。真正的缺陷是它**暴露**出来的一个 2026-04-21 就埋下的分支顺序错误。
   修法是修那个错误，不是恢复同步快路径。
2. **原 §2.3「首次播放才装载 BASS 素材与 BGM」在当前 HEAD 上已经不成立。**
   `d0c90b32`（2026-08-09）已经把 `reloadAssetsForChart` 挪到**开谱时**异步下发。
   第一版引用的 288 ms 实测来自 2026-08-07 的构建，那条路径已经改掉了。
   残留的只是一个竞态（§2.4），不是原来的稳定 300 ms。

### 0.2 本轮实跑核实

`preview_firework_lifecycle_spec` **当前通过**：

```
$ cmake --build build-macos-spec --target preview_firework_lifecycle_spec -j4
$ ./build-macos-spec/preview_firework_lifecycle_spec
preview_firework_lifecycle_spec ok      (exit=0)
```

三份 8 月审计（`BRANCH_AUDIT_WINDOWS_IDLE_FREEZE_DIAGNOSTICS_ZH.md`、
`PREVIEW_FPS_REGRESSION_AUDIT_BETA9_TO_CURRENT_ZH.md`、audio-worker 计划文档）都把它列为
「分支既有失败」，那是 8/04–8/09 的状态；`c68baa34`（8/13）与 `554a69c8`（8/15）随烟花曲线调整
一并更新了该 spec 的期望值。**这三份文档的相关表述现已过期。**
该 spec 只覆盖烟花视觉曲线，不覆盖预热机制，因此它既不阻挡也不验证 §2 的修复。

---

## 1. 现象 1 深度解析

### 1.1 先说结论：这不是「修复引入的回退」，是「修复暴露的旧洞」

四个提交按时间串起来才是完整因果链：

```
2026-04-15  3bf5b886  引入 strong-group 握手 + weak video 晚启动
                      ↓ 时钟函数分支顺序：BGM → 正在播放 → 启动冻结 → 暂停
                      ↓ 冻结分支在「正在播放」之下 ⇒ commit 后不会冻结  ✅ 正确

2026-04-21  d65de51e  把「启动冻结」分支上移到函数最顶端           ⚠️ 埋雷
                      （当时下面一条是无条件的 BASS 游标分支，
                        冻结分支留在底部会永远取不到 ⇒ 只能上移）

2026-05-19  90ec7c48  G1 Commit 4 删掉 BASS 游标分支
                      （上移的理由消失了，但上移没有回退）      ⚠️ 雷还在

2026-08-09  0d013404  retained 续播并入异步 strong-group        💥 引爆
                      （中段起播从此会设置 previewLateVideoStartPending_）
```

`d65de51e` 的差分（`MainWindow.TimelinePlayback.cpp`）：

```diff
 double MainWindow::currentPreviewAuthoritativeAudioClockSecond() const
 {
-    if (previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
-        return previewSfxRuntime_->backgroundPlaybackSecond();
+    if (previewStartupSyncPending_ || previewLateVideoStartPending_) {
+        return previewStartupPreparedSecond_;
+    }
+    if (previewSfxRuntime_ != nullptr) {
+        return previewSfxRuntime_->authoritativePlaybackSecond();
     }
     if (qtPreviewPlaying_) { ... 墙钟 ... }
-    if (previewStartupSyncPending_ || previewLateVideoStartPending_) {
-        return previewStartupPreparedSecond_;
-    }
     return qtPreviewPauseSecond_;
 }
```

上移本身在当时是必要的（下面那条 `previewSfxRuntime_ != nullptr` 是无条件真，冻结分支留在底部
就成了死代码）。**但它把两个语义完全不同的标志一起上移了**：

- `previewStartupSyncPending_` 是 **commit 之前**的状态 —— 音频还没起来，UI 不该跑在前面，冻结正确。
- `previewLateVideoStartPending_` 是 **commit 之后**的状态 —— 音频已在播、`qtPreviewPlaying_` 已为
  `true`、`qtPreviewElapsed_` 已 restart，墙钟本就是权威。这里冻结**没有任何理由**。

### 1.2 `0d013404` 到底在修什么（为什么不能回退它）

计划文档 `docs/superpowers/plans/2026-08-08-preview-audio-worker-autopause-and-log-pruning.md`
开宗明义：

> **Goal:** Move every realtime preview backend call off the GUI thread, pause visibly and
> permanently on a real output-device change…

背景是 Windows 空闲卡死 / 设备切换时 GUI 卡住那一系列问题（`WINDOWS_IDLE_FREEZE_*` 审计）。
BASS 的 `BASS_Init`、素材装载、seek、暂停在设备切换时会阻塞几百毫秒到数秒，全部跑在 GUI 线程上。

Task 7 明确写着：

> `startQtPreviewPlayback()` submits audio work tagged with the newly allocated playback
> transaction and **returns after optimistic visual setup**. …
> **Keep stage-media startup callbacks on the GUI thread and retain the existing strong-group handshake.**

也就是说：`seekRetainedPreviewPlaybackTransaction()` 从「返回 `double`」变成「返回
`PlaybackSubmission`」是**架构必然** —— 调用已经在 worker 线程上执行，GUI 侧拿不到同步结果了。
1.0 那条 retained 快路径依赖的正是那个同步返回值，它**必须**消失。作者选择复用既有 strong-group
而不是另造一套提交路径，这个选择本身是合理的。

**所以正确的修法是：保留异步化，修掉被它照出来的分支顺序错误。**

### 1.3 用户看到的到底是什么

中段暂停 → 按播放，当前 HEAD 的时间线：

```
t0  点击。qtPreviewPlaying_=false，滑块/时间轴/画面全部停在 S。
    ├─ preparePlaybackStart(S)  →  player_->pause() + player_->seek(S)   ← 真实解码往返
    └─ seekRetained…            →  投递到 audio worker
t1  音频完成回调到达 → previewStartupAudioPrepared_=true
t2  画布 present → commit → qtPreviewPlaying_=true，qtPreviewElapsed_.restart()
    画面和 transport 滑块从 S 开始走
    ├─ PV 若尚未 ready ⇒ previewLateVideoStartPending_=true
    └─ 时间轴播放头读权威时钟 ⇒ 仍然钉在 S           ← 冻结
t3  PV prepared 回调 → commitPreparedPlaybackStart(冻结的S) → 解冻
    时间轴播放头一次性跳到当前秒                      ← 「卡一下」
```

- `t0 → t2`：全部静止。这段在 1.0 的 retained 路径上是**零**（同步完成）。
- `t2 → t3`：时间轴冻结后跳变。上界 800 ms（`kMediaSeekPrepareTimeoutMs`）。
- 读时钟的是 `flushQtPreviewTimelinePosition()`（`MainWindow.PreviewTick.cpp:150`）。
  `onQtPreviewTick` 走的是墙钟 `fallbackSecond`，所以画面不冻 —— **画面在走、时间轴不走**，
  这个不一致本身就很扎眼。

**为什么偏偏「从中间」**：`preparePlaybackStart` 有 40 ms 短路
（`PreviewStageMediaHost_Playback.cpp:93`）——目标与 `lastSeekMs_` 相差 < 40 ms 时只排一个
下一轮事件循环的 ack。从头播、或 PV 恰好停在该位置时命中短路，几乎零延迟；
中段暂停后续播时 `lastSeekMs_` 还停在**上一次起播的目标**，与新目标相差甚远 ⇒ 真实
`player_->seek()` ⇒ 完整解码往返。

### 1.4 同一处的两个附带缺陷

**(a) PV 被 commit 在冻结秒上。**
`MainWindow.PreviewPlaybackState.cpp:165-166`：

```cpp
const double currentSecond = owner_.currentPreviewAuthoritativeAudioClockSecond();
state_.previewStageMediaHost_->commitPreparedPlaybackStart(currentSecond);
state_.previewStartupVideoStarted_ = true;
state_.previewLateVideoStartPending_ = false;   // ← 清零发生在读取之后
```

读的时候标志还没清，拿到的是冻结值。于是 PV 从旧秒开始播，**天然落后音频一个 prepare 时长**。

**(b) 冻结期间暂停/停止会倒退。**
`pauseQtPreviewPlaybackExact`（`:670`）与 `stopQtPreviewPlayback`（`:715`）都用这个时钟取暂停秒。
在冻结窗口内暂停，记录的是 S 而不是用户看到的位置 —— 播放头往回跳。
`applyPreviewPlaybackRate`（`:332`）取 `chartNow` 同理，冻结期改倍速会把时钟锚回 S。

**这三处在修好分支顺序后自动全部正确**，无需单独处理。

### 1.5 修复方案

#### F1（主修）：恢复分支顺序

`src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp:436`

```cpp
double MainWindow::currentPreviewAuthoritativeAudioClockSecond() const
{
    // commit 之后墙钟即权威：finalizeQtPreviewPlaybackStart 已经 restart 过
    // qtPreviewElapsed_。previewLateVideoStartPending_ 是 commit 之后的状态，
    // 不能参与冻结（d65de51e 的上移把它和 previewStartupSyncPending_ 混为一谈，
    // 上移的原因——无条件的 BASS 游标分支——已在 90ec7c48 移除）。
    if (qtPreviewPlaying_) {
        const double elapsedSeconds =
            static_cast<double>(qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
        return qtPreviewStartSecond_ + (elapsedSeconds * previewPlaybackRate_);
    }
    if (previewStartupSyncPending_ || previewLateVideoStartPending_) {
        return previewStartupPreparedSecond_;
    }
    return qtPreviewPauseSecond_;
}
```

**为什么安全**：

- commit 之前 `qtPreviewPlaying_ == false`，落到冻结分支 ⇒ **旧行为逐字节保留**。
- `previewLateVideoStartPending_` 只在 `tryCommitPreviewStartupSync()` 里被置真，而它紧接着就
  调用 `finalizeQtPreviewPlaybackStart()` 把 `qtPreviewPlaying_` 置真 ⇒
  「lateVideoPending 且未播放」这个组合**不存在**，第二个分支不会因此丢失覆盖。
- 已逐一核对全部 27 个调用点：语义性调用点（时间轴播放头、暂停/停止取秒、倍速锚点、
  PV 晚 commit、编辑器跟随、导出页当前秒）在改动后**全部从错误变正确**；其余为日志格式化。

#### F2（配套）：去掉 commit 时的冗余 PV seek

`src/preview/runtime/PreviewStageMediaHost_Playback.cpp:229`

```cpp
const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
lastSeekMs_ = targetMs;
player_->seek(targetMs);     // ← prepare 刚 seek 过同一位置
player_->play();
```

`preparePlaybackStart` 与 `submitPausedSeek` 都有 `qAbs(targetMs - lastSeekMs_) < 40` 短路，
**唯独 commit 没有**。于是每次起播 PV 都吃两次 seek（各自 flush 解码器、从最近关键帧重解）。
按同样的 40 ms 阈值短路即可；晚启动那条路径 `currentSecond` 确实已经走远，会正常落到真 seek。

#### F3（可选，观测后再定）：缩短 `t0 → t2`

F1 只消除「冻结后跳变」，`t0 → t2` 的启动延迟仍在（音频 worker 往返 + 一次 present）。
若实测这段仍然明显，可以让 retained 续播乐观提交：用 `audioSubmission.fallbackSecond`
先起时钟，完成回调到达后再按差值修正。

**风险**：worker 实际落点 S′ 与请求秒 S 若不一致，会引入固定 A/V 偏移；
`0d013404` 正是为了避免这个才要求「effective second 只在完成回调后才权威」。
**先量再改** —— 用 `preview/playback action=commit` 与 `action=start_request` 的时间差判断
这段到底有多长，以及 `audio_startup_completion_accepted` 里 `requested` 与 `effective` 差多少。

---

## 2. 现象 2 深度解析与副作用评估

### 2.1 根因（复核后不变）

`0.5.1`（`64f0aa84:src/preview/runtime/PreviewRuntime.cpp`）：

```cpp
void PreviewRuntime::setPlayheadSeconds(double seconds, bool requestUpdate)
{
    frameState_.playheadSeconds = seconds;      // 没有任何预热逻辑
    if (requestUpdate) update();
}
// handlePresentedFrame: 2 帧后无条件完成
```

`688e97b1`（2026-06-19）改成「确认绘制才完成」+ 每次 `setPlayheadSeconds` 重居中。
`refreshFireworkWarmupForPlayheadChange()`（`PreviewRuntime.cpp:2010`）在 armed 未完成期间**每帧**：

1. `removeFireworkWarmupMarkers()` —— `std::remove_if` 全量扫 `frameState_.noteMarkers`，
   每个 marker 做一次 `QLatin1String` 比较 + 两次 `qFuzzyCompare`
2. `appendFireworkWarmupMarker()` —— QVector 追加
3. `sceneContentRevision += 1` —— 命中 `PreviewPreparedSceneCache::sync()` 的缓存键
   （`PreviewPreparedSceneCache.cpp:126-144`）⇒ **整帧 `rebuild()`**：10 个 layer window
   全清重建 + 每层 2 次 `stable_sort`（`:165-168`）+ 游标 `resetForTime` 二分重建

而预热在空闲期**根本完不成**：日志里 arm 时 `present_count=0`，完成时才 9–36，中间隔了
3.6 s / 9 s / 21 s 墙钟时间 —— 暂停时预览几乎不 present。所以 armed 窗口一路活到首次播放。

日志相关性（6 份会话）：

| 会话 | 预热完成 vs 首次播放 | `play_request → play_complete` |
|---|---|---|
| logs 12 / 16 | **播放之后** | **324 ms / 348 ms** |
| logs 13/14/15/17/18 | 播放之前（用户先拖过） | 5 ms |

仓库自己踩到过一半 —— `PreviewPreparedSceneCache.cpp:180-186` 的注释：

> "…the firework PSO warm-up re-centers its synthetic marker on every playhead change while
> armed, so **for the first seconds of playback this ran per frame**…"

当时只给**诊断**加了 `runtimeDebugOutputEnabled()` 门禁，`rebuild()` 本身没动。

### 2.2 新发现：F11 / 窗口重绑定会让问题在会话中途复发

`PreviewQuickSceneRoot.cpp:738` 在窗口绑定时调用 `runtime_->setVisibleHostWindow(boundWindow_)`，
而 `PreviewRuntime::setVisibleHostWindow`（`:253-270`）在窗口**指针变化**时把
`fireworkWarmupArmed_/Done_` 全部清零并重新 arm。

1.1.0-beta.5 起「F11 进出全屏只切换可见性与绑定状态」—— 但内嵌预览与全屏预览是两个不同的
`QQuickWindow`，指针确实变了 ⇒ **播放中按 F11 会重新进入「每帧重建」状态**，直到下一次烟花绘制确认。
这条路径目前没人报，但机制与首次播放完全相同，同一个修复一并覆盖。

### 2.3 修复方案与**副作用评估**（用户要求的重点）

#### 方案 A（推荐）：把「每帧重居中」改成「按阈值重居中」

**做法**：记录上次重居中时的播放头 `lastWarmupCenterSecond_`，只在
`qAbs(playhead - lastWarmupCenterSecond_) > 0.5` 时才执行重居中三件套。

**为什么 0.5 s 是安全的**（这是关键论证，不是拍脑袋）：

- 合成 marker 放在 `playhead - kJudgeEffectFireworkTouchTriggerDelaySeconds(0.05) - 0.15`
  = `playhead - 0.20`（`PreviewRuntime.cpp:1974-1976`）
- 烟花生命周期 `kJudgeEffectFireworkDurationSeconds = 1.0`
  （`PreviewGameplayConfig.h:192`），有效区间是 `elapsed ∈ [0, 1.0]`
  （`PreviewJudgeFireworkLayerState.cpp:156`）
- 重居中瞬间 `elapsed = 0.15`，**播放头还能前进 0.85 s** 合成 marker 才失效

取 0.5 s 阈值留了 0.35 s 余量。1.0x 播放时约每 0.5 s 重居中一次（原来 60–180 Hz），
**降频 30–90 倍**；2.0x 时约每 0.25 s 一次，仍远低于帧率。

**副作用核查**：

| 风险 | 结论 |
|---|---|
| 违反 `cross-chain-linkage.md` 的「不许去掉 re-center」契约 | **不违反**。契约要求的是「seek / 负 pre-roll 不能把合成 marker 甩出生命周期窗口」。seek 是位移远大于 0.5 s 的跳变，一定越过阈值触发重居中。契约保住了 |
| 违反「不许去掉 notify 调用」契约 | **不涉及**。`notifyFireworkLayerProducedNode()` 完全不动 |
| 240 presents 兜底会不会更容易撞上 | 不会。合成 marker 始终在有效窗口内，第一帧就该绘制；兜底本来就只在烟花层被禁用时才生效 |
| `preview_firework_lifecycle_spec` | **不受影响**（已实跑通过）。该 spec 只测视觉曲线，不碰预热 |
| 播放中改倍速 / 负时间片头 | 都会造成 > 0.5 s 位移或落在阈值内的连续推进，两种情况都正确 |
| HUD 计数被合成 marker 污染 | **本来就没有**。HUD 走 `progressStatsCache`（`PreviewFrameState.h:297-305`），数据源是 `latestTimelineNoteMarkers_`，不是 `frameState_.noteMarkers` |
| 切谱资源释放 | 不涉及。`CHART_SWITCH_RESOURCE_RELEASE_AUDIT_ZH.md` §5 已确认该路径干净 |

**风险等级：低。** 改动集中在一个函数，行为退化方向是「重居中变少」，而重居中的唯一职责是
防止合成 marker 越窗 —— 越窗判据可以精确计算，不需要保守到每帧。

#### 方案 B（不推荐单独使用）：装载期强制跑完预热

**做法**：arm 之后主动驱动若干次 present 直到确认绘制。

**副作用 / 为什么不推荐**：

- **不可靠**。`armFireworkPsoWarmupIfReady()` 由 `assetsChanged` 触发，此时窗口可能还没
  exposed（日志里 `visible_host_skip … window_visible=0`），`update()` 不会真的 present。
  强制 present 需要等窗口可见，那就得再加一套「窗口可见后补跑」的状态机。
- **拖慢启动**。启动时序里 `preview_skin_async_dispatched` 已经占了 1293 ms
  （`logs 19` 的 `miacode_startup_timing.log`），再插入强制渲染会让首帧更晚。
- **和 F11 复发路径冲突**：窗口重绑定时同样要重跑，等于把这套逻辑再执行一遍。

**如果要做，也应该挂在「窗口首次 exposed」而不是「素材就绪」**，并且必须与方案 A 一起做 ——
方案 A 是兜底，方案 B 只是把 PSO 编译提前。**只做 B 不做 A，F11 复发路径依然存在。**

#### 关于「PSO 编译本身落在首帧」

即使 A+B 全做，`fireworkWarmupDone_` 之前的第一次真实烟花绘制仍会承担 PSO 编译 + 彩球贴图上传
（render thread stall）。这是 Qt RHI 惰性编译的固有成本，预热能做的只是把它挪到用户不敏感的
时刻。方案 A 保证的是**挪不动时也不会额外附加每帧重建**，这才是 324 ms 里可以稳定拿掉的部分。

### 2.4 关于原「2b」：已经修了，但残留一个竞态

`d0c90b32`（2026-08-09，`fix(preview): atomically apply chart path on asset reload`）在
`MainWindow.PreviewTimelineFlow.cpp:654-665` 加了**开谱即异步预载**：

```cpp
if (state_.previewSfxRuntime_ != nullptr && pathChanged) {
    const auto reload = state_.previewSfxRuntime_->reloadAssetsForChart(
        state_.currentFilePath_, state_.previewAudioSettings_);
    state_.previewSfxRuntimePrepared_ = false;
    state_.previewSfxRuntimePreparationAssetGeneration_ = reload.identity.assetGeneration;
    state_.previewSfxRuntimePreparationSequence_ = reload.identity.sequence;
}
```

完成回调在 `MainWindow.FrameBootstrap.cpp:1481-1491` 把 `previewSfxRuntimePrepared_` 置真。
之后首次播放的 `ensurePreviewSfxRuntimePrepared()` 直接短路返回。
**所以第一版建议的「预载 BGM」在 HEAD 上已经是既成事实，不需要再做。**
`logs 12` 那 288 ms 是 2026-08-07 构建的历史读数。

**残留缺陷**：`ensurePreviewSfxRuntimePrepared()`（`MainWindow.PreviewWarmupAndSettings.cpp:135-159`）
只检查 `previewSfxRuntimePrepared_`，**不检查是否已有 reload 在途**：

```cpp
if (state_.previewSfxRuntime_ == nullptr || state_.previewSfxRuntimePrepared_) {
    return;                       // ← 没有 previewSfxRuntimePreparationSequence_ != 0 的判断
}
```

开谱后 ~300 ms 内按播放/拖动 ⇒ 再下发一次完整的引擎+素材+BGM 装载，
并且新的 assetGeneration 会作废第一次的完成回调。**建议补上在途判断**：

```cpp
if (state_.previewSfxRuntime_ == nullptr
    || state_.previewSfxRuntimePrepared_
    || state_.previewSfxRuntimePreparationSequence_ != 0) {
    return;
}
```

**副作用核查**：唯一风险是「在途的那次 reload 失败后无人重试」。
但完成回调在失败时把 `previewSfxRuntimePrepared_` 置 `false` 且把
`previewSfxRuntimePreparationSequence_` 清零（`FrameBootstrap.cpp:1488-1491`），
下一次 `ensure…` 调用即可正常重试。**风险等级：低。**

---

## 3. 证据缺口与复现采样建议

**日志目录里没有能直接复核现象 1 的抓取。** 最新的 `logs 19`（= `logs.zip`）构建为
`1.1.0-beta.10 / git_revision=d20a87c034f6`，已含 `0d013404`，但整个 2026-08-12 会话只有
**22 行** runtime 日志，是一次纯启动、没有任何播放。`logs 12`–`logs 18` 停在 `a70e4012`
（2026-08-07），**早于** `0d013404`，只能作为「1.0 线快路径」基线。

补采一份（`--debug` 启动，打开带 PV 的谱）：

| 步骤 | 关注日志行 | 文件 |
|---|---|---|
| 启动后立刻按播放（不要先拖动） | `preview/runtime action=firework_pso_warmup_arm` / `_done`、`preview/interaction action=play_request` / `play_complete` | `miacode_runtime_debug.log` |
| 播放中暂停到中段，再按播放 | `preview/playback action=start_request` → `audio_startup_completion_accepted` → `commit … late_video_pending=1` → `weak_video_prepared` → `late_video_start_after_commit` | `miacode_audio_debug.log` |
| 同上 | `preview/stage_media action=prepare_playback_start` / `prepare_playback_ready` / `prepare_playback_timeout` / `commit_prepared_playback` | `miacode_audio_debug.log` |

判定：

- `commit` → `late_video_start_after_commit` 的时间差 = 时间轴冻结时长（F1 要消除的）
- `start_request` → `commit` 的时间差 = 启动延迟（决定要不要做 F3）
- `audio_startup_completion_accepted` 的 `requested` vs `effective` 差值 = F3 的 A/V 偏移风险量
- 出现 `prepare_playback_timeout` 则冻结吃满 800 ms

> `appendPreviewPlaybackLog` 走 **Audio** 通道（`MainWindow.TimelinePlayback.Internal.h:40`），
> `preview/playback` 全部落在 `miacode_audio_debug.log`，别找错文件。

---

## 4. 落地状态（2026-08-16）

前四项已实施；F3 按结论保留待测。

| # | 改动 | 状态 | 落点 |
|---|---|---|---|
| F1 | 权威时钟分支顺序：`qtPreviewPlaying_` 提到冻结分支之上 | ✅ 已改 | `MainWindow.TimelinePlayback.cpp` |
| A | 预热重居中改为 slack 门控 + 新增纯策略头与 spec | ✅ 已改 | `core/scene/PreviewFireworkWarmupPolicy.h`、`PreviewRuntime.{h,cpp}`、`PreviewFireworkWarmupPolicySpec.cpp` |
| F2 | commit 时冗余 PV seek 合并；40 ms 阈值提为具名常量 | ✅ 已改 | `PreviewStageMediaHost_Playback.cpp`、`PreviewStageMediaHostInternal.h` |
| §2.4 | `ensurePreviewSfxRuntimePrepared()` 增加在途判断 | ✅ 已改 | `MainWindow.PreviewWarmupAndSettings.cpp` |
| — | 后端恢复路径显式清除在途 identity（§2.4 的副作用修复） | ✅ 已改 | `MainWindow.WindowInteraction.cpp` |
| F3 | 乐观提交 | ⏸ 待测 | 需 §3 的实测数据 |

### 4.1 实施中发现并处理的两个副作用

审计时列的副作用清单不完整，实施阶段又抓到两个：

1. **`recoverPreviewBackendsAfterApplicationResume()` 依赖 `ensure…` 无条件重载。**
   它先把 `previewSfxRuntimePrepared_` 置 `false` 再调用 `ensure…`，期待必定重新下发。
   §2.4 的在途判断会让一个残留的 pending sequence 把恢复重载吞掉。
   处理：恢复路径显式清零 `previewSfxRuntimePreparationAssetGeneration_/Sequence_`
   ——「恢复」的语义本就是丢弃在途、从头再来。

2. **`PreviewRuntime::reset()` 清空 `noteMarkers` 但不补回合成 marker。**
   原来的每帧重居中会在下一帧自愈；改成 slack 门控后，这个洞会一直留到播放头走完 slack
   （若停在原地则永远不补），预热可能卡在无法确认的状态。
   处理：`reset()` 在 armed 未完成时补回合成 marker，与 `setNoteMarkers()` 的既有守卫对齐，
   并把「armed ⇒ noteMarkers 中恰有一个合成 marker」写成显式不变式（见 `PreviewRuntime.h`
   注释与 `cross-chain-linkage.md` §1）。

### 4.2 验证

- **Release 构建通过**（`build-macos-spec`，`-j4`）。
- **CTest 56/62 通过**，6 个失败：`oplog_self_test`、`timeline_model_spec`、
  `plain_code_editor_spec`、`preview_realtime_object_hot_path_spec`(SEGFAULT)、
  `touch_pad_authoring_state_spec`、`qtavplayer_platform_spec`。
  **已用 `git stash` 在干净 HEAD 上跑过基线：同样是这 6 个、同样的失败原因**（基线 6/61，
  差的 1 个是本次新增的 spec）。因此全部为既有失败，与本次改动无关。
- 新增 `preview_firework_warmup_policy_spec` **通过**；它不只测策略函数本身，而是把
  `fireworkWarmupNeedsRecenter` 的判定与 `buildPreviewJudgeFireworkLayerState` 的真实生命周期
  窗口对拍（在 slack 边界内外各采样，并遍历整个可达区间断言「策略说保留 ⇒ 层确实会画」），
  所以将来有人调 `kJudgeEffectFireworkDurationSeconds` 而忘了同步阈值时会红。
- `preview_firework_lifecycle_spec` **通过**（见 §0.2）。

> ⚠️ **尚未做运行时验证。** 以上只证明「编译通过 + 单测无回退」。
> F1/F2 的实际效果必须按 §3 在 Windows 上带 PV 复采一次日志才算闭环。

### 4.3 F3 仍不建议现在做

F1 只消除「冻结后跳变」，`点击 → commit` 的启动延迟仍在。是否需要 F3 取决于 §3 里
`start_request → commit` 的实测时长，以及 `audio_startup_completion_accepted` 中
`requested` 与 `effective` 的差值（决定乐观起跑会引入多大 A/V 偏移）。**先量再改。**

---

## 5. 涉及文件索引

- `src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp:436`（权威时钟 / F1）、`:453`（起播）
- `src/app/mainwindow/sections/timeline/MainWindow.PreviewPlaybackState.cpp:135`（weak video / §1.4a）、`:487`（commit 判据）、`:553`（finalize）、`:670`（暂停取秒 / §1.4b）
- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTick.cpp:136`（时间轴播放头读权威时钟）
- `src/preview/runtime/PreviewStageMediaHost_Playback.cpp:55`（prepare + 40 ms 短路）、`:194`（commit / F2）
- `src/preview/runtime/PreviewStageMediaHost_Timeout.cpp:57`（800 ms 超时）
- `src/preview/runtime/PreviewRuntime.cpp:253`（窗口重绑定重 arm / §2.2）、`:364`（setPlayheadSeconds）、`:1929`（arm）、`:1961`（合成 marker 放置）、`:2010`（每帧重居中 / 方案 A）
- `src/core/scene/PreviewPreparedSceneCache.cpp:123`（缓存键）、`:162`（rebuild）、`:165`（stable_sort）
- `src/core/scene/PreviewJudgeFireworkLayerState.cpp:149`（触发延迟）、`:156`（1.0 s 生命周期窗口）
- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp:654`（开谱异步预载）
- `src/app/mainwindow/sections/preview/MainWindow.PreviewWarmupAndSettings.cpp:135`（在途判断 / §2.4）
- `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp:1481`（reload 完成回调）
