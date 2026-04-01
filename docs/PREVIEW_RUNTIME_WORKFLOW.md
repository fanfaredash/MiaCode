# 预览运行时工作流说明

## 1. 目的与范围

本文面向维护者说明当前预览子系统的运行方式，重点覆盖：

- `MainWindow` 如何编排预览相关状态
- 时间轴快慢两条刷新链如何向预览提供数据
- `PreviewCanvas`、`PreviewMediaController`、`QtPreviewSfxRuntime` 各自承担的职责
- 启动预热、播放、暂停、拖动、试音等主要交互的工作流
- 当前设计中的强同步语义

本文不强调具体代码位置，重点是帮助开发者理解“现在系统是怎么工作的”。

## 2. 组件分工总览

当前预览链可以粗略分成五层：

1. `MainWindow`
   - 整个预览系统的编排者。
   - 持有预览播放状态、暂停位置、待启动请求、时间轴快照状态、音频设置、播放倍率等顶层状态。
   - 决定什么时候启动预热、什么时候允许播放、什么时候把 paused 状态应用回预览。

2. Timeline 快/慢刷新链
   - 快链负责编辑器输入后的轻量刷新，让时间轴和光标联动尽快更新。
   - 慢链负责完整解析、构建 preview snapshot、校验和 Muri 输入。
   - 预览播放真正依赖的是慢链生成的 preview snapshot，而不是单纯依赖当前文本框内容。

3. `PreviewCanvas`
   - 负责画面渲染。
   - 它只消费已经准备好的预览数据和当前播放秒数，不负责决定“应该播放什么”。
   - 画面中的 note、背景、HUD、统计信息，都是由外部状态驱动的。

4. `PreviewMediaController`
   - 负责背景媒体，包括视频帧和媒体侧背景音轨。
   - 当前正式 runtime 固定运行在专用线程，由 `MainWindow` 通过代理下发调用。
   - 它更偏向“媒体设备与解码后端”。

5. `QtPreviewSfxRuntime`
   - 负责实时 SFX、touchhold 持续音、背景音轨播放状态与 timeline 事件 drain。
   - 当前正式实例仍由主线程持有和直接调用。
   - 它更偏向“游戏音频时间线状态机”。

## 3. 状态所有权与线程归属

### 3.1 MainWindow

`MainWindow` 是整个预览链的状态中心。它维护的核心状态包括：

- 当前预览是否在播放
- 当前暂停秒数、启动秒数、返回秒数、结束秒数
- 当前 preview snapshot 是否已经准备好
- 待自动启动的播放请求
- 当前音频设置、播放倍率、轨道时长
- warmup generation 与 warmup 结果缓存

这些状态决定了预览子系统的整体行为。其他组件更多是在执行具体子任务。

### 3.2 PreviewMediaController

`PreviewMediaController` 的正式实例运行在专用线程中。

当前设计思路是：

- 主窗口只保留指针和代理入口，不直接跨线程访问内部设备对象。
- 初始化、媒体路径应用、播放同步等操作都投递到同一个 owner 线程。
- 需要即时读回结果时，主窗口通过阻塞式查询拿到“当前是否有视频媒体”“当前媒体播放秒数”等状态。

这个设计已经把媒体后端从 UI 线程和线程池预热里剥离出来，减少了混合设备和多媒体后端初始化的随机性。

### 3.3 QtPreviewSfxRuntime

`QtPreviewSfxRuntime` 当前仍由主线程直接持有和调用。

它的状态同时包含：

- miniaudio engine
- 各类 SFX bank
- touchhold 声音占用与活动 span
- 背景音轨运行状态
- 当前 timeline 事件列表
- 当前事件游标

它现在既是“音频设备封装”，又是“预览音频逻辑状态机”。这也是它后续若要线程化，必须谨慎处理同步语义的原因。

### 3.4 Warmup Pool

主窗口还维护一个独立的 `previewWarmupPool_`，用于异步预热 preview 子系统。

当前 warmup 已经不再初始化真实 runtime，而是只做可转移的数据预热，例如：

