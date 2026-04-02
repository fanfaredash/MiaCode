# Preview Runtime Workflow

## 1. 文档目的

本文描述当前预览运行时链路的真实工作方式，目标是帮助维护者理解：

- 预览相关状态由谁持有
- 编辑、解析、预览、验证、Muri 之间如何衔接
- 播放、暂停、拖动、继续播放时，哪些步骤必须强同步
- 当前实现为什么这样分层，以及哪些地方是刻意保守的

本文关注“系统怎么工作”，不强调具体代码定位。

## 2. 设计原则

当前预览链遵循三条优先级：

1. 预览实时性和同步性最高。
2. timeline 可视更新次之，可以略微落后。
3. validation 和 Muri 最低，播放期间甚至可以不更新可见 UI。

对应的核心目标是：

- 多种设备下保持鲁棒性
- 主窗口启动低延迟
- 播放、暂停、seek、resume 低延迟

这直接决定了当前实现不会为了“所有信息都实时”而牺牲预览主链，也不会轻易把 SFX 运行时拆到独立 owner 线程里引入起播抖动。

## 3. 核心组件分工

### 3.1 MainWindow

`MainWindow` 是预览系统的总编排者。它负责：

- 管理当前文档、活动难度、预览播放状态、暂停位置和播放倍率
- 驱动 timeline 快路径与慢路径刷新
- 决定何时触发 warmup、何时允许播放、何时回到 paused preview
- 把预览状态同步给 `PreviewCanvas`、`PreviewMediaController` 和 `QtPreviewSfxRuntime`

可以把它理解成预览运行时的状态协调器，而不是单纯的 UI 外壳。

### 3.2 TimelineQuickModel

`TimelineQuickModel` 是编辑态的轻量时间轴模型。它负责：

- 响应编辑器输入后的快速时间轴更新
- 支撑光标定位、follow preview、timeline 跳转等交互
- 在慢速完整解析尚未返回前，维持一个尽量及时的 timeline 视图

它不是播放时的最终数据源，只是编辑态的快速反馈层。

### 3.3 Slow Refresh / Analysis Pipeline

慢刷新链负责完整解析和预览快照发布，分析链负责 validation 与 Muri。

当前已经明确拆成两层：

- `slow refresh`
  - 做一次完整 parse
  - 生成带 `&first` 偏移的 preview snapshot
  - 尽快发布给 paused preview 或待启动播放请求
- `analysis`
  - 复用同一次 parse 的结果和同一份 note marker snapshot
  - 统一构建 validation 报告和 Muri 结果
  - 作为 latest-only、idle-first 的低优先级后台链

这是当前“预览主链优先、分析次之”的关键结构。

### 3.4 PreviewCanvas

`PreviewCanvas` 负责画面渲染，但不决定“应该播放什么”。它消费的是：

- 当前 note marker snapshot
- 当前播放秒数
- 当前媒体帧
- 当前 Muri 渲染选项与分析结果

也就是说，`PreviewCanvas` 是渲染端，不是调度端。

### 3.5 PreviewMediaController

`PreviewMediaController` 管背景媒体和媒体侧背景音轨，包括：

- 背景图片或视频
- 视频播放位置
- 媒体侧背景音轨
- 与 `PreviewCanvas` 的媒体帧同步

当前正式实例固定运行在专用 `QThread` 上。`MainWindow` 通过代理调用与它交互，避免把 Qt Multimedia 设备对象直接混在 UI 线程和线程池 warmup 中。

### 3.6 QtPreviewSfxRuntime

`QtPreviewSfxRuntime` 负责预览期的音频时序与事件驱动，包括：

- SFX 资源加载
- 背景轨播放
- 事件时间线推进与 `drainEvents`
- touchhold 持续音生命周期
- 试听

当前正式实例仍由主线程持有，不做独立 owner 线程迁移。原因很简单：预览物件渲染和 SFX 起播要求尽量从同一条同步链上同时起跑，过早线程化会放大起播抖动风险。

## 4. 三层状态模型

理解当前预览链，最重要的是区分三类状态。

### 4.1 编辑态

编辑态对应用户当前正在修改的文本，以及 `TimelineQuickModel` 提供的快速 timeline 视图。

这层状态变化最快，但不是播放时直接使用的正式输入。

