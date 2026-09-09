# 预览音频错位审查（问题 3 / 问题 4）

- 日期：2026-08-04
- 基准：`dev` `677a9625`（`1.1.0-beta.7`）
- 关联文档：`WINDOWS_IDLE_FREEZE_AUDIT_REVIEW_ZH.md`、`OBS_CONTENTION_PLAYBACK_STUTTER_AUDIT_ZH.md`

> 状态更新（2026-08-24）：本文对 Linux miniaudio 路径的结论仅适用于审查基准
> `677a9625`。当前 Linux 构建已启用 BASS/BASSmix/BASS_FX，并通过 PipeWire 设备选择接入
> 最新的 `PreviewBassDeviceLease` 生命周期；第 0、2、6 节相关结论不再描述当前实现。

| | 问题 3 | 问题 4 |
|---|---|---|
| 现象 | 播放中切换音频设备 / 插入外设 / 切后台 / 笔记本进入节能 → 预览音频错位；**暂停再播放可恢复** | Linux：**从中间播放**预览有显著延迟；**导出正常** |
| 平台 | Windows + macOS（macOS 上随时可能出现） | Linux |
| 用户线索 | — | `&first = 1.333` |

---

## 0. 最重要的一条：这两个问题不在同一套代码里

**Linux 根本没有 BASS。**

`CMakeLists.txt:726` 是 `if (WIN32 OR APPLE)`，只有在这个分支里才 `target_compile_definitions(MiaCode PRIVATE MIACODE_HAS_BASS_AUDIO=1)`（`:738`）。而 `QtPreviewSfxRuntime::createBackend()`（`src/audio/QtPreviewSfxRuntime.cpp:50-63`）：

```cpp
#ifdef MIACODE_HAS_BASS_AUDIO
    auto bassBackend = std::make_unique<BassPreviewAudioBackend>();
    ...
    return bassBackend;
#endif
    return std::make_unique<MiniaudioPreviewAudioBackend>();
```

所以：

- **问题 3（Win/mac）走 `BassPreviewAudioBackend`** —— BASS + BASS_FX tempo + BASS mixer。
- **问题 4（Linux）走 `MiniaudioPreviewAudioBackend`** —— miniaudio + 自研时间拉伸数据源。

**两者的 seek、时钟、偏移应用是完全不同的实现。** 任何把问题 4 当作问题 3 的 Linux 变体来排查的做法都会走偏；反过来，问题 3 的任何修复都不会影响 Linux。这条必须先钉死，否则后面全是无效功。

> 顺带纠正一个我一开始就想错的方向：`&first = 1.333` 这种小数**不是 locale 问题**。三处 `first` 解析全部用 `QString::toDouble()`，它固定按 C locale 解析，与系统区域设置无关。**不要往 locale 方向排查。**

---

## 1. 问题 3：设备变更 → 错位（Windows / macOS）

### F3-1（根因级，已确认）：全仓没有任何音频设备变更处理

在整个 `src/` 下检索不到：

- `BASS_ERROR_DEVICE` —— 无
- `BASS_SetDevice` —— 无（一次都没有）
- 任何设备重建 / reinit 路径 —— 无
- `QMediaDevices` / `audioOutputsChanged` / `defaultAudioOutput` 监听 —— **全仓零命中**

`BASS_Free()` 只出现在两处，都是析构/关闭路径（`BassPreviewAudioBackend.cpp:61`、`BassPreviewAudioBackend_EngineInit.cpp:218`）。

也就是说：**用户切换输出设备、插入耳机/外接声卡、系统切到后台降级、笔记本进入节能改变设备状态时，应用完全不知情。** BASS 的设备在底层被换掉或其延迟/采样率发生变化，而应用侧的时钟锚点没有任何重新对齐。

这一条不是推断，是检索结果。它直接解释了问题 3 的触发条件列表——那四种情况的共同点正是**输出设备状态改变**。

### F3-2（已确认）：为什么"暂停再播放能恢复"