- 背景媒体路径解析
- SFX 目录解析
- 资源文件和轨道文件的 OS 缓存预热

这条链通过 generation 保证只应用最新结果，避免旧谱面或旧路径的预热结果回填覆盖当前状态。

## 4. 数据来源与快照模型

### 4.1 编辑态与播放态不是同一个数据源

系统同时存在两种“谱面相关状态”：

- 编辑态状态：文本框中的最新内容，以及快链更新出的时间轴轻量数据
- 播放态状态：慢链生成的 preview snapshot

当前设计明确要求：

- 编辑中可以继续刷新时间轴、校验和 Muri 输入
- 一旦开始播放，预览音频、预览画面和物件统计会冻结在 play-start snapshot 上
- 播放中后续文本修改不会直接改写正在播放的内容

换句话说，预览播放不是“永远跟着最新文本跑”，而是“开始时取一份最新可用快照，然后在本次播放期间保持稳定”。

### 4.2 Preview Snapshot 的作用

preview snapshot 的核心作用是给播放链提供稳定输入，包括：

- note marker 集合
- 对应签名
- 与当前难度、当前 revision 对齐的播放数据

`MainWindow` 只有在确认当前 revision 的 preview snapshot 已经就绪后，才允许真正进入播放流程。

如果用户先按了播放，而 snapshot 还没准备好，系统不会立刻失败，而是把请求挂起，等待对应 revision 的 snapshot 落地后自动启动。

## 5. 启动与初始化工作流

应用启动时，预览链的大致流程如下：

1. `MainWindow` 初始化预览相关 UI、计时器和状态。
2. 创建独立的 preview warmup 线程池。
3. 创建 `PreviewCanvas`。
4. 创建 `QtPreviewSfxRuntime`。
5. 首次把当前 chart path、播放倍率和画面设置同步到预览组件。
6. 调度 preview subsystem warmup。

这里需要特别注意：

- media 正式 runtime 并不会在 warmup worker 中构造。
- SFX 正式 runtime 也不会在 warmup worker 中构造。
- warmup 只是提前解析路径、摸热文件缓存，让真正的首次 prepare 更顺滑。

## 6. 路径切换与谱面切换工作流

当当前谱面路径或活动难度发生变化时，`MainWindow` 会做几类同步动作：

- 把新的 chart path 下发给 media 与 SFX 侧
- 重新应用音频设置
- 重新构建波形缓存
- 重新触发 timeline 刷新
- 如果 preview warmup 已经启用过，则重新发起新 generation 的 warmup

这一阶段的目标不是“立刻开始播放”，而是让后续 seek、试音、播放使用的都是和当前谱面对齐的资源解析结果。

## 7. Warmup 工作流

### 7.1 Warmup 的目标

当前 warmup 的目标是缩短首次真实 prepare 的阻塞时间，而不是直接构造并持有真正的 runtime。

它被拆成两条独立子链：

- media warmup
- SFX warmup

两者共享同一个 generation，但彼此独立应用结果。

### 7.2 Media Warmup

media warmup 负责：

- 解析背景媒体路径
- 对视频或图片等媒体文件做轻量缓存预热
- 把解析结果回填到主窗口缓存
- 把 warmup-resolved media path 下发给正式 `PreviewMediaController`

如果此时正式 media controller 还没创建，主窗口会先确保它初始化，然后再把 warmup 结果应用过去。

### 7.3 SFX Warmup

SFX warmup 负责：

- 解析 SFX 目录
- 对常用 SFX 文件做缓存预热
- 对背景音轨文件做轻量缓存预热
- 把 chart path / track path / sfxDir 作为 warmup result 回填给主窗口
- 如果 `QtPreviewSfxRuntime` 已存在，则把这组 warmup-resolved path 记进去

需要强调的是：

- 当前 SFX warmup 还不会初始化 miniaudio engine
- 也不会创建正式 SFX bank
- 真正的 prepare 仍在首次需要时由主线程执行

### 7.4 Generation 机制

warmup 每次调度都会递增 generation。

