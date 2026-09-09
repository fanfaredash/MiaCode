---
lifecycle: working
---

# MiaCode 预览音频模块只读代码审查报告

- 日期：2026-09-07
- 基准：`feature/qml-ui` @ 406b5963（工作区 clean）
- 范围：`src/audio/` 下 BASS 默认路径（`BassPreviewAudioBackend*`、`PreviewAudioWorker*`、`PreviewAudioHealth.h`、`PreviewBassDeviceLease`、`PreviewAudioDeviceWatcher`），以及与之直接耦合的 `src/app/runtime/playback/Tick.cpp`、`src/common/WaveformCache.cpp`、`src/tools/video_export/BassExportAudioBackend.cpp` 的 BASS 设备用法。Miniaudio 后端为 Linux 专用非默认路径，未审。
- 方法：纯静态阅读 + 仓库既有审计文档交叉。**未修改任何文件，未运行构建/测试**（仓库已有 `build-macos/` 里的 spec 二进制，但本轮无需借助）。
- 仓库内只有 `third_party/bass/include/{bass.h,bassmix.h}`，**没有 `bass_fx.h` 也没有 BASS 文档（chm）**；凡涉及 BASS/BASSmix/BASS_FX 语义的判断，都注明了"依据 BASS 公开文档记忆，需以 bass.chm 复核"。

---

## 0. 结论摘要

| 编号 | 问题 | 类型 | 严重度 | 置信度 | 与 H2（BGM 单拍少走 60-150 ms）关联 |
|---|---|---|---|---|---|
| A1 | `BASS_MIXER_NONSTOP` 让 `bass_audio_health` 的 underrun 探针在架构上失明（master 与每个 resampler 都 NONSTOP） | 已确认的诊断盲区 | 高（诊断） | 确凿 | 强：这是 H2 至今没有任何 `bass_audio_stall` 行的原因。**已修复，见 §7.1** |
| A2 | 暂停期间 `latestHealthSample_` 不刷新，恢复后最长 1 s 内 `bass_status.bgm_raw` 是**暂停前**的旧值 | 日志伪影（非音频故障） | 中（误导分析） | 确凿 | 无：解释了"恢复后第一拍 ±36/-145/-75/+3.7 s"现象，与 H2 无关。**已修复，见 §7.1** |
| A3 | `bass_status.bgm_raw` 与 `auth` 时基不同步（bgm_raw 最多陈旧 1 s，auth 是现场值）→ `bgm_delta_ms` 本身带 0~+1000 ms 的采样相位噪声 | 分析方法层面 | 中 | 确凿 | 中：H2 的"单拍 0.85-0.94 s"必须用相邻两次 worker 样本的 `sampledAtMs` 差归一化后才可信（当前 bass_status 不打印 sampledAtMs） |
| A4 | 现有日志**无法区分**"BGM 源单独断供"与"整条 master 混音停顿"；SFX 的触发是按 master 混音位置（`BASS_SYNC_POS|MIXTIME`）驱动的，master 停顿时 SFX 触发行照样"正常"出现 | 分析框架 | 高 | 高 | 强：H2 的"BGM-only"结论目前证据不足；见 §2.4 的判别方法 |
| A5 | master mixer 以 `BASS_ATTRIB_BUFFER=0`（零播放缓冲）+ `BASS_ATTRIB_MIXER_THREADS=8` 运行，且从未设置 `BASS_CONFIG_DEV_BUFFER/UPDATEPERIOD`；任何 >1 个设备周期的处理抖动都会直接掉音 | 架构隐患 | 高 | 高（机制确定，是否为本次根因未证） | **默认已改为 0 ms / 4 线程，见 §7.7**。零缓冲仍是残余欠载风险，`0/8` 保留为回滚参考；不能用非零 master buffer 作为已验证的稳定化方案。 |
| A15 | `a9a95856` 引入 `BASS_ATTRIB_BUFFER=20–40 ms` 时没有同步降低 `BASS_CONFIG_UPDATEPERIOD`（默认值为 100 ms）；本机实测所有 `BUFFER_MS≠0` 都产生严重错误听感，延迟量还呈现约 `100−BUFFER_MS` 的关系 | 时序契约回归 | 高 | 高（复现事实；精确内部调度机制仍需探针确认） | **本轮最直接的回归触发因素**：非零值撤回为生产方案，保留为诊断项。 |
| A6 | 混音线程回调 `handleMixerGroupSync` 在音频线程持 `QMutex`、深拷贝 `CollapsedEventGroup`、拼 `QString`、走 DebugLog 的 `std::mutex` 队列；`QMutex` 无优先级继承 → 经典优先级反转 | 实时性隐患 | 中高 | 高（机制确定） | **已完成代码路径修复，硬件复测待完成，见 §7.7**：回调不再等待互斥锁，且不再深拷贝、构造日志字符串或写日志。 |
| A7 | `anchorSfxScheduler` 在锁外先读 master 位置、再在锁内 `BASS_ChannelSetSync`；若目标位置在这几毫秒内已被越过，sync 永不触发且链式调度整体卡死到下次 seek/pause | 竞态（假设） | 高（若成立则 SFX 全部静音） | 中（需 BASS 文档确认"已越过位置的 SYNC_POS 是否触发"） | 弱 |
| A8 | BGM 的 `setCurrentSec` 用 `BASS_ChannelSetPosition`，而同文件的 SFX `stop/playOneShot` 用 `BASS_Mixer_ChannelSetPosition`；BASSmix 要求混音源 seek 用后者以复位 resampler/ramp/缓冲 | 库语义不符 | 中 | 高（不一致是事实；后果需复核文档） | 弱-中：只在 seek/变速时触发，不解释稳态缺口 |
| A9 | `PreviewBassDeviceLease` 是"先到先得、不校验参数"的进程级租约；三处 `BASS_Init` 参数互不相同（预览 `-1/48000/0`，波形 `0/24000/NOSPEAKER`，导出 `0/48000/NOSPEAKER`）；macOS 上没有 Windows 那样的 `process_device_conflict` 检查 | 生命周期隐患 | 中高 | 高（代码事实）；实际触发条件未证 | 弱 |
| A10 | `bass_audio_health` 里 `buffered_ms/buffered_bytes` 在 A5 的零缓冲配置下恒≈0，`init_latency_ms` 恒 0；这些字段无信息量 | 诊断 | 低 | 确凿 | — |
| A11 | `logTrackFileMissingAfterLoadIfNeeded()` 在文件**存在**时每个同步 tick 都做一次 `QFileInfo::exists` (stat) | 性能小问题 | 低 | 确凿 | 弱（在 worker 线程，不阻塞混音） |
| A12 | 变速路径 `applyPlaybackRateAtChartSecond` 是 pause→setSpeed→seek→play，注释自认"~50-100 ms BGM gap"；这是**设计内**的可听缺口 | 设计说明 | 低 | 确凿 | 若用户在"跳变"前刚拖过速度条，则这就是他听到的 |
| A13 | `pausePreviewPlaybackTransaction` 把 pauseSecond 改写成 BGM 现场游标 → H2 的缺口在暂停瞬间被"吸收"进权威时钟（视觉播放头回退 = 缺口） | 设计副作用 | 低 | 确凿 | 中：说明"暂停再播放后 bgm_delta 归零"不代表缺口消失，只是被换算掉了 |
| A14 | `setChartPath()` 在 `masterRunning` 时可直接 `initializeAssets()` 重建全部样本，`playbackSession_` 的 `backgroundTrackRunning` 等标志不复位 | 状态残留 | 低 | 中（取决于上层是否总是先停播） | 无 |