时钟是**锚点 + 推进**模型，不是纯粹跟随设备位置。`BassPreviewAudioBackend_PlaybackClock.cpp` 里有一整套：

- `anchorTransportToSecond(second, reason)` —— 把传输层锚到某个 chart 秒
- `startTransportFromCurrentAnchor()` —— 从当前锚点起播
- `RetainedSeekAction::{ResumeAnchored, AnchorPaused, AnchorAndResume}`（`:494-513`）

暂停会走 `apply_paused_state` → `anchorTransportToSecond`（`:405`），再播放走 `startTransportFromCurrentAnchor`（`:462`）。**一次暂停/播放就是一次强制重新锚定**，所以设备变更累积的偏差被抹掉了。

这同时是个好消息：**它说明错位是"锚点陈旧"而不是"数据损坏"**，修复方向是在设备变更时主动重锚，而不是重建整个音频栈。

### F3-3（已确认）：错位是**当前就能测**的，不需要新埋点

`logPlaybackStatus()`（`:129`）在 `--debug` 下已经输出 `bass_status` 行，其中直接带着三个错位量：

| 字段 | 含义 |
|---|---|
| `drift_ms` | `(authoritativeSecond − fallbackSecond) × 1000` |
| `bgm_delta_ms` | `(authoritativeSecond − bgmChartSecond) × 1000` —— **这就是音画错位量** |
| `bgm_raw_delta_ms` | BGM 实际 raw 位置与期望 raw 位置之差 |

还有 `bgm_offset`、`bgm_running`、`master_running`、`retained_mode`、`rate`、`speed_mode`。

**所以问题 3 的验证不需要写一行代码**：开 `--debug`，播放，然后切换音频设备，看 `bgm_delta_ms` 是否在切换瞬间跳变并保持。

### F3-4（假设）：macOS 上"随时可能出现"

macOS 上 CoreAudio 的设备状态变化远比 Windows 频繁且隐式：默认设备采样率切换、聚合设备、蓝牙编解码协商、App Nap / 电源状态下的 I/O 周期调整，都会改变有效延迟，而**不一定伴随一次用户可见的"设备切换"**。在 F3-1 这个"完全不监听"的前提下，表现就是"随时可能错位，原因不明"。

定性：与 F3-1 同源，只是触发频率更高。**未证**，需靠 F3-3 的日志把每次跳变与系统事件对齐来确认。

---

## 2. 问题 4：Linux 从中间播放有延迟（miniaudio 路径）

### F4-1（已确认）：BGM 走的是带内部缓冲的时间拉伸数据源

`MiniaudioPreviewAudioBackend.cpp` 里 BGM 经过 `StretchedBackgroundState` —— 一个自定义 `ma_data_source`，带自己的 `seekCallback`（`:224`）和输入/输出缓冲。

时间拉伸器**天然有启动延迟**：它必须先吃进若干输入帧才能吐出第一帧输出。seek 之后这段"预热"就是听感上的延迟。

### F4-2（已确认）：这段延迟**代码里已经在量了**

`StretchedBackgroundState` 已经埋了三条日志，且正好是为了测 seek 后的预热间隙：

| 日志 | 位置 | 含义 |
|---|---|---|
| `stretched_seek seek=N output_frame=… source_frame=… rate=…` | `:256` | 一次 seek，**输出帧与源帧分别是多少** |
| `stretched_read_first_input seek=N rendered_cursor_frame=… decoded_frames=… rate=…` | `:202` | seek 后**第一次喂入**输入 |
| `stretched_read_first_output seek=N rendered_cursor_frame=… frames_out=… available_before=… requested_frames=…` | `:157` | seek 后**第一次产出**输出 |

`loggedFirstPutAfterSeek` / `loggedFirstReceiveAfterSeek`（`:82-83`）保证每次 seek 只各记一次。

**`first_input` 与 `first_output` 之间的间隔，就是这次 seek 的预热延迟。** 和问题 3 一样，验证不需要新代码。