只有 generation 与当前主窗口一致的结果，才允许被应用。这样可以避免以下问题：

- 用户切了新谱面，但旧谱面的 warmup 较晚返回
- 用户切了新路径，但旧路径的解析结果覆盖了新状态
- 路径在 warmup 进行中被再次修改，导致结果回填顺序错乱

## 8. SFX Runtime 的 prepare 工作流

虽然 SFX warmup 会提前提供路径结果，但正式 `QtPreviewSfxRuntime` 仍采用按需 prepare。

当前 prepare 过程大致是：

1. 把 warmup-resolved path 写回 runtime
2. 调用 `reloadAssets`
3. 初始化音频 engine 和各类 SFX bank
4. 初始化背景音轨或变速背景轨道
5. 重新同步当前 chart path
6. 同步当前播放倍率
7. 标记 runtime 已准备完成

因此，当前系统里的“异步 warmup”和“正式 prepare”并不是同一件事：

- warmup 解决的是路径解析和文件缓存问题
- prepare 解决的是正式 runtime 可用性问题

## 9. 暂停态工作流

暂停态是当前预览设计里一个非常重要的稳定状态。

当系统处于 paused preview 时，`MainWindow` 会把 paused 状态显式应用回预览系统。这个过程至少包含三件事：

1. 如果 note marker 已变化，则把最新 preview snapshot 重新配置到 SFX timeline
2. 把 SFX 事件游标重置到当前暂停秒数
3. 强制暂停所有 touchhold 持续音

这意味着 paused preview 的语义不是“只是停表”，而是：

- 预览画面停在当前秒数
- 时间轴与统计面板和当前 paused 秒数对齐
- touchhold 持续音必须保持静音

当前设计要求只有播放开始或继续播放时，才允许重新恢复 touchhold 声音。

## 10. 播放启动工作流

播放启动是预览链里最关键的事务之一。

### 10.1 启动前门槛

开始播放前，系统会先检查：

- 当前 chart field 是否需要先写回文档
- 当前是否存在活动难度
- 当前 revision 的 preview snapshot 是否已准备好

如果 snapshot 尚未准备好：

- 主窗口会先触发 slow refresh
- 把本次播放请求挂到 pending 状态
- 等 snapshot 落地后自动重试启动

### 10.2 启动事务

当启动条件满足后，主窗口会依次完成：

1. 确保 media runtime 已初始化
2. 确保 SFX runtime 已 prepare
3. 把 paused preview 状态应用一遍，保证 timeline 和 touchhold 状态干净
4. 计算本次理论起播秒数
5. 把播放倍率和背景音量同步给 media 与 SFX
6. 启动 SFX 背景音轨
7. 立即回读 SFX 的实际播放秒数，得到 `effectiveStartSecond`
8. 以这个实际起播秒数重置 SFX 事件游标
9. 若不是 resume，则补一次首拍 drain
10. 恢复当前秒数处应当持续响起的 touchhold 声音
11. 用最终起播秒数设置主窗口时钟、时间轴、画布和 slider
12. 如有视频媒体，通知 media controller 从同一秒数开始播放
13. 标记 `qtPreviewPlaying_ = true` 并启动 tick/timeline timer

这里的核心思想是：

- 视觉时钟和音频时钟应尽量从同一个实际秒数起步
- SFX 的实际起播秒数优先于理论输入秒数
- 首拍事件和 touchhold 恢复必须在正式进入播放态前完成

## 11. 播放中 Tick 工作流

播放中的每个 tick，主窗口会做以下事情：

1. 先决定“当前 canonical second 应该是多少”
   - 如果 SFX 背景音轨存在且正在运行，则优先使用其当前播放秒数
   - 否则退回到主窗口本地计时器按倍率推算的秒数
2. 把这个秒数同步给 media controller
3. 让 SFX runtime 根据当前秒数补做背景轨同步
4. 检查是否到达播放终点
5. 更新 `PreviewCanvas`、时间轴、统计、slider 和编辑器跟随
6. 调用 `drainEvents(second)`，触发所有已经到时的音效事件