---

## 1. 对三个既定方向的深挖结论

### 2.1 方向 (a)：`BASS_ASYNCFILE` / 异步读缓冲 —— **排除**

**观察到的：**

- `src/audio/BassPreviewAudioBackendSample.h:210-232`：BGM 文件先被 `QFile::readAll()` 整体读进 `QByteArray bytes`，随后 `BASS_StreamCreateFile(TRUE /*mem*/, bytes.constData(), 0, size, BASS_STREAM_DECODE | BASS_STREAM_PRESCAN | BASS_ASYNCFILE)`。第一个参数 `TRUE` = **内存流**。
- 全仓 `grep BASS_CONFIG_ASYNCFILE_BUFFER` 零命中；`BASS_SetConfig` 只在 Windows 分支设了一次 `BASS_CONFIG_DEV_DEFAULT`（`BassPreviewAudioBackend_EngineInit.cpp:74`）。
- 没有任何 macOS 特化的缓冲配置。

**推断：** 内存流没有文件句柄，播放期间不存在磁盘读取；`BASS_ASYNCFILE` 只对文件流（mem=FALSE）有意义，在这里是无效标志（BASS 文档措辞需以 chm 复核，但"内存流不读盘"是事实）。因此"macOS 上异步读缓冲偏小 + 磁盘争用导致 BGM 单拍喂数不足"这个机制**在代码层面不成立**。

**保留的一条变体（未验证）：** BGM 的 `bytes` 是普通堆内存（WAV 可达几十 MB），机器磁盘常年接近满 → 内存压力下这段缓冲可能被压缩/换出，解码线程顺序读到冷页会产生页错误停顿。但这仍是"整条混音停顿"（见 §2.4），不是 BGM 源特有的断供。

### 2.2 方向 (b)：tempo / WSOLA（compact40）—— **稳态下找不到会少走的机制；但两个非稳态路径有 flush 语义**

**观察到的：**

- `BassPreviewAudioBackendImpl.h:40-44`：tempo 用 `BASS_FX_FREESOURCE | BASS_STREAM_DECODE` 创建；属性常量 `0x10000/0x10013/0x10014/0x10015` 手写（仓库没有 `bass_fx.h`）。
- `Impl.h:166-171, 195-199`：默认 preset `compact40` = sequence 40 / seek 15 / overlap 8 ms，固定值（非 SoundTouch 的 0=自动）。
- `Sample.h:438-543 setSpeed`：`BASS_ATTRIB_TEMPO` **只在源处于 `BASS_MIXER_CHAN_PAUSE` 时写**（G1 Commit 6 守卫），稳态播放期间不会触碰 tempo。
- 稳态播放期间没有任何代码对 BGM 源做 seek/flag 翻转（`syncBackgroundTrack` 只是 `maybeStartPendingBackgroundTrack` + 状态日志，`Transport.cpp:627-633`）。

**推断：**

- rate=1.0 时 `tempo=0`，SoundTouch 仍走 TDStretch，但输入/输出比率在平均意义上严格 1:1；固定 40/15/8 窗口的 CPU 代价按 44.1 kHz 估算是每 40 ms 做约 660×350 次 MAC 的相关搜索，远小于实时预算。**在没有任何外部干预的稳态下，我找不到让 tempo 流"少交付 60-150 ms"的代码路径。**
- 会触发 tempo 内部 flush（丢掉已吃进但未吐出的 ~sequence+seek ≈ 55 ms 输入，或者反之需要重新预热）的只有两条路径：
  1. `Transport.cpp:341-368 applyPlaybackRateAtChartSecond`：pause → setSpeed → `setCurrentSec` → play。注释 `:336-338` 自认"the only audible event is the brief BGM gap (~50-100ms typically)"。**这是设计内的、量级与 H2 完全一致的 BGM-only 缺口**，但只在用户变速时发生。
  2. `configureBackgroundTrackForSecond`（seek/暂停后重定位），同样 setCurrentSec+pause，之后 play。
- 与 H2 的关联：如果用户复现时**没有**动速度条也没有 seek，(b) 不能解释；如果动过，A12 就是答案。这一点需要回看复现日志里 `bass_live_rate_change` / `bass_sample_play reason=live_rate_resume` 行是否出现在缺口拍之前。

### 2.3 "恢复播放后第一拍 bgm_raw 异常" —— **已定位为日志伪影，与 H2 是完全独立的第三件事**

**证据链（按因果顺序）：**

1. `PreviewAudioWorker.h:36-39` + `PreviewAudioWorker.cpp:312-350`：worker 无论是否在播放，都按 ≥1 s 节拍调 `backend.sampleHealth()`。
2. `BassPreviewAudioBackend_PlaybackClock.cpp:246-248`：
   ```cpp
   if (!engineInitialized_ || !audioHealthPlaybackRunning_.load(...)) {
       return sample;          // <-- 在 :268 `latestHealthSample_ = sample` 之前就返回
   }
   ```
   即**暂停期间 `latestHealthSample_` 冻结在暂停前最后一次采样**。
3. `Transport.cpp:76-77 suspendPlaybackTransport` 把 `audioHealthPlaybackRunning_` 置 false；`Transport.cpp:211 startTransportFromCurrentAnchor` / `PlaybackClock.cpp:522 commit` 置回 true。恢复后 worker 最迟要 1 s 后才会再次真正采样。
4. 暂停期间用户拖动播放头 → `repositionPausedTransportToSecond`（`Transport.cpp:172-197`）→ `repositionMasterTransportClock` 在 `:164` 把 `lastStatusLogSecond = -1`，于是恢复后的**第一个** sync tick 立刻打 `bass_status`（`PlaybackClock.cpp:298-303` 的节流被绕过）。
5. `PlaybackClock.cpp:341, 353`：这一行的 `bgm_raw` 读的是 `latestHealthSample_.bgmRawSecond`，也就是**暂停前**的 BGM 位置；`auth` 是新位置。

**推断：** `bgm_delta_ms = auth − (旧 bgm_raw − offset)` 恰好等于**用户在暂停期间拖动的距离**（往前拖为正、往后拖为负），量级和方向都"不固定"正是因为它反映的是拖动距离而不是任何音频状态。观测到的 +36/-145/-75/+3.7 s 与此完全吻合。

