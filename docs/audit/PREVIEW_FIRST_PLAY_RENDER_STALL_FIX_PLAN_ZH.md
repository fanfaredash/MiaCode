# PV 首播画面掉帧：复核结论与修复方案

- 日期：2026-08-19（Asia/Shanghai）
- 基线：`57c01ecc`（`dev`，即 beta.15 诊断提交落库后的状态）
- 对应审计：`docs/audit/PREVIEW_FIRST_PLAY_RENDER_STALL_HANDOFF_AUDIT_ZH.md`（下称「审计」）
- 复核范围：审计 §5.1、§5.2、§5.4 与 §6A / §6C / §6D
- **本次复核没有复现环境**（macOS，无 AMD / D3D11VA / DWM）。因此：所有结论只来自代码与审计中已归档的日志；
  所有改动均已在 macOS Release 下编译通过，新增策略以 CTest 规格验证；**Windows 现场行为尚未验证**。

---

## 0. 先纠正审计里的一处证据读法

审计 §5.2 与 §2.2 用 `position_ms=121066`（≈ 完整时长）与 `last_pts_ms=1267` 的矛盾来论证假 EOM。
这个矛盾成立，但 `position_ms` 本身不是独立证据：

```cpp
// third_party/QtAVPlayer/src/QtAVPlayer/qavplayer.cpp
qint64 QAVPlayer::position() const
{
    ...
    if (mediaStatus() == QAVPlayer::EndOfMedia)
        return duration();          // ← EOM 时按定义返回时长
    return d->pts() * 1000;
}
```

也就是说，只要状态是 `EndOfMedia`，`position_ms` **必然**等于 duration，真结束和假结束都一样。
真正的证据是 `active_elapsed_ms=1446`、`frames=38`、`last_pts_ms=1267` 对 121 秒时长的落差。
本次新增日志因此直接记录「解码进度 vs 媒体自身时长」，不再依赖 `position_ms`（见 §2）。

这不影响审计的结论，但会影响后续判读：不要再把 `position_ms≈duration` 当成新证据。

---

## 1. §5.1 / §6A —— 把 `render_submit` 从黑盒拆开（已实现）

### 问题

`render_submit_ms=1005.612` 是 `beforeRendering → afterRendering` 的一个包围式区间。这一段里至少混了三类完全
不同的工作，而现场日志无法区分它们：

1. QRhi 资源阶段：纹理/缓冲上传、资源创建、交换链取图；
2. Render pass 记录阶段：真正录制 draw call —— **首次使用某个 material 时，图形管线（PSO）/ 着色器变体就是在这里由驱动构建的**；
3. `endFrame` 尾段。

审计 §5.1 列的五种可能机制，前两种（PSO 首次编译 / RHI flush + 资源状态转换）恰好分居前两个阶段。
不拆开就永远只能猜。

### 改动

`src/preview/quick_scene/PreviewQuickSceneRoot.cpp/.h`

- 新挂 `QQuickWindow::beforeRenderPassRecording` / `afterRenderPassRecording` 两个 direct 连接（与既有相位钩子同一渲染线程）。
- `render_frame_profile` 增加三个字段：

  | 字段 | 区间 | 含义 |
  |---|---|---|
  | `resource_prep_ms` | beforeRendering → beforeRenderPassRecording | QRhi 上传 / 资源创建 / 交换链取图 |
  | `pass_record_ms` | beforeRenderPassRecording → afterRenderPassRecording | draw call 录制；**首次 PSO / 着色器变体在此构建** |
  | `submit_tail_ms` | afterRenderPassRecording → afterRendering | 提交尾段 |

- 新增 `action=render_stall_context`：进程内第一次 `render_submit_ms >= 250` 的帧写一行，含
  `dominant=resource_prep|pass_record|submit_tail` 结论。即使采样把 `render_frame_profile` 抑制掉，
  这一行也一定在。**该行不做任何 DXGI/COM 调用**（它跑在渲染线程）。
- 顺带修：`syncVisibleHostWindowBinding()` 之前没有断开这些相位连接，同一窗口重新绑定（F11 / re-parent）
  会叠加一整套 direct lambda。现在统一断开再重建。

`src/preview/runtime/PreviewStageMediaHost_Diagnostics.cpp`