其中 `drainEvents` 会完成：

- 同秒事件分组
- 可聚合音效的聚合播放
- `touchhold_start` / `touchhold_stop` 这类内部控制事件的处理
- 普通 SFX 的实际发声

当前语义下，tick 并不是单纯刷新 UI，而是驱动实时音频时间线向前推进的重要环节。

## 12. 暂停与停止工作流

停止或暂停播放时，主窗口会做如下处理：

1. 如果 SFX 背景音轨存在，优先从 SFX runtime 读取当前秒数作为 pause second
2. 暂停 SFX 背景音轨
3. 如果有媒体侧播放，通知 media controller 暂停
4. 停掉 preview tick timer 和 timeline timer
5. 更新主窗口内的播放状态标记
6. 停止所有 SFX 与 touchhold 声音
7. 再次把 paused preview 状态应用回预览系统
8. 更新 slider、统计信息和暂停按钮样式

这里有一个重要特点：

- pause second 优先以真正的音频运行位置为准
- 停止后不是只清掉“正在播放”的标记，而是要把预览系统重新归位到一个稳定的 paused 状态

## 13. Seek 与拖动工作流

seek 的目标是“立即把预览定位到某个秒数”，而不是“在后台慢慢追上”。

当前流程大致如下：

1. 确保 media runtime 和 SFX runtime 已可用
2. 若当前正在播放，先停止播放并保留位置
3. 更新主窗口内部的起播秒数、暂停秒数、timeline 起点等状态
4. 同步 paused 状态下的媒体时间戳
5. 更新画布、时间轴、统计和 slider

seek 本身不直接恢复 touchhold 声音，因为 paused preview 的语义要求它保持静音。

如果用户随后按播放，系统会从新的 paused second 重新进入播放启动事务。

## 14. 播放倍率与音频设置工作流

### 14.1 播放倍率

当播放倍率变化时：

- 主窗口更新自身倍率状态
- 把倍率同步给 media controller
- 把倍率同步给 SFX runtime
- 若当前正在播放，则通过“先停后启”的方式，用新倍率重新启动预览

这种设计的优点是简单、状态清晰；代价是倍率切换本身是一次完整的播放重建。

### 14.2 音频设置

音频设置变化时：

- media 侧背景音量立即同步给 media controller
- SFX 各类音量立即通过 `applyLevels` 更新到 runtime
- 试音面板采用短延迟防抖，在用户停止拖动后触发试听

当前语义默认：

- 试音应尽量反映用户刚刚调完的参数
- 试音使用的是正式 `QtPreviewSfxRuntime`
- 如果 runtime 尚未 prepare，会先做一次 on-demand prepare

## 15. Touchhold 的特殊语义

touchhold 在当前设计里不是普通的单次播放音效，而是一种持续音状态。

它的工作方式可以概括为：

- timeline 构建阶段，会把 touchhold 转成 span 信息以及内部 start/stop 事件
- drain 阶段遇到 `touchhold_start` 时启动对应 span
- 遇到 `touchhold_stop` 时停止对应 span
- 在 pause 或 stop 时，所有 touchhold voice 都会被强制停掉
- 在播放开始或 resume 时，会根据当前秒数重新扫描 span，并恢复此刻应处于活动状态的 touchhold

因此 touchhold 依赖的不是单个时刻，而是“当前秒数附近的一段连续状态”。这也是它对同步时序特别敏感的原因。

## 16. 自动启动与慢刷新协同

如果用户在最新 preview snapshot 尚未准备好的时候按下播放：

1. 主窗口不会立刻播放
2. 会保留一份待启动请求
3. slow refresh 产出对应 revision 的 preview snapshot 后
4. 若当前难度和 revision 仍匹配，则自动再次调用播放启动流程

当前约束是：

- auto-start 只等待 preview snapshot
- 不要求等待 validation 完成
- 不额外弹出等待 UI
- validation/Muri 的结果即使已经在后台算出，播放期间也允许暂缓到下一次 paused/idle 再刷新到可见 UI