**可用现有日志一步证伪/坐实：** 取每个异常拍的 `bgm_raw`，与它之前最近一条 `bass_sample_pause ... pause_bgm_raw=`（`Sample.h:606-613`）或暂停前最后一条 `bass_status.bgm_raw` 比较——如果相等（±1 s 内的推进量），伪影成立。

**修复方向：** `sampleHealth()` 早退分支也应更新 `latestHealthSample_`（或在 pause/seek/anchor 路径主动把它标记为 invalid，`logPlaybackStatus` 打印 `bgm_raw=-1` 并附 `sample_age_ms`）。

### 2.4 H2 的候选机制：先修正分析框架，再看代码里让"整条混音停顿"更容易的因素

**观察到的（决定分析框架的三个事实）：**

1. SFX 不是墙钟驱动的。`EventDrain.cpp:332-337`：每组打击音由 master mixer 上的 `BASS_SYNC_POS | BASS_SYNC_MIXTIME | BASS_SYNC_ONETIME` 触发，位置 = 锚点 decode 位置 + 相对秒数换算的字节数（`:327-331`）。`bass_sfx_mixer_trigger` 行里的 `group_second` 是谱面表里的值，不是触发时刻。**如果 master 整体停 100 ms，SFX 同样晚 100 ms 出声，但这行日志的内容完全"正常"**，只有该行的墙钟时间戳与 `group_second` 的差会多出 100 ms（前一轮是否检查过这个差值，未知）。
2. `bass_status` 里唯一来自 BASS 的位置量就是 `bgm_raw`（`BASS_Mixer_ChannelGetPosition(source)`，`PlaybackClock.cpp:259-261`）；`mixer=` 字段是 `(auth − sessionStart)/rate`（`:344-345`），纯墙钟换算。**master mixer 的实际混音位置从未被记录**。`currentSfxSchedulerChartSecond()`（`EventDrain.cpp:270-300`）已经会把 master decode 位置换算成谱面秒，但只在 `applyLevels/configureTimeline` 里用，没进日志。
3. `update_paint_node_stats` 正常只说明渲染线程没卡，与 CoreAudio IO 线程/BASS 混音线程无关。

**推断：** 当前证据**不足以**断言缺口只发生在 BGM 源。"整条 master 混音停了 60-150 ms，之后所有 BASS 位置（BGM、SFX sync）相对墙钟永久落后同样的量"与全部已知观测一致，且：

- 仓库历史已经见过同一现象：`src/app/runtime/playback/Tick.cpp:216-220`：
  > The pre-G1 implementation existed to absorb jitter in the BASS-master-mixer cursor (~50-100ms stalls from DXGI back-pressure, tempo-stream stalls, buffer underrun). With wall-clock now the master timeline ...
  以及 `docs/audit/PREVIEW_PLAYBACK_STUTTER_AUDIT_ZH.md:68`（G1 Commit 4 `90ec7c48` 删掉 BASS 游标分支）。也就是说，G1 之前视觉时钟跟随 master 游标时能直接看到 50-100 ms 的停顿；G1 换成墙钟后视觉不抖了，但**音频侧的停顿本身没有被处理**，现在以"BGM 相对墙钟永久落后"的形式重新露头。
- 听感上"音乐卡一下、打击音没事"不能作为 BGM-only 的证据：打击音是 <100 ms 的瞬态，一次 100 ms 的整体掉音很难在打击音上被察觉，在持续的音乐上却很明显。

**判别方法（不改代码，用现有日志）：**

- 对每个缺口拍，取缺口前后各一条 `bass_sfx_mixer_trigger`，计算 `(行墙钟时间戳 − 会话起点墙钟) − group_second/rate`。若这个差值在缺口拍前后跳变了与 BGM 缺口相同的量（60-150 ms）→ 整条混音停顿（A5 路线）；若差值不变 → 才是真正的 BGM 源单独断供。
- 最小埋点建议（一行日志）：在 `logPlaybackStatus` 增加 `master_chart=currentSfxSchedulerChartSecond(auth)`，让 master 位置与 `auth`、`bgm_raw` 同框；三者的两两差立即区分三种情形（master 停/BGM 停/都没停只是采样相位）。

**代码里让 master 停顿更容易的因素（按我认为的重要性排序）：**

- **A5** `EngineInit.cpp:372` `BASS_ATTRIB_BUFFER=0`：master 无播放缓冲，混音（含 tempo/SoundTouch、所有 resampler 的 SRC、8 个混音线程的汇合）都在设备回调的实时预算内完成；`:374` `BASS_ATTRIB_MIXER_THREADS=8` 让每个周期要等 8 个普通优先级线程完成；`BASS_CONFIG_DEV_BUFFER`、`BASS_CONFIG_UPDATEPERIOD` 从未设置（走 BASS 默认）。这种配置在 CPU 争用（QSG 渲染、Spotlight、视频导出）下的失效模式就是"偶发一次几十到一百多毫秒的掉音，然后所有 BASS 位置相对墙钟永久落后"。
- **A6** `EventDrain.cpp:386-502`：混音线程回调里 `QMutexLocker(schedulerMutex_)`（`:411`）→ `const CollapsedEventGroup group = preparedGroups_[groupIndex]`（`:447`，深拷贝 QVector/QString）→ `triggerGroup` 里多次 `BASS_Mixer_ChannelSetPosition/Flags/SetAttribute` → 解锁后 `appendAudioDebugLog` 走 `DebugLog.cpp:468-512` 的 `std::mutex` 队列。`schedulerMutex_` 是 `QMutex`（`BassPreviewAudioBackend.h:299`），macOS 上无优先级继承；worker 线程在 `anchorSfxScheduler`（`EventDrain.cpp:239-255`，锁内调 `BASS_ChannelSetSync`）、`logPlaybackStatus`（`PlaybackClock.cpp:343-381`）、`resetCursor`、`maybeStartPendingBackgroundTrack` 持锁时若被抢占，实时线程就在锁上等。头文件注释（`.h:283-298`）已经意识到这是"audio-callback lock"，但仍把 BASS 调用和堆分配留在了锁内。
- **A11** 每 tick 一次 `stat()`（在 worker 线程，不直接阻塞混音，但会拉长 worker 持锁前后的调度延迟；见 §3）。

**结论：** 我**没有定位到**"BGM 源单独少交付"的代码级根因；我定位到的是 (1) 当前日志无法支持"BGM-only"这个前提，(2) 代码里有一套明确会造成整条混音偶发停顿、量级匹配、且仓库历史已记录过的配置（A5+A6）。下一步应先用 §2.4 的判别方法把前提钉死。

---

## 3. 问题清单（含修复方向）

### A1. `BASS_MIXER_NONSTOP` 让 underrun 探针失明（已确认；正式收录）