- `first_playback_bridge_trace` 在 `action=render_stall` 之后追加 `action=render_stall_vram`，
  每个 DXGI 适配器一行 `formatAdapterVideoMemoryPayload`（budget / usage / `local_over_budget`）。
  这条跑在 GUI 线程，一次事务只发一次，所以 DXGI 往返是可接受的。
  审计 §5.6 想要的显存预算数据现在就在卡顿现场的时间点上。

`src/app/gpu_adapter_probe.cpp`

- `quick_shell/device` 增加 `umd_driver_version=`（`IDXGIAdapter::CheckInterfaceSupport(__uuidof(IDXGIDevice))`）
  与 `feature_level=`。**此前整份日志没有任何地方记录显卡驱动版本**，而这是任何「首次提交卡住」报告
  必然被问的第一个问题（审计 §5.6 只给出了适配器名）。

### 下一份现场日志怎么读

```
preview/quick_scene action=render_frame_profile ... render_submit_ms=1005.612
    resource_prep_ms=… pass_record_ms=… submit_tail_ms=…
```

- `pass_record_ms` 占大头 → 首次管线构建 / 着色器编译（审计 §5.1 机制 1）。配合下面 §3 的 `layer_first_draw` 可直接点名是哪个图层。
- `resource_prep_ms` 占大头 → 纹理上传 / 资源创建 / 交换链取图（机制 2、5；同时看 `render_stall_vram` 是否 `local_over_budget=1`）。
- 三段都小、`swap_gpu_ms` 大 → present / vsync 侧，不是提交侧。
- 三段都小而 `render_submit_ms` 仍然大 → 说明阻塞发生在信号之间的 Qt 内部，此时才值得上 ETW / GPUView。

**这一步不改任何渲染行为，只补可见性；符合审计 §6A「先补齐可见性，不先改设备路径」。**

---

## 2. §5.2 —— 假 `EndOfMedia`：定性、取证、有界恢复（已实现）

### 2.1 机制分析（代码级，非猜测）

`QAVPlayer` 报 `EndOfMedia` 的唯一来源是 demux 循环里的这个条件：

```cpp
// qavplayer.cpp doDemux()
if (demuxer.eof() && videoQueue.isEmpty() && audioQueue.isEmpty()
    && subtitleQueue.isEmpty() && filters.isEmpty() && !isEndOfFile()) {
    setPendingMediaStatus(EndOfMedia);
    q_ptr->stop();
}
```

`demuxer.eof()` 又只由这里写：

```cpp
// qavdemuxer.cpp QAVDemuxer::read()
int ret = av_read_frame(...);
if (ret < 0) {
    if (ret == AVERROR_EOF || avio_feof(d->ctx->ctx()->pb))
        eof = true;
}
```

关键在 `avio_feof()`。FFmpeg 的 `AVIOContext::eof_reached` **不只在真正读到文件尾时置位**——
`fill_buffer()` 在任何一次底层读取失败时都会置位并把 errno 写进 `AVIOContext::error`：

```c
if (len <= 0) {
    s->eof_reached = 1;      /* 任何读失败都会走到这里 */
    if (len < 0) s->error = len;
}
```

而 `eof_reached` 一旦置位就**粘住**（`fill_buffer` 开头直接 return），只有 `avio_seek()` 会清掉它。

结论：**一次瞬时的字节读失败（磁盘抖动、杀软拦截、文件被其他进程短暂占用）就足以把这个 121 秒文件
永久变成「已经结束」，直到发生一次 seek。** 这与审计记录的现象完全吻合：

- 冷启动最易复现（首次读盘 + 杀软首次扫描）；
- 多次播放后概率下降（页缓存已热）；
- 无 PV 不触发（没有这条 AVIO 路径）；
- `reseek=0` 的那次仍然出现 —— 因为 commit 不再 seek，也就没有任何东西能清掉这个粘滞位。

`ret < 0` 且 `avio_feof()` 为真时，`AVIOContext::error` 就是区分「真结束」和「读失败」的判据：
真结束时它是 0，读失败时它是负的 errno。**现场日志此前完全没有这个字段。**

### 2.2 取证（vendored 层只观测，不改行为）

新增 `third_party/QtAVPlayer/src/QtAVPlayer/qavpreviewdemuxdiag_p.h`（MiaCode addition，跨平台，
已登记进 `QtAVPlayer.cmake` 的私有头列表），计数器实现在 `qavdemuxer.cpp`：