### 4.2 Preview Snapshot

preview snapshot 是慢刷新产物。它包含：

- 当前 revision 对应的 note markers
- 对应签名
- 解析后的稳定预览输入

播放只能基于某一份已经落地的 snapshot 启动。也就是说，播放不是“永远追着最新编辑态跑”，而是“基于某个稳定快照播放”。

### 4.3 Playback Session

playback session 是一次正在运行的播放会话状态。它包含：

- 当前起播秒数
- 当前暂停秒数
- 当前实际播放时钟
- 当前 play-start snapshot

播放一旦开始，预览音频、画面和物件统计都冻结在本次 play-start snapshot 上，直到播放停止。

## 5. 启动与 Warmup

### 5.1 Warmup 的目标

当前 warmup 的目标是“降低首次 prepare 和首次媒体接入的延迟”，不是“在后台抢先构造真实 runtime”。

### 5.2 当前 Warmup 是 data-only

现在的 warmup 已明确收缩为数据预热：

- media warmup
  - 解析背景媒体路径
  - 对背景媒体文件做 OS cache 预热
- SFX warmup
  - 解析 SFX 目录
  - 预热常用音效文件和背景轨文件

它不再做这些事情：

- 不在线程池里临时构造 `QMediaPlayer`
- 不在线程池里临时构造 `QVideoSink` / `QAudioOutput`
- 不在线程池里临时初始化 `ma_engine`
- 不构造一次性 sacrificial runtime

### 5.3 Warmup 结果如何应用

warmup 通过 generation 驱动，只允许最新结果生效：

- media warmup 结果会回填到 `MainWindow`，然后下发给正式 `PreviewMediaController`
- SFX warmup 结果会回填解析出的 `chartPath / trackPath / sfxDir`，供正式 `QtPreviewSfxRuntime` 后续按需 prepare 时使用

这种设计避免了旧 chart 或旧路径的 warmup 结果晚到后覆盖当前状态。

## 6. 编辑到预览的数据流

### 6.1 快路径

用户编辑文本时，`TimelineQuickModel` 会先更新：

- timeline 视图立即刷新
- 光标映射、跳转和 follow 维持可用
- slider 范围和基础时长估计同步更新

这条链的目标是交互流畅，而不是提供正式播放输入。

### 6.2 慢路径

每次 schedule timeline refresh 后，慢路径会做：

1. 读取当前 chart 文本和 `&first`
2. 执行完整 parse
3. 构建带偏移的 preview snapshot
4. 生成最新 note marker signature
5. 发布为当前 latest preview snapshot

如果当前没有在播放，会立即把这份 snapshot 应用回 paused preview。

### 6.3 分析路径

slow refresh 完成后，不再分别派发 validation worker 和 Muri worker，而是统一调度一个 analysis request：

- 输入
  - 同一次 parseResult
  - 同一份 shifted note markers
  - 当前 Muri render options
  - 当前 static tap-on-slide threshold
- 输出
  - validation report
  - Muri analysis report
  - Muri static references

这是当前“单次 parse，多路消费”的正式形态。

## 7. Analysis 的 latest-only 与 idle-first 语义

### 7.1 Latest-only

analysis 链明确只关心最新请求：

- 新请求到来时，可以覆盖旧 pending request
- worker 返回时，如果已经不是最新 revision，就直接丢弃

它的目标不是逐条处理所有中间态，而是尽快收敛到最新稳定状态。

### 7.2 Idle-first

analysis 调度还带一个短延迟 idle timer。目前它的作用是：

- 连续输入期间不急着频繁做 validation/Muri
- 让 rapid edits 先合并
- 把 CPU 时间优先留给编辑态和预览主链

### 7.3 播放期间的可见 UI 延后

播放期间允许 analysis 结果继续在后台更新缓存，但可见 UI 应用可以延后：

- validation 列表和装饰不必立即刷新
- Muri 面板不必立即刷新
- `PreviewCanvas` 上的 Muri 分析显示也可以等到暂停态再同步

停播后会立即：

1. 先应用 deferred analysis UI
2. 再补发仍然挂起的 analysis request

因此 paused 状态下工具信息会很快追上，而 active playback 不会被低优先级分析干扰。