- **定位：**
  - `BassPreviewAudioBackendSample.h:305-310`：每个样本的 resampler = `BASS_Mixer_StreamCreate(nativeFreq, 2, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_MIXER_NONSTOP)`。
  - `BassPreviewAudioBackend_EngineInit.cpp:356-359`：master = `BASS_Mixer_StreamCreate(48000, 2, BASS_SAMPLE_FLOAT | BASS_MIXER_NONSTOP | BASS_MIXER_POSEX)`。
  - `PlaybackClock.cpp:253-256`：探针读 `BASS_ChannelIsActive(master)` 和 `BASS_Mixer_ChannelIsActive(bgmSource)`。
  - `PreviewAudioHealth.h:57-60, 158`：`isUnderrun == (activity == Stalled)`。
- **成因：** NONSTOP 的语义是"没有源数据也不停，输出静音"（`bassmix.h:41` 注释：don't stall when there are no sources）。master 永远 PLAYING；BGM 源被读的是它在 resampler（也 NONSTOP）里的状态，只要 `CHAN_PAUSE` 没设就是 PLAYING。两个探针都不可能报 STALLED；`bass_audio_stall` 行在当前架构下不可能出现。
- **严重度/置信度：** 高（诊断）/ 确凿。
- **修复方向（任选或组合，不需要动播放链路）：**
  1. **比值探针（推荐，后端无关）：** worker 每次采样记录 `(bgmRawSecond, sampledAtMs)`，相邻两次样本算 `advance = Δbgm_raw / (Δt·rate)`；`advance < 0.97` 且 BGM 处于 running 即判定 underrun，并把 Δ 值写进 `bass_audio_health`。这直接把 H2 变成可自动检出的事件。
  2. **master 位置探针：** 同时采 `BASS_ChannelGetPosition(masterMixer_, BASS_POS_BYTE|BASS_POS_DECODE)`，做同样的比值；两者一起就是 §2.4 的判别器。
  3. **查最内层解码流：** 用 BASS_FX 的 `BASS_FX_TempoGetSource(tempo)` 拿到 decode 句柄，对它 `BASS_ChannelIsActive`/`BASS_ChannelGetPosition`——但对内存解码流它几乎不会 STALLED，价值低于 1/2。
  4. 若要保留 `ChannelIsActive` 语义，需要去掉 resampler 上的 NONSTOP（master 保留），代价是 BGM 到尾时 resampler 会 stall 并可能被 master 移除，需重审 `isAtOrPastEnd` 路径；不建议为了探针改播放链路。

### A2. 暂停期间 `latestHealthSample_` 不刷新（已确认；见 §2.3）

- **定位：** `PlaybackClock.cpp:242-270`（`:246-248` 早退在 `:268` 赋值之前）；读者 `:341, 353`。
- **修复方向：** 早退分支也写 `latestHealthSample_ = sample`（此时 `bgmRawSecond=-1`），并在 `bass_status` 加 `bgm_raw_age_ms = now − sampledAtMs`。

### A3. `bass_status` 中 `bgm_raw` 与 `auth` 不同时基（已确认）

- **定位：** `PlaybackClock.cpp:339-358`；worker 节拍 `PreviewAudioWorker.h:39`。
- **成因：** `auth` 是打日志当下的墙钟谱面秒，`bgm_raw` 是最多 1 s 前的 BASS 位置。`bgm_delta_ms` 天然带一个 0~1000 ms 的正向相位噪声，逐行比较无意义；只有 `bgm_raw` 相邻差 / `sampledAtMs` 相邻差才是干净的推进率。前一轮"单拍推进 0.85-0.94 s"若是用 `bass_status` 相邻行算的，需要确认两行取到的是**不同**的 worker 样本（否则会出现 0 和 2 的伪值）；如果是不同样本，则 Δt ≥ 1.000 s，0.85 s 的推进量才是真实落后。
- **修复方向：** `bass_status` 打印 `sampledAtMs`（或 age），分析脚本按样本序号去重。

### A4/A5. 零缓冲 master + 8 线程混音 + 默认设备缓冲（架构隐患；H2 强候选）

- **定位：** `EngineInit.cpp:337`（`BASS_Init(-1, 48000, 0, ...)`，无 `BASS_DEVICE_*` 标志）、`:372`（`BASS_ATTRIB_BUFFER 0`）、`:374`（`BASS_ATTRIB_MIXER_THREADS 8`）；全仓无 `BASS_CONFIG_DEV_BUFFER/UPDATEPERIOD/UPDATETHREADS/BUFFER` 设置。
- **成因：** 见 §2.4。补充一点：`Sample.h:326-327` 试图给 decode-only 的 resampler 也设 `BASS_ATTRIB_BUFFER=0`，注释承认它返回 NOTAVAIL——说明作者的意图是"整条链路零缓冲"。零缓冲是为了 SFX 触发延迟，但代价是 master 没有任何吸收抖动的余量。
- **严重度/置信度：** 高 / 机制确定，是否为本次根因未证。
- **修复方向：** (1) 先用 §2.4 判别；(2) 若坐实，给 master 一个小缓冲（`BASS_ATTRIB_BUFFER` 20-40 ms）或设 `BASS_CONFIG_DEV_BUFFER`，SFX 触发延迟增加的量可以在 `mixerSecondForChartSecond` 里用常量补偿；(3) `MIXER_THREADS` 降到 2 或 1（活动源实际只有 BGM + 少数 SFX，8 线程只是增加汇合等待）；(4) 记录 `BASS_INFO.minbuf`（已在 health 行里）与实际设备周期，看零缓冲是否低于 `minbuf`。

### A6. 混音线程回调内的锁/堆分配/日志（实时性隐患）

- **定位：** `EventDrain.cpp:411`（QMutex）、`:447`（深拷贝 group）、`:454-457`（`triggerGroup` → 每个事件 `applyVolume`+`BASS_Mixer_ChannelSetPosition`+`BASS_Mixer_ChannelFlags`，`Sample.h:566-592`）、`:485-491, 495, 498`（解锁后 QString 格式化 + `DebugLog.cpp:468-512` 队列互斥）。`reconcileTouchholdVoice`（`:504-544`）在锁内做 `touchholdOwnerSpanIndexAt` 扫描 + BASS 调用。
- **修复方向：** 回调里只做 BASS 调用和无锁（atomic/SPSC）状态递交；`CollapsedEventGroup` 预先展平成 POD 数组按索引访问；日志改为写入无锁环形缓冲由 worker 线程再格式化；`schedulerMutex_` 若必须保留，改成 `std::mutex` + `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)`（macOS 支持）。

### A7. `anchorSfxScheduler` 的"位置已越过"竞态（假设）