| 字段 | 含义 |
|---|---|
| `demux_eof_events` | eof 被置位的次数 |
| `demux_eof_averror` | 来自 `AVERROR_EOF`（真结束） |
| `demux_eof_avio` | **只**来自 `avio_feof()`（可疑） |
| `demux_read_failures` | 非 EOF 的读失败 |
| `demux_last_read_result` | 最后一次负返回值 |
| `demux_last_avio_error` | eof 置位时的 `AVIOContext::error` |
| `demux_last_eof_byte_pos` | eof 置位时的 `avio_tell()` |
| `demux_seek_resets` | seek 清掉粘滞 eof 的次数 |

计数器是 relaxed 原子，写在 demux 线程，且第一次置位的判定放在原本就持有的互斥区内（避免与 `d->eof` 竞争）；
GUI 线程只在 EOM 这种低频时刻读一次。**解码控制流一行未改** —— vendored 层负责观测，产品层负责决策。

判读规则：`demux_eof_avio` 增加且 `demux_last_avio_error != 0` ⇒ 这是一次伪装成流结束的 I/O 失败。

### 2.3 定性策略（可测试，跨平台）

新增 `src/core/video/PreviewEndOfMediaPolicy.h` + `preview_end_of_media_policy_spec`（已注册 CTest，通过）。

唯一判据是 **解码进度 vs 媒体自身时长**：

- `natural`：`duration - decoded <= max(0.35 s, 3 × 帧间隔)` —— 解码器确实到了文件末尾。
- `stale`：落差超过上述松弛量 —— 就是审计 §5.2 那次（121.066 s vs 1.267 s）。
- `unknown`：时长未知或一帧都没解出来 —— **不做任何恢复**（在信息缺失时恢复，等于把坏文件变成无限重载循环）。

规格用例直接钉住两份审计的现场数据：

- `PREVIEW_AUTO_PAUSE_INITIAL_DIAGNOSIS_ZH.md` 那个 0.333 秒 / 10 帧的 `pv.mp4` 必须判为 `natural`，
  **且不因为「谱面还剩 113 秒」而改判**。这正是那份审计反复强调不能用「短于 N 秒就忽略」阈值的原因。
- 本次 121 s / 1.267 s 的现场必须判为 `stale` 且值得恢复。
- 低帧率源（2 fps）在真结束时必须仍判 `natural`（帧间隔松弛项覆盖固定下限）。
- 时间轴已经走过 PV 末尾时，即使是 `stale` 也不恢复 —— 画面结果一样，恢复没有收益。

### 2.4 有界恢复阶梯

`PreviewStageMediaHost::handleVideoEndOfMedia()` / `tryRecoverFromStaleEndOfMedia()`（`PreviewStageMediaHost_Timeout.cpp`）：

| 次序 | 策略 | 说明 |
|---|---|---|
| 0、1 | `seek_resume` | `seek(期望位置)`，**等 `seeked` 确认后再 `play()`** |
| 2 | `reload` | `setSource("") → setSource(path) → seek → play` |
| ≥3 | 放弃 | 写 `stale_end_of_media_exhausted`，保留最后一帧 |

两处细节是必须的，否则恢复本身会制造新缺陷：

1. **必须先 seek 再 play，且要等 `seeked`。** `QAVPlayer::play()` 里有
   `if (d->isEndOfFile()) seek(0);` —— 在 eof latch 还没清掉时直接 `play()` 会把 PV **从头重播**，
   正是要修的故障的另一种形态。因此 `seek_resume` 把 `play()` 推迟到 `QAVPlayer::seeked` 回调里
   （`PreviewStageMediaHost_Backend.cpp` 的 `seeked` 回调 + `staleEndOfMediaResumePending_`），并配 900 ms 超时升级为 reload。
   `reload` 路径不需要等待：`setSource` 内部走 `terminate()`，latch 已被清空。
2. **seek 恰好也是清掉 FFmpeg AVIO 粘滞 eof 的唯一手段**（`avformat_seek_file → avio_seek → eof_reached = 0`），
   所以最便宜的一档同时也是对 §2.1 那个机制最对症的一档。

预算按「每次加载的媒体」重置（`clearMedia` / `loadVideoMedia` / `releaseDecoderForFileReplace`），
不会跨谱面累积。