### 7.4 纯分析参数变化的优化

有些变化并不需要重新 parse，例如：

- 切换 Muri render mode
- 修改 static tap-on-slide threshold

这些路径现在会优先复用“最近一次 parseResult + 最近一次 preview snapshot”，直接重跑 analysis，而不是强制触发整次 slow refresh。

## 8. 播放前准备

开始播放前，系统会先经过一个前置门槛：

1. 如果当前字段有脏状态，先尽量同步到文档
2. 确认存在活动难度
3. 确认当前 revision 的 preview snapshot 已就绪

如果 snapshot 尚未就绪，不会硬启动播放，而是：

- 请求 slow refresh
- 记录 pending playback 请求
- 等匹配 revision 的 snapshot 落地后自动起播

这保证了播放的输入总是来自明确的一份 preview snapshot。

## 9. 播放启动事务

### 9.1 先准备 runtime

播放启动会先确保：

- `PreviewMediaController` 已创建并初始化后端对象
- `QtPreviewSfxRuntime` 已完成 on-demand prepare

### 9.2 先回到一个干净的 paused preview

正式起播前，会先应用一次 paused preview 状态，目的是：

- 让 timeline program 与最新 snapshot 对齐
- 把 cursor 重置到起播附近
- 确保 touchhold 保持静音起点

### 9.3 SFX 事务返回真实起播秒数

`QtPreviewSfxRuntime` 会通过 `startPreviewPlaybackTransaction(...)` 一次性完成：

- 同步背景轨倍率
- 启动背景轨
- 回读背景轨当前真实秒数
- 重置事件 cursor
- 非 resume 路径补首拍 `drain`
- 恢复 touchhold 持续音

这一步会返回 `effectiveStartSecond`。主窗口后续会用这个秒数作为本次播放的正式起点。

这是当前保证“物件渲染与 SFX 尽量同时起跑”的关键同步点。

### 9.4 媒体侧同步

如果当前有视频媒体，则会把相同的 `effectiveStartSecond` 下发给 `PreviewMediaController` 开始播放。

最终：

- timeline playhead
- preview canvas
- slider
- follow preview
- 媒体播放

都会围绕同一个 `effectiveStartSecond` 进入运行态。

## 10. 播放中的时钟推进

### 10.1 canonical second

播放中，主窗口每个 tick 都要先决定当前的 canonical second。

当前规则是：

- 如果 SFX 背景轨存在且正在运行，优先以它的播放秒数为准
- 否则退回本地 elapsed timer 根据倍率推算的秒数

这让当前播放时钟更贴近真实音频时钟，而不是单纯依赖 UI 本地计时。

### 10.2 Tick 做什么

每个 tick 主要做四件事：

1. 计算当前播放秒数
2. 把该秒数同步给 `PreviewMediaController`
3. 更新 `PreviewCanvas`、timeline、slider、editor follow
4. 调用 `QtPreviewSfxRuntime::drainEvents(second)`

如果已到末尾，则会补最后一次位置更新并结束播放。

### 10.3 Timeline 单独刷新

timeline 位置刷新和播放 tick 分离：

- 播放 tick 负责运行时推进
- timeline timer 负责按较稳定节奏刷新 playhead

这样能避免 timeline 绘制和音频事件推进完全绑死在一条更新链上。

## 11. 暂停、停止与回到 Paused Preview

停播时系统不会只改一个布尔值，而是显式回到 paused preview。

流程是：

1. 优先从 `QtPreviewSfxRuntime` 捕获 pause second
2. 若 SFX 背景轨不可用，则退回媒体当前秒数
3. 暂停媒体播放
4. 停掉 tick 和 timeline timer
5. 停止所有 SFX
6. 重新应用 paused preview 状态
7. 刷新 deferred analysis UI
8. 如有挂起的 analysis request，立即触发补跑

paused preview 的语义是：

- 画面停在当前秒数
- timeline 对齐当前停点
- touchhold 必须静音
- 播放态 snapshot 与 paused 视图重新收敛

## 12. Seek 与拖动

seek 的目标是“立刻把预览定位到某个秒数”，而不是后台慢慢追上。

当前做法是：

- 如果正在播放，先停播并保留位置
- 更新 pause second / start second / timeline pending second
- 同步 paused 媒体时间戳
- 更新 canvas、timeline、slider 和统计