- **定位：** `EventDrain.cpp:227-228`（锁外读 master decode 位置）→ `:239-249`（锁内 `armNextGroupSyncLocked`）→ `:329-337`（`relativeSecond = max(0, target − anchor)`，`BASS_ChannelSetSync(BASS_SYNC_POS|MIXTIME|ONETIME, targetPosition)`）。
- **成因：** 从读位置到 SetSync 之间 master 已前进若干毫秒；若下一组事件距离锚点 <1 个设备周期（`resetCursor(second,false)` 只跳过 ≤second+eps 的组，`:50-60`，所以"锚点后 1-10 ms 处有音符"是常见情形），sync 位置在设置时已被越过。BASS 的 `BASS_SYNC_POS` 是"到达/越过"触发；对已越过的位置**我记忆中不会补触发**（需 bass.chm 确认）。一旦不触发：`scheduledGroupSync_ != 0` → `armNextGroupSyncLocked` 永远早退（`:305`）；`drainEvents` 因 `sfxSchedulerActive_` 早退（`:114-119`）→ **该会话所有打击音静音**，直到下一次 pause/seek 重新 anchor。
- **日志签名：** `bass_sfx_scheduler action=anchor ... next_group_idx=N` 后长时间没有 `bass_sfx_mixer_trigger`，且 `bass_status.armed_group_idx` 一直等于 N、`triggered_count` 不变。
- **修复方向：** 锁内读位置并把 `targetPosition` 至少设为"当前位置 + 一个混音块"；或在 SetSync 后立即再读一次位置，若已越过则直接在当前线程触发该组并继续 arm。
- **置信度：** 中。需要 (1) BASS 文档确认语义；(2) 在日志里找上述签名。

### A8. BGM seek 用 `BASS_ChannelSetPosition` 而不是 `BASS_Mixer_ChannelSetPosition`（库语义不符）

- **定位：** `Sample.h:366-375 setCurrentSec`（`BASS_ChannelSetPosition(source, ...)`）、`:334`（创建时同样）；对比 `:575 playOneShot`、`:623 stop` 用 `BASS_Mixer_ChannelSetPosition`。
- **成因：** BASSmix 文档要求混音源改位置用 `BASS_Mixer_ChannelSetPosition`，它在 seek 之外还会复位混音器为该源维护的重采样/ramp 状态与已缓冲数据；直接 `BASS_ChannelSetPosition` 会让 `BASS_Mixer_ChannelGetPosition` 在缓冲被消耗完之前报告不准确的位置（这正是 `bgm_raw` 的读法）。此外，这里的 `source` 是 BASS_FX tempo 流，`BASS_ChannelSetPosition` 会经 BASS_FX 转发给解码流并清空 SoundTouch 缓冲（BASS_FX 语义，需复核）。
- **与 H2：** 弱-中。只在 seek/变速时发生，不解释稳态缺口；但会让 seek 后头几百毫秒的 `bgm_raw` 不可信，与 A2 叠加会放大"恢复后第一拍异常"。
- **修复方向：** 统一为 `BASS_Mixer_ChannelSetPosition`；并检查 BASS_FX 是否要求对 tempo 流用 `BASS_POS_BYTE` 以外的标志。

### A9. 进程级 BASS 设备租约"先到先得、不校验参数"（生命周期隐患）

- **定位：** `PreviewBassDeviceLease.cpp:49-70`：`ownedReferences > 0` 时直接 `++` 并返回 ProcessOwned，**不调用 `api.initialize()`**；三处调用者的 `initialize` 分别是：
  - 预览：`EngineInit.cpp:337` `BASS_Init(-1, 48000, 0)`（真实默认设备）
  - 波形：`src/common/WaveformCache.cpp:150` `BASS_Init(0, 24000, BASS_DEVICE_NOSPEAKER)`（no-sound 设备，24 kHz）
  - 导出：`src/tools/video_export/BassExportAudioBackend.cpp:200` `BASS_Init(0, 48000, BASS_DEVICE_NOSPEAKER)`
- **成因：** 谁先 acquire，整个进程就用谁的设备与采样率；后来者拿到的是"别人的设备"。若波形解码（打开谱面时在 `QThreadPool` 跑，`WaveformCache.cpp:792-810`）抢在预览 `initializeAudioEngine` 之前，预览的 master mixer 会建在 device 0（无声设备）上——表现为完全无声而非跳变，所以大概率现实中预览总是先到；但反过来的问题存在：预览在 `invalidateOutputDevice()`（`EngineInit.cpp:449`）release 时若波形/导出恰好持有引用，`BASS_Free` 不会执行，设备切换后预览重新 acquire 仍复用**被切掉的旧设备**（`ownedReferences>0` 分支），Windows 有 `process_device_conflict` 检查（`:302-313`）能兜底，**macOS 没有**。
- **严重度/置信度：** 中高 / 代码事实确凿，触发条件未证。
- **修复方向：** 租约按 (device, freq, flags) 键控，或预览独占真实设备、解码类用途统一走 device 0；release 时若引用未归零但请求方是"设备失效"，强制标记 stale 让下次 acquire 重新 Init。

### A10. health 行中的缓冲字段无信息量（观察）

- **定位：** `PlaybackClock.cpp:204-216`（`BASS_ChannelGetData(master, BASS_DATA_AVAILABLE)`）；`Health.h:120-129`。
- **成因：** A5 的 `ATTRIB_BUFFER=0` 使 master 无播放缓冲，`buffered_ms` 恒≈0；`init_latency_ms` 因未用 `BASS_DEVICE_LATENCY` 恒 0（注释已说明）。分析时不要把 `buffered_ms=0` 当作"缓冲被吃空"的证据。

### A11. 每 tick 一次 `stat()`（小问题）

- **定位：** `PlaybackClock.cpp:107-121`：`QFileInfo::exists(trackPath)` 是短路链的最后一项，文件存在时**每次**都执行；调用点 `:828`（每个 `SyncBackgroundTrack`，即 GUI tick 频率，`Tick.cpp:203`）、`Transport.cpp:52, 206`、`PlaybackClock.cpp:542`。
- **修复方向：** 改成基于 mtime 的节流（每 1-2 s 一次）或复用 `preparedAssets_.trackStamp`。

### A12. 变速路径的设计内 BGM 缺口（设计说明）

- **定位：** `Transport.cpp:329-373`，注释 `:336-338`。
- **说明：** 若复现日志中缺口拍之前有 `bass_live_rate_change` 行，这就是听到的那一下，且量级一致；不是 bug 但值得在报告里排除/确认。

### A13. 暂停把 BGM 游标写回权威时钟（设计副作用）

- **定位：** `PlaybackClock.cpp:670-680`：`masterRunning && BGM 运行` 时 `result.pauseSecond = BGM 现场游标 − offset`。
- **说明：** H2 让 BGM 落后墙钟 100 ms；用户按暂停时权威时钟被改写成 BGM 位置，视觉播放头回退 100 ms，缺口从此"消失"（`bgm_delta` 归零）。分析"暂停后是否恢复"时要意识到这是换算，不是修复。

### A14. 播放中 `setChartPath` 重建样本不复位会话标志（小）