### 2.5 不变的红线

`handleVideoEndOfMedia` 的**任何**分支都不会结束主 transport。
`PREVIEW_AUTO_PAUSE_INITIAL_DIAGNOSIS_ZH.md` 定位的那条耦合（背景视频 EOM → `playbackFinished` →
`finishQtPreviewPlaybackAndReturnToEntry`）保持已解除状态；主播放的自然结束入口仍然唯一：
`MainWindow.PreviewTick.cpp::onQtPreviewTickAtSecond()` 对 `previewPlaybackEndSeconds()`。
该契约已写进三份 `cross-chain-linkage` 参考的 §5。

### 2.6 新日志

```
preview/stage_media action=end_of_media_classified
    eom_class=stale duration_ms=121066 decoded_ms=1267 expected_ms=1446 shortfall_ms=119799
    frame_interval_ms=33.3 frames=38 was_active=1 recoverable=1
    recoveries_used=0 recovery_budget=3 txn=1 last_seek_ms=0
    demux_eof_events=1 demux_eof_averror=0 demux_eof_avio=1
    demux_read_failures=0 demux_last_read_result=-541478725
    demux_last_avio_error=-4058 demux_last_eof_byte_pos=… demux_seek_resets=0
preview/stage_media action=stale_end_of_media_recover attempt=0 strategy=seek_resume …
preview/stage_media action=stale_end_of_media_resumed second=1.446 position_ms=1446 recoveries=1
```

---

## 3. §5.4 / §6D-1、§6D-2 —— 预热（部分实现 + 明确的后续方案）

### 3.1 已实现：预热完成语义升级为「已呈现」（§6D-2）

原判据：烟花图层在 `updatePaintNode` 里返回了非空节点 → `fireworkLayerDrawSignal_` 自增 →
GUI 线程 `handlePresentedFrame()` 发现它变了就判定 done。

两个弱点叠在一起会提前收工：节点是在 sync 阶段产生的，**比像素早整整一次 render+swap**；
而完成检查挂在 **queued** 的 `frameSwapped` 上，可能滞后一帧。两者相加，预热可能在那次管线构建
真正完成之前就宣布结束 —— 这正是审计 §6D-2 说的「预热完成 ≠ 已完成可观察的 render/present」。

改动：`PreviewQuickSceneRoot` 在 `updatePaintNode` 里只做标记，新增一条 **direct** 的 `frameSwapped`
钩子在真正 swap 之后才把信号推给 runtime；`PreviewRuntime::notifyFireworkLayerProducedNode()`
更名为 `notifyFireworkLayerPresentedNode()`，注释同步改写。现在 done 的含义是
**「一个包含该节点的帧已经上屏」**。

### 3.2 已实现：首次绘制可见性（把 §5.4 的猜测变成数据）

审计 §5.4 说预热「可能」没覆盖首用状态，但没有任何日志能说出**卡住的那一帧到底是谁在首次绘制**。

改动：`PreviewTextureLayerStats` 增加 `nodeProduced`；`updateLayerSlot()` 返回是否产出节点；
`PreviewQuickSceneRoot::noteFirstLayerDraws()` 为每个图层写一次
`preview/quick_scene action=layer_first_draw layer=… update_paint_count=… build_ms=… candidates=… active=…`。

它从 `updatePaintNode` 发出，因此在日志里**紧挨着同一帧的 `render_frame_profile` 之前**。
一行 `layer_first_draw` 后面直接跟着一个几百毫秒的 `pass_record_ms`，就是「该图层首次使用付了这笔账」的直接证据。

### 3.3 已实现：PV 管线是否已预热（§6D-1 的前置事实）

`commit_prepared_playback` 增加 `frames_before_commit=` / `video_prewarmed=`。
按当前实现，PV 在加载时就 `pause() + seek(0)`，第一帧在**暂停态**推给 `VideoOutput`
（`PreviewStageMediaItem.qml` 的 `visible` 绑定 `hasVideoFrame`），所以理论上视频 material 在按播放前就已编译。
`video_prewarmed=0` 会直接证伪这个假设 —— 而这正是 §6D-1 要不要做「首张硬解纹理预热」的判据。
**在拿到这个字段之前不要先写预热代码。**

### 3.4 未实现（有意）：逐图层合成 marker 预热