这个设计能保证播放尽量使用最新内存态，同时不被更慢的验证链路拖住。

## 17. Latency Detector 中的 QtPreviewSfxRuntime

除了主预览链，延迟检测器也会单独创建自己的 `QtPreviewSfxRuntime` 实例。

它主要用于：

- 本地背景音轨播放
- 当前秒数读取
- beat audition

这说明 `QtPreviewSfxRuntime` 当前不仅服务于主预览，还被当成一个可复用的同步音频服务组件使用。后续若改变其线程模型，需要同时评估主预览和延迟检测器两条链的行为差异。

## 18. 当前架构的优点

当前方案有几个明显优点：

- 预览播放依赖稳定的 snapshot，而不是边编辑边直接改写正在播放的内容
- media 后端已经迁移到专用 owner 线程，跨设备鲁棒性明显更好
- warmup 已经从“初始化真实 runtime”收缩为“预热轻量数据与文件缓存”
- validation / Muri 已明确降级为后台 latest-only 结果，播放期间不再追求可见 UI 的即时性
- paused preview 语义明确，便于保证 seek、resume 和 touchhold 行为一致
- SFX timeline 的事件生成和 drain 责任集中，逻辑边界清楚

## 19. 当前架构的主要张力

当前最主要的结构张力集中在 `QtPreviewSfxRuntime`：

- 它同时承担资源管理、设备控制、背景轨同步、事件游标推进和 touchhold 生命周期管理
- 它当前由主线程直接调用，因此很多调用路径天然依赖“调用完成即生效”的同步语义
- 这让它很容易用，但也让后续线程化难度变高

尤其是以下语义都强依赖同步顺序：

- start/resume 时立刻得到实际起播秒数
- pause 后 touchhold 必须立刻静音
- tick 内 `drainEvents` 必须和当前 canonical second 保持一致
- 音量滑杆调整后的试音应反映最新参数

## 20. 优化思路与可能影响

### 20.1 优化思路

我建议后续把 `QtPreviewSfxRuntime` 的优化方向定为：

1. 保留当前 snapshot 模型和 paused preview 语义，不改产品行为边界。
2. 把 `QtPreviewSfxRuntime` 逐步改造成固定 owner 线程组件。
3. 但不要把所有现有接口直接改成异步排队，而是先抽出少量“事务型接口”。

更具体地说，可以考虑把最敏感的操作收敛为几个原子事务：

- `preparePausedState`
- `startPlaybackTransaction`
- `stopPlaybackTransaction`
- `tickTransaction`
- `applyLevelsAndAudition`

普通 setter 可以异步，强同步语义则通过事务接口保留。

### 20.2 预期收益

如果设计得当，这样做可能带来：

- 更清晰的 owner 边界
- 更低的线程/设备不确定性
- 更容易隔离音频后端初始化和运行时状态
- 更一致的跨设备表现
- 后续继续优化首次 prepare 和后台重建时更容易控制副作用

### 20.3 可能的风险

这类优化也会带来明显风险，主要包括：

- 若事务边界切得太碎，主线程与音频线程之间的频繁阻塞可能带来新的卡顿
- 若把同步语义改成普通异步队列，可能出现首拍延后、pause 后仍短暂发声、touchhold 恢复晚一帧等问题
- 若播放时钟改为读取异步镜像状态，可能导致时间轴、画布和音频实际时间出现轻微漂移
- 若试音和音量应用顺序不能严格保证，用户可能听到旧参数下的 audition
- 延迟检测器也依赖同一个 runtime 语义，线程化方案必须同步评估其交互体验

### 20.4 建议的实施策略

更稳妥的推进方式是：

1. 先把现有同步语义写清楚并固化成文档与测试预期。
2. 再把 `QtPreviewSfxRuntime` 的主预览调用面收敛为少数代理入口。
3. 先保证播放、暂停、seek、resume、试音这些关键事务的语义不变。
4. 最后再考虑是否进一步引入更激进的异步镜像状态。

如果后续要做线程化，建议优先把“语义保持不变”作为第一目标，而不是单纯追求接口形式上的异步化。