- **定位：** `Assets.cpp:252-275` → `initializeAssets()`（`:84-196`）→ `resetAssets()` 只 `disarmSfxScheduler`，`playbackSession_.backgroundTrackRunning/masterRunning` 等保持旧值；新建的 BGM 处于 CHAN_PAUSE，但 `bass_status.bgm_running=1`。
- **置信度：** 中（取决于上层是否总在切谱前 StopAll；命令队列没有强制顺序）。

---

## 4. 建议的下一步（按性价比排序，前两条不改代码）

1. **用现有日志做 §2.4 的判别：** 对每个缺口拍，比较缺口前后 `bass_sfx_mixer_trigger` 行的"墙钟时间戳 − group_second/rate"是否同步跳变。这一步决定 H2 是 A5 路线还是真正的 BGM-only。
2. **用现有日志验证 A2：** 异常拍的 `bgm_raw` 是否等于暂停前最后的 `pause_bgm_raw`/`bgm_raw`。
3. **最小埋点（两行）：** `bass_status` 增加 `master_chart=` 与 `bgm_raw_age_ms=`；`bass_audio_health` 增加相邻样本推进比。这三个字段足以把 A1/A2/A3/A4 一次性解决为可量化指标。
4. 若 (1) 指向整条混音停顿：先保持 `BASS_ATTRIB_BUFFER=0`，只比较 `MIXER_THREADS=4` 与旧参考值 `8`，同时处理 A6。任何非零 buffer 实验都必须先确认 `BASS_CONFIG_UPDATEPERIOD` 与回调时钟语义，不能再以“增加缓冲”作为默认修复。
5. 独立于本次症状：A7（需先查文档，再在日志找签名）、A8、A9 各自立项。

---

## 5. 已排除的可能

- `BASS_ASYNCFILE`/`BASS_CONFIG_ASYNCFILE_BUFFER` 偏小或 macOS 特殊处理 —— BGM 是 `mem=TRUE` 内存流（`Sample.h:226-232`），播放期间无文件 I/O；全仓无该配置。
- 稳态播放中 tempo 属性被改写 —— `setSpeed` 有 CHAN_PAUSE 守卫（`Sample.h:494-512`），且没有稳态调用点。
- 稳态播放中对 BGM 源的 seek/flag 翻转 —— `syncBackgroundTrack` 只做 pending-start 与日志（`Transport.cpp:627-633`）。
- `bass_status` 首拍异常来自 retained/anchored 重定位逻辑本身 —— `configureBackgroundTrackForSecond`（`Transport.cpp:511-597`）的 seek 值正确，异常来自 A2 的陈旧快照。
- 渲染线程卡顿导致 —— `update_paint_node_stats` 正常，且 BASS 混音线程与 QSG 线程无锁共享。

## 6. 仍未验证

- H2 是否真的 BGM-only（§2.4 判别未做；前一轮"SFX 正常"的判据不明）。
- 已通过 BASS 官方在线文档确认：`BASS_ASYNCFILE` 对内存流会被忽略；`BASS_ATTRIB_BUFFER=0` 表示不做通道播放缓冲，数据在最终混音需要时产出；低的非零值可能要求降低 `BASS_CONFIG_UPDATEPERIOD`；该更新周期默认 100 ms。仍未确认的细节包括 `BASS_SYNC_POS` 对已越过目标位置的行为（A7）、`BASS_Mixer_ChannelGetPosition` 与当前 BGM seek 组合的实际后果（A8），以及 macOS 实际回调线程调度。仓库仍无 `bass_fx.h` 和本地 CHM。
- A9 的实际触发顺序（波形解码与预览引擎初始化谁先）——需要看启动日志里 `bass_engine_ready` 与波形任务的先后。
- 内存压力/页错误（§2.1 变体）是否在复现机器上存在——需要 `vm_stat`/`memory_pressure` 与缺口时刻对齐。
- 复现日志中缺口拍前是否出现过 `bass_live_rate_change`（A12）。

---

## 7. 追加进展（2026-09-07 后续会话）

本节记录报告提交之后、同一轮排查里发生的三件事：A1/A2 落地修复、§2.4 判别方法的实测验证、以及一次独立的第二类症状（click/pop）调查。三者均已提交或即将随本次提交落地，编号延续用 `B` 前缀，避免与上面的 A1-A14 混淆。

### 7.1 A1、A2 已修复（commit `f9ab122b`）

- **A2**：`sampleHealth()`（`BassPreviewAudioBackend_PlaybackClock.cpp`）的早退分支现在也会发布一个新鲜的哨兵快照（`bgmRawSecond=-1`），不再让暂停前的旧值原地冻结；`bass_status` 新增 `bgm_raw_age_ms` 字段，直接标出该行 `bgm_raw` 陈旧了多久。
- **A1**：新增 `computeAdvanceProbe()`（`PreviewAudioHealth.h`），用相邻两次 worker 采样的 `bgm_raw` 推进量对比实际经过的墙钟时间（×rate），`<0.97` 判定为欠载，写入 `bass_audio_health` 的 `advance_ratio=`/`underrun_est=` 字段；新增 `backgroundTrackContinuityEpoch_`，在 seek/anchor/reposition（`configureBackgroundTrackForSecond`）和现场变速（`applyPlaybackRateAtChartSecond`）两处递增，避免跨越这些边界的两次采样被误判为欠载。
- 实测验证（2026-09-07 会话 `pid=28325`）：暂停 3 秒后恢复，`bass_status` 第一拍 `bgm_raw=-1.000000 bgm_raw_age_ms=189`（不再是历史脏值），下一拍 `bgm_raw=59.839002` 且位置正确——修复行为符合预期。

### 7.2 §2.4 判别方法已实测：H2 是"整条混音停顿"（A5 路线），不是 BGM-only

报告 §2.4 提出的判别法（比较缺口前后 `bass_sfx_mixer_trigger` 的"墙钟时间戳 − group_second/rate"是否同步跳变）用一次新会话实测坐实：

- 会话 `pid=12557`、`txn=3`（谱面 84.0s→101.28s 连续播放，无 seek）：`bass_status` 在谱面 ~98.2s→100.1s 出现两拍连续缺口（`bgm_raw` 单拍推进 0.94s、0.92s，累计少走约 152ms，且缺口未在下一拍被追回，`bgm_raw_delta_ms` 从 -872 永久性跳到 -1053 附近并稳定），同一时刻 `bass_sfx_mixer_trigger` 的实际触发墙钟时间也出现两次异常超额延迟（`+62.7ms`、`+81.8ms`，累计约 149ms，与 BGM 缺口量级一致）。
- 结论：SFX 触发同步延迟，说明不是"BGM 源单独断供"，而是**master 混音整体短暂停顿了约 150ms**，之后 BGM 和 SFX 调度相对墙钟一起永久落后。此前"打击音完全不受影响"的说法是被日志形式误导——`bass_sfx_mixer_trigger` 打印的是排期表值 `group_second`，不是触发时刻，看起来正常，实际触发墙钟时刻确实晚了。
- 这条结论把 A5（零缓冲 master + 8 线程混音）从"强候选"确认为**当时已知证据下最直接的根因**。