slider 拖动本身带 debounce，避免连续 seek 过于频繁，但最终落点仍然是强同步应用到 paused preview。

## 13. QtPreviewSfxRuntime 的内部职责

当前 `QtPreviewSfxRuntime` 内部已经不再是完全扁平的大一统状态，而是分成几块概念：

- warmup 路径信息
- prepared assets
- prepared timeline program
- playback session

其中：

- prepared 部分更偏“可复用运行前状态”
- session 部分更偏“当前这轮播放的动态状态”

它目前最关键的几个事务入口是：

- `applyPausedPreviewState`
- `startPreviewPlaybackTransaction`
- `capturePausedPreviewTransaction`
- `syncPreviewPlaybackClockTransaction`

这几组接口的意义不是“让 API 看起来整洁”，而是把强同步语义收口，避免主窗口层面手工拼装过细步骤。

## 14. PreviewMediaController 的当前语义

`PreviewMediaController` 目前的边界比较清晰：

- 它是正式 media runtime 的 owner
- 它只在专用线程上创建真实 Qt Multimedia 后端对象
- 主窗口通过 queued / blocking queued 代理与它通信

它负责的主要事情包括：

- 解析并加载背景图片或视频
- 推送媒体帧给 `PreviewCanvas`
- 管媒体侧背景轨
- 在播放态与 paused 态之间同步媒体时间戳

这个设计主要服务于跨设备鲁棒性，而不是单纯为了“代码更优雅”。

## 15. 当前最重要的运行时契约

维护预览链时，最应该牢记的是这些契约：

- 播放必须基于已经落地的 preview snapshot 启动
- 播放期间预览内容冻结在 play-start snapshot 上
- validation / Muri 的优先级低于播放本身
- paused preview 必须保持 touchhold 静音
- 起播时 SFX 和物件渲染应尽量从同一条同步链同时起跑
- warmup 只能做 data warmup，不应再偷偷构造真实 live runtime

这些约束共同构成了当前预览 runtime 的稳定性边界。

## 16. 为什么现在不把 SFX 拆到独立线程

从结构上看，把 `QtPreviewSfxRuntime` 迁到固定 owner 线程似乎很自然，但当前仍然没有这样做，原因并不只是“工作量大”，而是语义风险很高：

- 播放启动要求画面与 SFX 尽量同时进入运行态
- pause / resume / seek / audition 都有强同步语义
- touchhold 恢复和首拍 drain 对时序非常敏感

如果把它改成普通异步队列，很容易引入：

- 起播抖动
- pause 后残响
- audition 听到旧参数
- timeline 时钟和音频时钟轻微漂移

因此当前策略是：先通过事务化入口整理语义，再决定是否需要进一步线程化，而不是为了线程化而线程化。

## 17. 对“流式传输谱面数据”的看法

未来如果继续追求更低的编辑尾延迟，`流式传输谱面数据` 是一个值得认真考虑的方向，尤其是在这些场景下会显得有必要：

- 谱面规模继续增大，完整 slow refresh 成本上升
- 希望在长时间连续编辑时进一步降低 parse 和 analysis 尾延迟
- 希望把更多低优先级分析做成增量式后台处理

但它的难点也非常明确：

- 预览播放依赖“冻结的 play-start snapshot”，而流式数据天然偏向持续变动，两者目标相反
- parser、slide/wifi 关系、touchhold span、`&first` 偏移、Muri 静态参考等逻辑并不天然适合简单按块切分
- validation 和 Muri 比 preview 更适合流式化，但它们仍然依赖稳定、可复现的 marker 语义
- export、preview runtime、analysis 三条链目前共享大量 parser 语义，流式化后很容易出现“增量链”和“完整链”结果漂移

所以，如果以后真的要做这件事，更合理的路线通常不是“把当前播放核心直接改成流式”，而是：

1. 先让 analysis 链更增量化、更流式化。
2. 继续保留播放链基于封口 snapshot 的强同步模型。
3. 在两者之间建立明确的 snapshot sealing 边界。

换句话说，流式传输谱面数据有必要，但它首先更适合改造低优先级分析链，而不是直接冲击当前预览 runtime 的核心同步语义。
