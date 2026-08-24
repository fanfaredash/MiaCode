# 部分谱面预览自动暂停：初步诊断与交接

- 日期：2026-07-22
- 状态：初步根因已定位，尚未修改代码
- 用户现象：部分谱面开始播放后会稳定自动暂停，反复恢复也无法连续完整播放
- 材料来源：`C:\Users\admin\Desktop\maimai_workspace\打包\logs.zip`、`389.zip`、`639.zip`

## 结论摘要

当前证据已经形成完整因果链：问题谱面目录中存在一个时长仅约 **0.333 秒** 的 `pv.mp4`。MiaCode 将它优先识别为视频背景；视频后端每次播放到该文件末尾时发出 `EndOfMedia`，`PreviewStageMediaHost` 随即发出 `playbackFinished()`，主窗口又把该信号无条件解释为“整个谱面预览结束”，最终调用 `stopQtPreviewPlayback(true)`，连带暂停仍在正常播放的 BGM 和谱面时间轴。

这不是音频解码失败，也不是谱面时间轴自然结束。根因是“弱依赖的背景视频结束”错误地终止了“由音频/统一内容时长驱动的主预览 transport”。

## 已核实证据

### 1. 两次复现均解析到了 `pv.mp4`

日志记录的实际复现目录为：

- `D:/majdata2/fumens/639/maidata.txt`，解析媒体为 `D:/majdata2/fumens/639/pv.mp4`，`kind=video`
- `D:/majdata2/fumens/389/maidata.txt`，解析媒体为 `D:/majdata2/fumens/389/pv.mp4`，`kind=video`

对应日志位置：`miacode_audio_debug.log` 的 `action=set_chart_path`，主要在第 46/62/811 行（639）和第 826/841 行（389）。

注意：提供的 `639.zip` 中没有 `pv.mp4`，但复现日志明确表明测试时的原始 639 目录中存在该文件。因此，`639.zip` 与实际复现目录的媒体文件集合不完全一致；这不影响日志层面的因果判断，但后续若要做资产级复核，需要重新收集原始 `639/pv.mp4`。

### 2. 389 的 `pv.mp4` 是可解码但极短的视频

对 `389.zip/pv.mp4` 的探测结果：

- 编码：HEVC
- 分辨率：1080 × 1080
- 帧率：30 fps
- 帧数：10
- 时长：0.333333 秒
- 文件大小：121,638 字节

因此它不是普通的完整 PV，而是一个只有 10 帧的极短视频。目录中同时存在可用的 `bg.png`，但媒体解析顺序会优先选择视频，所以 `pv.mp4` 会遮蔽图片背景。

解析顺序由 `src/common/ChartAssetPaths.h` 的 `backgroundMediaCandidateFileNames()` 定义，目前为：

1. `bg.mp4`
2. `pv.mp4`
3. `bg.jpg`
4. `bg.png`
5. `bg.jpeg`

### 3. 每次自动暂停都紧跟视频 EOM，时间上完全对应

639 复现会话（日志 PID 14236）首次播放：

1. `13:13:59.557Z`：BGM 与视频从 0 秒开始，日志显示 `has_video=1`。
2. `13:13:59.570Z`：BGM 状态正常，`bgm_running=1`、`master_running=1`，音频长度约 113.208549 秒。
3. `13:13:59.869Z`：视频报告 `status=EndOfMedia position_ms=333`。
4. 同一毫秒：主预览执行 `action=stop ... pause_second=0.311328`。

之后用户每次恢复，均在约 0.27～0.32 秒后重复同一模式。该会话连续记录了 31 组 `retained_start -> EndOfMedia -> stop`，暂停位置从 0.311328 秒逐步推进到 8.743607 秒。

389 复现会话（日志 PID 23448）同样如此：首次在播放约 0.312582 秒后，由 `EndOfMedia position_ms=333` 触发 `stop`；后续恢复持续重复。

日志中的部分 `EndOfMedia` 发生时播放器状态仍显示 `PlayingState position_ms=0`，但同样立即触发主预览停止，说明当前处理只判断 EOM 枚举，不校验该事件是否应拥有主 transport 的结束权。

### 4. 音频和谱面时长并未到达结束点

提供的音频均可被 FFmpeg 完整解码，没有发现解码错误：

- 389 `track.mp3`：约 109.531429 秒（BASS/波形日志口径约 109.500000 秒）
- 639 `track.mp3`：约 113.240816 秒（BASS/波形日志口径约 113.208549 秒）

MiaCode 日志计算的统一预览时长也远大于 0.333 秒：

- 389：谱面约 108.000 秒，统一预览约 111.000 秒
- 639：谱面约 110.625 秒，统一预览约 113.625 秒

音频和运行日志中均没有 `WARN`、`ERROR` 或 `FATAL`。自动暂停发生时，主 BGM 仍处于运行状态。

## 代码层根因

问题由以下调用链构成：