### 7.3 第二类症状：click/pop（与 H2 的"卡顿感"是不同的表现）

用户后续报告的"跳变"里，至少有一次的听感是**瞬间的啪/嗒（click/pop）**，明确不是"音乐拖一下"的卡顿感。核实当时的实测会话（83 段快速 play/pause 循环）后，两种既有判别方法（`bass_status` 逐拍对比、`bass_sfx_mixer_trigger` 细粒度时间戳对比）均未检测到任何异常——说明这类瞬时波形不连续不一定伴随可测量的位置漂移，需要新的诊断角度。

**排除的方向**（均已问过用户确认）：
- 暂停/恢复/seek 附近的无淡化硬切拼接 —— 用户确认这次的啪音**不在 play/pause 附近，是播放过程中自然出现的**。
- Touchhold 声道复用 —— 用户确认该谱面段落**没有 touchhold 音符**。
- 一开始怀疑的 `BASS_MIXER_NONSTOP` 静音硬切填补欠载 —— **前提有误，已订正**：`bassmix.h:41` 的语义是"没有源时不停摆"，不是逐 buffer 的静音替代；本工程所有源都是内存解码流（`BassPreviewAudioBackendSample.h:226-232`），不存在"无源"场景，NONSTOP 在这里跟稳态播放的波形连续性无关。

**保留/上调的候选**：
- **设备级极短欠载**（沿用 A5）：master `BASS_ATTRIB_BUFFER=0` + `MIXER_THREADS=8`，所有解码（含 BGM 的 BASS_FX SoundTouch 变速处理）都在输出线程的硬实时期限内完成，任何调度抖动都可能在 buffer 粒度上掉一拍，且现有 `advance_ratio`（5 秒窗口聚合）在数值上根本看不见几毫秒级的瞬时问题。
- SoundTouch/tempo 流的 overlap-add 拼接边界（`compact40` 预设，sequence 40ms/seek 15ms/overlap 8ms）——仅当播放倍速 ≠1.0 时适用。
- 削波（`Sample.h:291-293` normalize 增益上限 4.0，`clampSampleVolume` 上限 2.0，全链路无 limiter）。

### 7.4 新增诊断：输出端不连续性探针（本次一并提交）

为把"用户听到啪"变成可测量、可定位的日志事实，新增一个只读诊断探针（不改动任何播放/混音逻辑）：

- **原理**：在 `masterMixer_` 上挂一个只读 BASS DSP 回调（`BASS_ChannelSetDSPEx(..., BASS_DSP_READONLY)`），直接扫描输出端交错 float32 波形，检测三类事件：
  - `kind=step`：相邻采样跳变 `>0.25`（跨 block 边界保留上一个尾样本）——同时能捕捉设备欠载拼接和 SoundTouch 拼接缝。
  - `kind=clip`：连续采样 `|x|>=0.999`——捕捉削波。
  - `kind=late_callback`：本次 DSP 回调到达时刻，相对"上次到达 + 上个 block 应有时长"的延迟 `>2ms`——buffer 粒度的欠载探针，比 `advance_ratio` 的 5 秒窗口精细得多。
- **实时安全**：回调跑在 BASS 自己拉取 mixer 数据的线程上（不是 MiaCode 另起的线程），因此严格无锁、无日志、无堆分配，只写 POD 状态并 `tryPush` 进一个无锁 SPSC 环形缓冲（`kCapacity=512`）；由 `PreviewAudioWorker` 线程按既有 ~1Hz 节奏排空，格式化成 `bass_output_glitch kind=... mixer_pos=... ...` 日志行（环形缓冲满时只丢弃+计数，不阻塞生产者）。
- **代码定位**：
  - 纯函数与阈值：`src/audio/PreviewAudioOutputGlitchProbe.h`
  - 无锁环形缓冲：`src/audio/PreviewAudioOutputGlitchRing.h`
  - DSP 回调独占状态：`src/audio/BassPreviewOutputGlitchProbeState.h`
  - DSP 回调本体 + 挂载/卸载：`src/audio/BassPreviewAudioBackend_EngineInit.cpp`（`outputGlitchDspProc`、`attachOutputGlitchProbe`/`detachOutputGlitchProbe`）
  - worker 侧排空落日志：`src/audio/BassPreviewAudioBackend_PlaybackClock.cpp` 的 `drainOutputGlitchEvents()`
  - 单测：`src/tools/preview/PreviewAudioOutputGlitchProbeSpec.cpp`（跳变/削波/回调延迟/日志格式/环形缓冲的纯逻辑覆盖）
- `mixer_pos` 是"master mixer 引擎初始化以来的累计秒数"（因为 master 全生命周期保持 `ACTIVE_PLAYING`，与播放会话是否运行无关），不是谱面内位置，读日志时需要用同一时刻的 `bass_status`/操作日志换算回谱面秒。

### 7.5 实测确认：探针捕获到与 click/pop 症状精确对应的真实事件

会话 `pid=64120`（2026-09-07 `13:52:52`–`14:12:47` UTC，全程 `rate=1.000`，约 20 分钟）：

- 全场 1412 条 `bass_output_glitch`，**全部是 `kind=late_callback`**，一次 `kind=step`/`kind=clip` 都没有。
- 分布：约 99.7%（1369 条）迟到量级在 3-9ms（持续存在的背景抖动，说明当前零缓冲配置常年运行在临界边缘，只是平时几毫秒的迟到听不出来）；例外的 3 个离群值——14ms、46ms、**94ms**。
- 后两个离群值精确对应用户报告的一次"啪"：
  ```
  14:12:41.488  late_ms=46   （落在 txn=1020 播放窗口内，谱面 ~37s 附近）
  14:12:42.493  late_ms=94   （全场最大值）
  14:12:42.273  用户暂停（txn=1020 pause_exact）
  14:12:47.199  应用关闭
  ```
  从第一次离群值到关闭 5.71 秒，从最大值到关闭 4.7 秒，与用户描述的"出现后 5 秒内暂停并关闭"精确吻合。
- **`step`/`clip` 全场零命中，即使在 94ms 那次也没有**，是这次最值得注意的反常点：说明这次 click/pop 大概率不是"波形本身出现了超过 0.25 幅度的采样跳变"，而是回调迟到本身导致设备层面插入了重复/静音数据造成的听感断裂——这个环节发生在 BASS mixer 往外吐数据"之后"的设备缓冲层，当前探针挂载点（mixer 的 DSP 点）看得到"这一拍来晚了"，看不到"来晚之后设备具体怎么补的这个空档"。