§6D-1 完整版是「为每一类首用状态各造一个离屏合成体，分帧切片预热」。本次**不做**，理由：

- 每个图层的 layer-state builder 有各自的接受条件（type 串、lane、时间窗、slide 路径……）。
  造出一个「永远画不出来」的合成体，结果是预热每次都走超时兜底 —— 复杂度增加、收益为零，且很难在无复现环境下发现。
- §3.2 的 `layer_first_draw` 会**直接点名**需要预热哪几个图层。先拿数据，再按名单造合成体，是更短的路径。

后续实施建议（拿到 `layer_first_draw` 名单后）：

1. 把 `appendFireworkWarmupMarker()` 泛化成 `appendWarmupMarkers()`，按名单为每个图层各产一个离屏合成体
   （沿用现有的 `touchPoint = (-1e6, -1e6)` 手法：GPU 裁掉全部三角形，PSO 照样编译、纹理照样上传）。
2. 每个参与图层沿用 §3.1 的「已呈现」判据各自确认，全部确认后才 done；保留 present 上限兜底。
3. **分帧切片**：一帧一个图层，避免把卡顿从「按播放时」搬到「加载时」。
4. 每个合成体都要有一条 `preview_*_warmup_policy_spec` 式的规格，用真实 layer-state builder 断言
   「这个合成体在这个 playhead 上确实会被画出来」——`preview_firework_warmup_policy_spec` 已经是现成范式。

---

## 4. §6C —— 对照实验最小集合（待现场执行）

每次只改一个变量；`--debug` 由 `Start_MiaCode_Debug.bat` 提供。
**收集目录以日志里的 `runtime_log_path` 为准**，`MIACODE_LOG_DIR` 生效时通常是包根 `logs/`。

| # | 目的 | 命令 | 关键判读字段 |
|---|---|---|---|
| C0 | 主捕获（默认路径 + 全部新诊断） | `set MIACODE_PREVIEW_FRAME_PACING_DIAG=1&&set MIACODE_PREVIEW_QSG_RENDER_TIMING=1&&call Start_MiaCode_Debug.bat` | `resource_prep_ms` / `pass_record_ms` / `submit_tail_ms`、`render_stall_context dominant=`、`layer_first_draw`、`render_stall_vram`、`preview/qsg_timing` |
| C1 | 无 PV 对照（同谱面，移走 `pv.mp4`，保留 `bg.png`） | 同 C0 | 是否仍有 `render_submit` 尖峰。仍有 ⇒ 与视频无关 |
| C2 | 软件解码对照 | 同 C0，界面切「软件渲染」（或 `MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO=1`） | 尖峰是否消失 ⇒ 是否与 D3D11VA 解码队列相关 |
| C3 | 诊断开销对照 | `call Start_MiaCode_Debug.bat`（不设 pacing 变量） | 复现概率是否变化；证明诊断没改变问题 |
| C4 | 渲染循环 A/B（仅诊断用，非修复） | `set MIACODE_PREVIEW_FORCE_BASIC_RENDER_LOOP=1&&…` | 卡顿形态是否改变 ⇒ 渲染线程相关 |
| C5 | 扩展干扰排除 | 临时禁用第三方扩展后重跑 C0 | 皮肤 / SFX / 资源生命周期的间接影响 |

每次都是：冷启动 → 打开同一带 PV 谱面 → 从 0 秒播放一次 → 退出。
每次都记录是否出现 `render_submit` 尖峰 **和** `eom_class=stale`。
**「没复现」不等于修好了**，必须留档。

假 EOM 另有一组独立对照（与上面正交）：同谱面换不同 PV（不同编码 / 分辨率 / 时长），
看 `demux_eof_avio` 与 `demux_last_avio_error` 是否随文件变化。若 `demux_last_avio_error != 0`，
下一步应查磁盘 / 杀软 / 文件占用，而不是继续查 GPU。

---

## 5. §6D —— 产品修复方向的现状

| 审计条目 | 状态 | 说明 |
|---|---|---|
| 6D-1 真正的空闲期 GPU 预热 | **部分**：已补齐前置事实（`video_prewarmed`、`layer_first_draw`）；合成体泛化按 §3.4 待做 | 需先有 `layer_first_draw` 名单，否则是盲造 |
| 6D-2 预热完成语义升级为「已呈现」 | **已完成** | §3.1 |
| 6D-3 解码 / 渲染队列协调 | **未做，且不应现在做** | 需要 ETW/GPUView 支撑。审计已排除桥接本身（最大 1.867 ms），不要仅凭 `two-device` 之名切单设备 |
| 6D-4 单独修 QtAV 假 EOM | **已完成** | §2；按事务 + 媒体时长 + 实际播放进度识别，真短 PV 的从属结束行为保留 |