1. `src/preview/runtime/PreviewStageMediaHost_Backend.cpp`
   - QtAVPlayer 分支收到 `QAVPlayer::EndOfMedia` 后，无条件 `emit playbackFinished()`。
   - QMediaPlayer 兼容分支收到 `QMediaPlayer::EndOfMedia` 后，也采用相同行为。
2. `src/app/mainwindow/sections/preview/MainWindow.PreviewStageMediaRoute.cpp`
   - `PreviewStageMediaHost::playbackFinished` 被无条件连接到：
     `finishQtPreviewPlaybackAndReturnToEntry("Qt preview reached the end of current media.")`。
3. `src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp`
   - `finishQtPreviewPlaybackAndReturnToEntry()` 调用 `stopQtPreviewPlayback(true)`，于是整个预览被暂停。

这与当前播放架构冲突：

- `MainWindow.TimelinePlayback.cpp` 已把视频启动明确记录为 `weak_video_prepare_started`，说明背景视频是弱依赖媒体。
- 主时钟由 `currentPreviewAuthoritativeAudioClockSecond()` 提供；有音频时由 BASS 主时钟驱动，无音频时可回退到墙钟。
- 正常结束条件已经由 `MainWindow.PreviewTick.cpp::onQtPreviewTickAtSecond()` 统一判断：当主播放时间到达 `previewPlaybackEndSeconds()` 时才结束。
- `previewPlaybackEndSeconds()` 使用 `max(chartEnd + tail, music)` 的统一内容时长策略，并不依赖背景视频时长。

因此，`PreviewStageMediaHost::playbackFinished` 对主 transport 的控制属于遗留耦合。它让一个纯视觉背景媒体错误地取得了整个预览的生命周期所有权。

## 临时规避方案

在应用修复发布前，可对受影响谱面采用以下规避方式：

1. 将异常短的 `pv.mp4` 移出谱面目录，或重命名为不会被解析器匹配的文件名，例如 `pv_bak.mp4`。
2. 保留 `bg.png`；MiaCode 在找不到 `bg.mp4`/`pv.mp4` 后会自动回退到图片背景。
3. 若该视频本应是真正的 PV，则替换为与歌曲/谱面长度相符的完整视频。

对 389，提供的压缩包已经包含 `bg.png`，移走 0.333 秒的 `pv.mp4` 即可绕过问题。对 639，需在实际复现目录中处理日志所指向的 `pv.mp4`；提供的 `639.zip` 本身不含该文件。

## 建议的产品代码修复

建议采用“解除生命周期耦合”的根治方案，而不是按视频长度做特例：

1. 背景视频 EOM 只更新 `PreviewStageMediaHost` 自身状态和诊断信息，不再发出能够终止主预览的信号。
2. 删除 `MainWindow.PreviewStageMediaRoute.cpp` 中 `playbackFinished -> finishQtPreviewPlaybackAndReturnToEntry` 的连接。
3. 最好同时删除或改名 `PreviewStageMediaHost::playbackFinished`，避免后续再次把视觉媒体 EOM 误接为主 transport EOM。若保留，应明确命名为 `videoPlaybackFinished`，且只允许视觉层消费。
4. 主预览唯一的自然结束入口继续保留在 `MainWindow.PreviewTick.cpp`，以 `previewPlaybackEndSeconds()` 为准。
5. 视频较短时的视觉策略可独立决定（保留最后一帧、隐藏视频并回退图片、或停止视频层）；无论选择哪种，都不得暂停音乐、SFX、谱面动画和时间轴。

不建议仅增加“视频时长小于 N 秒则忽略”的阈值。问题的本质不是 0.333 秒这个数值，而是背景视频无权结束主播放；任何比音乐或谱面更短的合法视频都可能触发同类问题。

## 建议回归验证

修复后至少覆盖：

1. 389 原始材料：0.333 秒 `pv.mp4` 到达 EOM 后，BGM、谱面和时间轴继续到统一内容结束点。
2. 639 原始复现目录：验证相同行为，并重新收集/核对实际 `pv.mp4`。
3. 视频短于音乐、视频长于音乐、无视频仅图片三种组合。
4. 无 BGM 但有谱面的情况：仍由墙钟/谱面统一时长结束，不能由视频结束。
5. 从大于视频时长的位置 seek/恢复播放：视频可以保持结束态，但主 transport 继续。
6. Windows QtAVPlayer 主路径与非 Windows QMediaPlayer 兼容路径保持一致。
7. 最终自然结束仍只发生一次，状态栏显示 timeline/content-duration 结束，而不是 media EOM 结束。

## 本次审查边界

- 本次只完成初步定位和方案交接，未修改产品代码、未构建、未运行自动化测试。
- `639.zip` 缺失复现日志中出现的 `pv.mp4`，因此没有对该文件本体做哈希或媒体探测。
- 尚未评估短视频结束后“保留最后一帧”与“回退静态背景”哪一种更符合产品体验；该选择不影响上述主 transport 解耦结论。