**结论**：设备级极短欠载（A5 的具体表现）现在有了跟症状精确对应的实测时间戳和量级证据，从"理论候选"升级为**当前证据下最直接的根因**，且这条证据同时适用于 H2（卡顿型）和本节的 click/pop 型两种症状——两者很可能同源，只是欠载时长不同导致听感不同（更长的欠载表现为卡顿，更短的表现为一声啪）。

### 7.6 下一步（后续会话已开始执行）

- “给 master 一点缓冲余量能否消除 click/pop”的旧假设已被复现硬件否定：所有 `BUFFER_MS≠0` 的实例均有严重错误听感。受校验的 env 覆盖仍保留，但非零值只用于有明确问题的诊断，不再安排盲扫或作为验收候选。
- 若要进一步定位"迟到之后设备具体怎么补空档"：`step` 阈值 0.25 可能偏高，可以尝试调低阈值再测一轮，或者在设备输出更靠后的位置（如果 BASS/CoreAudio 允许）再加一层探针。
- A6（混音线程回调内的锁等待/堆分配/日志）已完成代码路径修复，见 §7.7；仍需与 A5 一起做硬件复测。

### 7.7 A5/A6 代码级修复（2026-09-08，硬件验收待完成）

本轮按 §7.6 落地了两组改动：

- **A5 缓冲与线程策略**：master mixer 默认从 `BUFFER=0 / MIXER_THREADS=8` 改为
  `0 ms / 4`。新增 `MIACODE_BASS_MASTER_BUFFER_MS`（`0..500`）和
  `MIACODE_BASS_MIXER_THREADS`（`1..16`）作为受校验的 A/B 覆盖；非法、非有限或越界值
  回退到新默认。`bass_engine_ready` 同时记录请求值、BASS 实际回读值、set/read 成功与覆盖
  有效性，避免测试者误以为属性已生效。旧组合可显式设为 `0 / 8` 复现。
- **SFX 可听时间补偿**：`SfxSchedulerAnchor` 携带 BASS 实际回读的输出缓冲秒数，
  `mixerSecondForChartSecond()` 将 sync 提前该时长。decode 游标到 chart time 的反换算保持不变，
  因为缓冲只改变声音何时抵达设备，不改变 master decode 游标的推进速度。
- **A6 实时回调**：`handleMixerGroupSync()` 改用无等待 `tryLock()`；若 worker 正持锁，回调只把
  当前唯一 sync handle 写入原子槽，下一次 worker tick 在缓冲提前量内完成该动作。回调诊断通过
  固定容量 POD SPSC ring 交给 worker 格式化，BASS 错误也通过 thread-local POD 槽延迟报告。
  `CollapsedEventGroup` 改为常量引用，已规范化的 SFX kind 优先直接查表，回调路径不再构造
  `QString`、深拷贝组或进入 DebugLog。`bass_sfx_mixer_deferred` 和最终行的 `deferred=1` 可量化
  实际发生过多少次互斥争用。

代码验证已完成：Windows Release `MiaCode` 目标成功编译链接，
`bass_preview_sfx_scheduler_policy_spec` 通过，覆盖新默认、旧组合 A/B、非法值回退、30 ms 调度
提前量以及 callback ring 的 FIFO/满载行为。

**仍不能仅凭代码验证宣称症状消失。** 下一步必须在原复现硬件上分别运行新默认 `0 / 4` 与
旧组合 `0 / 8`，比较相同播放区间内 `bass_output_glitch kind=late_callback` 的次数/离群值，并
主观确认 click/pop 与 H2 卡顿是否消失。若 `0 / 4` 仍有离群，应继续排查设备周期和系统调度，
而不是增加 `BUFFER_MS`。

### 7.8 本轮复评：非零 master buffer 的建议撤回（2026-09-09）

用户在复现硬件上确认：`BUFFER_MS=20/30/40` 均导致音频轨道严重错误；单线程实例尤其严重。`30/1 → 30/4` 只消除了线程数这一项混杂因素，听感问题仍在；本轮 `7a7109e4` 已将生产默认改为 `0/4`，并推送到 `feature/qml-ui`。

这与 BASS 官方语义相符：[`BASS_ATTRIB_BUFFER`](https://www.un4seen.com/doc/bass/BASS_ATTRIB_BUFFER.html) 说明 0 是不做播放缓冲、在最终混音需要时请求数据；低的非零值可能需要同时降低更新周期。[`BASS_CONFIG_UPDATEPERIOD`](https://www.un4seen.com/doc/bass/BASS_CONFIG_UPDATEPERIOD.html) 的默认值是 100 ms，而当前工程没有显式设置它。因此“20/30/40 ms 的迟到量约为 100 ms 减去请求 buffer”是一个有官方机制支持、但仍需回调探针独立验证的高置信度假设。macOS 的设备缓冲是另一层：[`BASS_CONFIG_DEV_BUFFER`](https://www.un4seen.com/doc/bass/BASS_CONFIG_DEV_BUFFER.html) 在 macOS 不可用，不能把它与 master channel buffer 混为一谈。

建议重新归类如下：

- **撤回**：30 ms 生产默认、正 buffer 作为普遍稳定化措施、1 个 mixer 线程默认、以及“读取成功/补偿成功就等于听感正确”。
- **保留**：`0/8` 作为已知参考/回滚组合；`0/4` 作为当前生产候选；线程数覆盖用于隔离容量因素；读回值、late-callback、A6 回调实时性、A7 sync 竞态、A8 seek 语义、A9 设备租约等诊断方向。
- **重述**：`MIACODE_BASS_MASTER_BUFFER_MS` 非零值是复现机上的已知有问题实验项，不是受支持的低延迟配置；`MIXER_THREADS` 是并行处理容量参数，不是时钟修复手段。

从 v1 到 v2，最重要的行为变化不是 BGM 文件读取：v1 已经使用内存解码流、BASS_FX tempo、零 master buffer 和 8 个 mixer 线程。v2 期间先加入 macOS BASS 后端，再把 SFX 从 GUI 墙钟排程改成 master mixer sync，随后把 backend/BASS 操作迁移到 worker，并加入进程级设备租约与 hotplug barrier。这些变化增加了时钟、回调、生命周期之间的耦合和对调度抖动的敏感度，但不能单独解释本轮 `BUFFER_MS≠0` 的回归。最直接的回归来自 `a9a95856`：把 master buffer 改成非零，同时没有调整默认 100 ms update period，并叠加了 SFX buffer 补偿与回调路径改造；`d3c50201` 仅将线程数改回 4，`7a7109e4` 才移除了已确认的正 buffer 触发因素。

最后，`0/4` 仍不是“已经听感验收通过”的结论。既有日志还表明旧 `0/8` 零缓冲路径存在独立的短时欠载风险；下一次用户 A/B 应只比较 `0/4` 与 `0/8`，并同时看 `late_callback` 离群值。只有在 master/device 位置、BGM 产出位置和设备回调节奏被分别记录后，才能最终区分 BGM 单独欠供与整个 master/device 停顿。