### F4-3（首要假设，未证）：偏移可能被应用在错误的帧域

`seekCallback` 同时打印 `output_frame` 和 `source_frame`（`:256`），说明这里存在**输出帧域 ↔ 源帧域**的换算，且换算依赖 `rate`。

`first` 偏移在 BASS 侧是这样应用的（`BassPreviewAudioBackend_Transport.cpp:483`）：

```cpp
const double rawSecond = second + playbackSession_.backgroundTrackOffsetSeconds;
```

即 `raw = chart + offset`，是**秒域**的加法。若 miniaudio 侧把等价偏移施加在**拉伸后的输出帧域**而非**源帧域**（或反之），误差就会随 `rate` 缩放，且在 `rate == 1.0` 时可能恰好不可见。`&first = 1.333` 提供了一个大到足以被听出来的常量偏移，这也解释了为什么用户能明确指认这个参数。

**为什么导出正常**：导出根本不走预览音频后端。导出的音频是独立复用的，`PreviewQuickExportSession` 只负责画面。所以"导出正常"**不能**用来证明 chart 侧时间轴没问题——它只证明预览音频路径与导出音频路径不一致。

> 需要说明：我没有逐行读完 `StretchedBackgroundState` 的帧域换算，**F4-3 是有结构性依据的假设，不是结论**。§4 的第一步就能证伪或坐实它。

---

## 3. 跨切面：`first` 至少有四份独立实现，且预览与导出读的不是同一个来源

这一条同时影响问题 3 和问题 4，也是仓库既有约定（预览↔导出为 sync pair）被打破的地方。

| 实现 | 位置 | 读取来源 |
|---|---|---|
| `parsedFirstSeconds(raw, ok)` | `src/tools/muri/MuriDump.cpp:54` | 传入值 |
| `parsedFirstSeconds(raw)` | `src/tools/video_export/VideoExportSnapshot.cpp:148` | `document.first` |
| `parsedDocumentFirstSeconds(raw, ok)` | `src/app/mainwindow/sections/export/MainWindow.ExportSnapshot.cpp:110` | `document.first` |
| `TimelineSection::parsedFirstSeconds(ok)` | `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp:546` | **实时 UI 字段文本** |

最后一条是关键：`MainWindow.FrameBootstrap.cpp:1747` 的注释明说 `parsedFirstSeconds() already reads the live field text`。

**预览链路读的是编辑框里的实时文本，导出链路读的是 `document.first`。** 两者在"用户改了 offset 但尚未提交到 document"的窗口期内会不一致。这不一定是问题 3/4 的直接成因，但它是同一片区域里一个确定的、独立的不一致点，且正是"预览与导出表现不同"这类报告的经典来源。

三份 `parsedFirstSeconds` 的解析语义本身是等价的（都是 `trimmed().toDouble()`，失败回退 0.0），差别只在 `ok` 出参。所以**问题不在解析，在来源和调用点**。

### 一个失效的面包屑

`BassPreviewAudioBackend_Transport.cpp:482` 的注释引用了 `PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §4.1`（记录了 0.5x 变速崩溃的场景 (b)）。**该文件在仓库中已不存在。** 这说明这块时钟对齐问题此前被系统研究过，但结论文档已经丢失。修复前值得先从 git 历史里把它找回来，避免重复踩坑。

---

## 4. 验证方案

核心原则：**两个问题的关键仪表都已经存在，第一轮不要写任何代码。**

### 阶段 A — 问题 3（Windows / macOS），不改代码

启动加 `--debug`，日志看 `miacode_audio_debug.log`（注意：预览音频走 **Audio 通道**，不是 runtime 日志）。

1. 播放一段有 BGM 的谱面，静置 30 s 建立基线，记录 `bgm_delta_ms` 的正常波动范围。
2. 依次触发，每次之间留 20 s：
   - 系统设置里切换默认输出设备
   - 插入 / 拔出耳机或外接声卡
   - 切到后台（另一个窗口置前）
   - 拔掉电源让笔记本进入节能