---

## 6. 对照审计 §7 验收标准

| 验收项 | 现状 |
|---|---|
| 1. 不再出现 `render_submit_ms` / watchdog 百毫秒级尖峰 | **未达成**。本轮补的是可见性，不是根治。§5.1 的根因仍未定位 |
| 2. 不再出现假 EOM，真短 PV 仍不终止主播放 | **代码已就绪、规格已通过、现场未验证**。真短 PV 的从属行为由 `preview_end_of_media_policy_spec` 钉住 |
| 3. 无 PV / 普通 PV / 目标 PV / 软解 A/B 均有存档 | **未达成**，见 §4 待执行 |
| 4. 诊断改动、修复、版本号、文档以干净提交交付 | 本轮改动为干净工作区变更，待提交；版本号未动（beta.15 的版本提升不在本轮范围） |
| 5. 涉及设备拓扑 / render loop / 视频后端时补多厂商回归计划 | 本轮**未触碰**这三者，故不触发该项 |

---

## 7. 本轮改动清单

产品代码：

- `src/preview/quick_scene/PreviewQuickSceneRoot.{h,cpp}` —— render pass 相位拆分、`render_stall_context`、
  `layer_first_draw`、预热「已呈现」direct 钩子、相位连接重绑定泄漏修复
- `src/preview/quick_scene/PreviewTextureRepository.h` —— `PreviewTextureLayerStats::nodeProduced`
- `src/preview/runtime/PreviewRuntime.{h,cpp}` —— `notifyFireworkLayerPresentedNode()` 语义与注释
- `src/preview/runtime/PreviewStageMediaHost.h` —— 假 EOM 恢复状态
- `src/preview/runtime/PreviewStageMediaHost_Timeout.cpp` —— `handleVideoEndOfMedia` /
  `tryRecoverFromStaleEndOfMedia` / `resetStaleEndOfMediaRecovery` / demux 取证字段
  （放在既有的超时/恢复 TU，而不是继续把 `_Backend.cpp` 撑大）
- `src/preview/runtime/PreviewStageMediaHost_Backend.cpp` —— EOM 分派挂接 + `seeked` 恢复续播
- `src/preview/runtime/PreviewStageMediaHost_Media.cpp` —— 每次加载重置恢复预算
- `src/preview/runtime/PreviewStageMediaHost_Playback.cpp` —— `frames_before_commit` / `video_prewarmed`
- `src/preview/runtime/PreviewStageMediaHost_Diagnostics.cpp` —— `render_stall_vram`
- `src/app/gpu_adapter_probe.cpp` —— `umd_driver_version` / `feature_level`
- `src/core/video/PreviewEndOfMediaPolicy.h`（新）—— EOM 定性策略

vendored（仅观测）：

- `third_party/QtAVPlayer/src/QtAVPlayer/qavpreviewdemuxdiag_p.h`（新）
- `third_party/QtAVPlayer/src/QtAVPlayer/qavdemuxer.cpp` —— EOF 来源计数
- `third_party/QtAVPlayer/src/QtAVPlayer/QtAVPlayer.cmake` —— 登记新私有头

测试与文档：

- `src/tools/preview/PreviewEndOfMediaPolicySpec.cpp`（新）+ `CMakeLists.txt` 注册（CTest 通过）
- `docs/ops/DEBUG_INDEX.md`、三份 `miacode-dev-guide` 参考（debug/logging、build-and-tools、cross-chain-linkage）

验证边界：macOS Release 编译通过；`preview_end_of_media_policy_spec` 通过；
仓库既有 63 项 CTest 中 61 项通过，失败的 `plain_code_editor_spec`（macOS IMK mach port）与
`qtavplayer_platform_spec`（断言 Windows/D3D11）在**未改动的 `dev` 基线上同样失败**，与本轮改动无关。
Windows 现场行为、假 EOM 恢复的实际效果、以及 §5.1 的根因，**都还没有被验证**。