3. 每次触发后立刻看 `bgm_delta_ms`：
   - **在触发瞬间跳变并保持在新值** → 坐实 F3-1（锚点陈旧）
   - 跳变后自行收敛回 0 → 另有自愈机制，需重新分析
   - 无跳变但用户仍听到错位 → 错位在 BASS 之后（设备侧缓冲），F3-1 不成立
4. 然后暂停 → 播放，确认 `bgm_delta_ms` 回到基线（应与用户"暂停可恢复"的描述一致）。

macOS 追加：长时间播放不做任何操作，观察 `bgm_delta_ms` 是否自发跳变；若是，把跳变时刻与 `system.log` / 电源事件对齐。

### 阶段 B — 问题 4（Linux），不改代码

1. 用 issue 里那个 `&first = 1.333` 的例子。
2. 从头播放，记录是否正常。
3. **从中间 seek 后播放**，在 `miacode_audio_debug.log` 里抓这一组：

```
stretched_seek seek=N output_frame=… source_frame=… rate=…
stretched_read_first_input  seek=N …
stretched_read_first_output seek=N …
```

4. 判别：
   - `output_frame` 与 `source_frame` 的差**换算成秒后接近 1.333（或其倍数 / 与 rate 相乘）** → 坐实 F4-3，偏移施加在错误帧域
   - 两者一致，但 `first_input` → `first_output` 的间隔就是听到的延迟 → 是拉伸器预热延迟，属另一类问题（应在 seek 时预填充）
   - 两者都正常 → 延迟在 miniaudio 设备层，需另查
5. 对照组：把 `&first` 改成 `0`，重复第 3 步。**若延迟随之消失，F4-3 基本坐实。** 这一步成本极低，应优先做。
6. 再对照：以 `rate != 1.0`（如 0.75x）重复，看误差是否随 rate 缩放——这是帧域错误的特征签名。

### 阶段 C — 仅在 A/B 无法判别时才补埋点

若阶段 B 第 4 步落在"两者都正常"，才需要在 miniaudio 设备回调层补一条时间戳日志。**不要在阶段 A/B 之前预先补埋点**——现有三条 `stretched_*` 日志是专门为这个场景埋的，先用它们。

---

## 5. 修复方向（结论确认后再动手）

- **F3-1**：在设备变更时主动重锚，而不是重建音频栈。既然暂停/播放已经能恢复，最小修复就是**监听设备变更并触发一次等价于 pause→resume 的重锚**。Windows 可用 `IMMNotificationClient` 或 Qt 的 `QMediaDevices::audioOutputsChanged`；macOS 用 CoreAudio 属性监听。注意这会是全仓第一个设备变更监听点。
- **F4-3**：把偏移统一到**源帧域**，并为 `rate != 1.0` 补一个断言或单测。
- **§3 的 `first` 多来源**：让预览与导出读同一个来源。这一条独立于问题 3/4 都该做，属于 sync-pair 契约。
- **失效面包屑**：从 git 历史恢复 `PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md`，或删除那条引用。

---

## 6. 审查边界

本文是静态代码调研，**未包含任何实机复现、Linux 构建或音频测量**。

请区分：

- **已确认的事实**：Linux 无 BASS 且走 miniaudio（`CMakeLists.txt:726/738` + `QtPreviewSfxRuntime.cpp:50-63`）；全仓无任何设备变更处理；锚点模型的存在与暂停重锚路径；`bass_status` 与 `stretched_*` 三条日志已存在且字段可用；`first` 有四份实现且预览读实时 UI 文本、导出读 `document.first`；三份解析均为 locale 无关。
- **有依据的假设**：F4-3（偏移帧域错误）、F3-4（macOS 高频触发同源）。
- **已排除**：locale 相关的小数解析问题；以及"问题 4 是问题 3 的 Linux 版本"这一等同关系。

阶段 A 第 3 步和阶段 B 第 5 步都是单步可证伪的，应优先执行。
