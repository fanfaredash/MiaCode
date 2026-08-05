# MiaCode 0.5.0-beta9 至当前版本预览帧率回退审计

- 审计日期：2026-08-05（Asia/Shanghai）
- 当前审计对象：`a87c09d80eea2b44257611cb129ca8958ec83b4d`
- 当前内部版本：`1.1.0-beta.7-test.4`
- 比较基线：`92194b3d7d687fc7c7df3364853a7bd007def600`（2026-06-16）
- 比较范围：`92194b3d7d687fc7c7df3364853a7bd007def600..a87c09d80eea2b44257611cb129ca8958ec83b4d`
- 变更规模：424 个提交，878 个文件，新增 119,186 行，删除 42,159 行
- 审计性质：代码与提交历史静态审计，并合并仓库已有审查和已有实测；没有为本报告新增运行时性能采样
- 排序规则：先按“能否进入当前默认热路径”，再按“单次成本、发生频率、覆盖用户范围、既有证据强度”综合排序

## 1. 边界说明

仓库没有 `0.5.0-beta9` tag，也没有能被 Git 唯一定位为 `0.5.0-beta9` 的版本提交。历史中可见 `0.5.0-beta8`，随后出现 `0.5.1-beta`，再由 `037654a0a66e2557c202febfffda3d1011b4d17b` 仅修改版本元数据为 `0.5.0-beta10`。因此本报告采用该版本元数据提交的直接父提交 `92194b3d7d687fc7c7df3364853a7bd007def600` 作为“beta9 末端代码代理”。这个快照的 `CMakeLists.txt` 仍写作 `0.5.1-beta`，但它是当前历史中最接近且可复现的 beta9/beta10 代码分界。

这意味着：

- 报告能可靠回答“beta10 打包点之前的最后一份代码到当前版本发生了什么”；
- 如果外部分发的 `0.5.0-beta9` 二进制并非由该父提交构建，个别结论的“首次出现版本”可能前后偏移一个内部包；
- 所有列出的提交都位于代理基线之后，除明确标为“非增量、降级或排除”的项目外，均属于本比较窗口内的代码变化。

## 2. 总体结论

当前代码中，最值得怀疑的持续性回退不是单一绘制算法，而是三个可以叠加的常驻链路：

1. `PreviewFrameState` 跨线程快照在一个预览 tick/呈现周期内被完整发布约 4–5 次；当前状态对象含 93 个直接 `QImage` 字段、4 个 `QVector<QImage>` 字段，以及谱面标记、无理分析、视频帧、字符串和缓存指针。即使 Qt 容器与图像采用隐式共享，每次复制仍会产生共享对象分配、大量引用计数写入和一次原子 `shared_ptr` 发布。
2. Windows 高性能 GPU 绑定默认开启后，混合显卡机器可能形成“根 Quick 窗口在独显、预览合成窗口在默认集显”的双 QRhi/双适配器结构；它与 OBS、浏览器、视频解码和 2 GB 独显显存压力可共同放大呈现等待。
3. PV 视频默认改为硬件解码，而默认关闭的 H2 单设备模式使常规 Windows 路径仍可能经过 D3D11VA 两设备共享纹理、逐帧复制和最长 180 ms 的 keyed-mutex 等待。

其后是两类次级因素：一类在默认或近默认路径上制造固定 CPU 分配/格式化，例如 HUD 诊断字符串的提前构造和扩展事件 JSON；另一类只在特定谱面、视觉样式、背景模式、长音频、诊断开关或切换/启动阶段出现，例如触摸判定精灵翻倍、星光判定、双 VideoOutput、全窗口模糊、波形全量解码和烟花预热重建。

本报告没有把“相关提交存在”写成“已经证明导致掉帧”。除仓库已有审计明确记录的实测外，所有强度结论均为代码路径、频率和资源规模推断。

## 3. 可疑度总表

| 排名 | 可疑度 | 审计结果 | 覆盖范围 | 主要形态 |
|---:|---|---|---|---|
| 1 | 极高 | 帧状态快照在单个 tick/呈现周期内放大为约 4–5 次完整复制与原子发布 | 所有实时预览 | 持续 CPU、分配、引用计数与跨线程争用 |
| 2 | ~~极高（条件性）~~ → **待触发** | 高性能 GPU 默认绑定造成根窗口与预览合成窗口分属独显/集显 | Windows 混合显卡，且高性能适配器 ≠ 默认适配器，且独立合成窗口已启用 | 持续跨适配器合成、显存与呈现竞争 |
| 3 | 高（条件性） | PV 默认硬解进入 D3D11VA 两设备共享纹理与 180 ms keyed-mutex 路径 | Windows、含 PV/BG 视频 | 解码复制、同步等待、丢帧或长尾阻塞 |
| 4 | 高至中 | HUD 诊断详情在诊断关闭时仍提前格式化；开启后还可逐文本强制 flush | 默认时间戳 HUD；诊断开启时更强 | 固定字符串分配；条件性日志阻塞 |
| 5 | 中高（条件性） | 扩展事件总线每 tick 构造 JSON，启用扩展后在 GUI 线程执行 JS 回调 | 所有预览有基础成本；扩展用户更高 | JSON 分配、队列扫描、JS 执行 |
| 6 | 中高（瞬时） | 烟花 PSO 预热在确认绘制前随 playhead 反复提升场景 revision | 启动/资源加载后约 2–4 秒上限 | 全谱面 prepared cache 重建 |
| 7 | 中（谱面相关） | Touch 判定特效由 9 个精灵增至 17 个 | Touch 密集谱面 | 几何、批次、纹理绑定与透明过绘制 |
| 8 | 中（模式相关） | 内圆适配外部填充模式对同一视频维护双 sink、双 VideoOutput 和离屏遮罩 | BG/PV 且缩放模式 3 | 视频采样与额外合成 pass |
| 9 | 中（样式相关） | Starry/DX 非 Break Tap 每事件增加两圈共 16 个星光精灵 | Starry 样式、Tap 密集段 | 几何和透明过绘制 |
| 10 | 中（设置相关） | 自定义应用背景升级为全窗口纹理与 MultiEffect 模糊 | 自定义背景且模糊大于 0 | 根 Quick 窗口离屏层、显存与填充竞争 |
| 11 | 中（阶段性） | 波形缓存未命中时读入压缩文件并完整解码为单声道 float | Windows/macOS（含 BASS 的构建），打开/切换长音频谱面 | 后台 CPU、内存、分页与 BASS 生命周期竞争 |
| 12 | 中低 | 2026-08-05 起每预览 tick 查询一次 BASS mixer 播放位置 | Windows/macOS、BGM 正在运行 | GUI 线程音频 API 查询与偶发重锚 |
| 13 | 中低（诊断条件） | 帧节奏诊断为每帧安装计时并对慢帧无条件写日志 | `--debug`/诊断环境变量 | 测量和日志自扰动 |
| 14 | 低（突发） | 预览纹理仓库越界后整代清空并在同一帧重建 | 高 DPR/大量资产变体 | 单帧或数帧纹理重传 |
| 15 | 低（过渡态） | QuickShell 几何变化与 VideoOutput 绑定定时器形成切换期抖动链 | 窗口缩放、DPI、全屏、显示状态变化 | attach/detach 与布局抖动 |
| 16 | 低（平台/音频条件） | BGM 默认使用 BASS_FX tempo 与 compact40 窗口 | Windows/macOS、BGM | 音频线程 CPU 竞争 |
| 17 | 低且已有实测降级 | 时间轴纹理缓存会跨谱面增长，当前只在 8192 项逃逸阈值清空 | 长会话、多次换谱/皮肤/缩放 | 驻留增长；极端时全清 |
| 18 | 很低（交互条件） | Touch 创作悬停增加输入命中与一个 QSG 图层 | 暂停且创作快捷状态开启 | 鼠标移动更新和额外透明节点 |
| 19 | 很低 | 分区 HUD 字体使每次 HUD 重绘额外构造 chart-info 字体路径 | HUD 重绘 | QFont/QFontInfo 与字形缓存分裂 |
| 20 | 很低（加载期） | 自定义轮廓暂停区合成增加逐像素亮度处理和多次 QPainter 合成 | 轮廓/皮肤重载 | 后台资产生成峰值 |

## 4. 排序后的详细审计结果

> 每条结论下方的「> 审查（2026-08-05）」引用块是本报告发布后的逐条复核回执：核对结论是否与当前代码一致、是否已直接修复、还是需要取舍或属误报。汇总见第 10 节。已落地的代码修复在提交 `cd3c446a`。

### 4.1 帧状态快照在单个 tick/呈现周期内放大为约 4–5 次完整复制与原子发布

- 可疑度：极高；当前最强的全局静态候选
- 日期：2026-07-05；2026-07-20 调整原子实现
- 版本：首次引入于 `1.0.1-beta.3`；原子 free-function 形式位于 `1.1.0-beta.5`
- 提交：`8a4085b4e42181d8f7aff0346f97b3ae80c965c2`；`2e7460efd50b6cfb6b05dc8574cb8beacc3d76d3`
- 模块：实时预览状态发布、GUI/渲染线程桥、帧节奏
- 目录：`src/preview/runtime/`、`src/core/scene/`、`src/preview/quick_scene/`、`src/app/mainwindow/sections/timeline/`
- 触发链路：`预览定时 tick / QSG present` → `noteTickForProfiling()`、`applyQtPreviewPosition()`、`applyVisualClockSmoothing()`、`requestNextFixedIntervalPreviewFrame()`、`handlePresentedFrame()` → `PreviewRuntime::publishFrameStateSnapshot()` → `make_shared<PreviewFrameState>(frameState_)` → `refreshPreviewFrameStateHudStatsSnapshot()` → `atomic_store(shared_ptr)` → 渲染线程 `frameStateSnapshot()`/`atomic_load`

代码事实：

- `PreviewRuntime::publishFrameStateSnapshot()` 每次都创建新的 `shared_ptr` 控制块与 `PreviewFrameState` 对象，而不是只更新一个小型时间戳结构。
- 当前 `PreviewFrameState.h` 及其内嵌资产结构共有 93 个直接 `QImage` 字段和 4 个 `QVector<QImage>` 字段；此外还含 `noteMarkers`、Muri 报告/选项、`QVideoFrame`、多个 `QString`、共享进度缓存等。
- Qt 隐式共享避免了图像像素深拷贝，但没有消除每个字段的引用计数读写、容器复制、堆分配和跨线程原子共享指针交换。
- 现行固定间隔路径中，一个播放 tick 至少包含：`noteTickForProfiling()` 一次发布、播放中的 `setPlayheadSeconds(second, false)` 一次发布、下一帧 `PreviewRuntime::update()` 一次发布；视觉平滑时间不同于音频时间时再发布一次。独立的 `handlePresentedFrame()` 每个实际呈现再发布一次。因此稳定情况下约为每 tick/呈现周期 4 次，视觉平滑生效时约 5 次。
- 以 tick 与 present 频率接近估算：60 Hz 约 240–300 次快照/秒，120 Hz 约 480–600 次/秒，180 Hz 约 720–900 次/秒。仅 93 个直接 `QImage` 字段就对应约 22,320–27,900、44,640–55,800、66,960–83,700 次引用触碰/秒，尚未计入向量内图像和其他隐式共享字段。
- 代理基线已有相似的 tick、视觉平滑和请求下一帧链路，但没有这套完整状态快照复制/原子发布。因此它是明确位于比较窗口内的增量热路径。

评估：该项覆盖所有实时预览，不依赖视频、特定谱面或诊断开关；它同时发生在 GUI 线程和渲染线程交界处，能直接侵蚀高刷新率下更短的帧预算。静态代码无法给出实际毫秒数，但发生频率、对象宽度和回归边界均最完整。

> 审查（2026-08-05）：**结论成立，但两条修复路线都不属于低风险，因此未直接改动。**
>
> 已逐条核对：`publishFrameStateSnapshot()` 确为整体 `make_shared<PreviewFrameState>(frameState_)`；`PreviewFrameState.h` 当前正好是 93 个直接 `QImage` 字段与 4 个 `QVector<QImage>`（`wifiImages` / `wifiEachImages` / `wifiBreakImages` / `wifiMineImages`），计数无误。播放期单 tick 的 4 次发布链路也逐个走通了：`noteTickForProfiling()` → `applyQtPreviewPosition()` 里的 `setPlayheadSeconds(second, false)` → 视觉平滑的 `setPlayheadSeconds(visualSecond, false)` → `requestNextFixedIntervalPreviewFrame()` 里的 `update()`；再加每次呈现的 `handlePresentedFrame()`，稳态就是 5 次。
>
> 补一个报告没写死的前提：第三次发布不是「视觉平滑生效时才有」，而是必然发生——`previewVisualLookaheadVsyncs()` 默认 1.0 而非 0，所以 `visualSecond` 恒不等于 `second`，`qAbs(...) > 1e-9` 恒真。
>
> 两条可行路线及其取舍：
>
> - **(a) tick 内合并发布。** 给 `PreviewRuntime` 加一个批处理作用域，把 tick 期间的发布推迟到 `requestNextFixedIntervalPreviewFrame()` 之后一次完成，5 次 → 2 次。逻辑上是安全的（GUI 线程在 tick 函数返回前不会走到渲染同步点），但它改变的正是渲染线程读到状态的时刻，而本仓库在帧节奏上反复栽过这类跟头——第 5.3 节记录的 present-driven gate 就是上线后又撤销的。没有 Windows 实测数字就合入不合适。
> - **(b) 收窄快照。** 把 `skin` / `judgeOverlay` / `judgeEffect` / `noteMarkers` 这些「每谱一次」的大块移入 `shared_ptr<const>`，让每帧拷贝只剩标量与少量共享指针。这才是真正的解法，但要改所有 `state.skin.xxx` 形式的消费方，属结构性改动。
>
> 建议顺序：先用 (a) 在 Windows 上做一次 A/B 拿到毫秒量级，确认这条确实值钱，再决定是否投入 (b)。

### 4.2 高性能 GPU 默认绑定可使根窗口与预览合成窗口分属两个适配器

- 可疑度：极高，但依赖 Windows 混合显卡拓扑
- 日期：2026-07-04
- 版本：策略骨架 `1.0.1-beta.3-test`；默认开启提交为 `1.0.1-beta.5-test`，既有审计记录的首个用户可见包为 `1.1.0-beta`
- 提交：`c87ae83f4b1bc8143827d9f992ae2f4fd5cf902e`；`41cf08a8afa48f31296feba7dc775f86897b4299`
- 模块：GPU 设备策略、QuickShell 根窗口、预览复合窗口、Windows D3D11/QRhi
- 目录：`src/app/gpu_device_provider.cpp`、`src/app/quick_shell/`、`src/common/DebugOptions.h`
- 触发链路：`QuickShell 启动` → 解析高性能 DXGI LUID → 根 `ApplicationWindow` 在场景图初始化前 `QQuickGraphicsDevice::fromAdapter(独显)` → 预览 `QQuickView` 以 `preferVideoShareDevice=true` 保持 Qt 默认适配器/集显 → 两套 QRhi 与持久场景图并存 → DWM 跨适配器合成、显存迁移和呈现背压 → 预览帧率降低

代码事实：

- `gpuBindHighPerformanceEnabled()` 在没有环境变量覆盖时直接返回 `true`。
- 根 Quick 窗口调用 `bindHighPerformanceQuickGraphicsDevice(..., preferVideoShareDevice=false)`，会绑定解析出的高性能适配器。
- 预览复合窗口调用同一 provider，但传入 `preferVideoShareDevice=true`。H2 单设备路径关闭或不可用时，provider 明确保留 Qt 默认适配器，以维持 FFmpeg D3D11VA 同适配器共享桥。
- 预览复合 `QQuickView` 设置 `setPersistentGraphics(true)` 与 `setPersistentSceneGraph(true)`，最小化或可见性变化时资源仍可驻留。
- provider 内注释仍称根窗口非默认适配器绑定“opt-in until validated”，与当前默认返回 `true` 相互矛盾，表明风险说明没有随默认值翻转同步。
- 单 GPU 机器，或“高性能适配器就是系统默认适配器”的机器，不形成实际拆分。

既有审计结合：`OBS_CONTENTION_PLAYBACK_STUTTER_AUDIT_ZH.md` 和 `WINDOWS_IDLE_FREEZE_AUDIT_REVIEW_ZH.md` 已把 i5-1155G7 + MX450 2 GB + Iris Xe 场景识别为根窗口落在 MX450、预览/视频合成保留在 Iris Xe 的高风险结构。OBS、浏览器硬件加速、视频解码和较小独显显存可同时增加 DWM 合成、VRAM 驻留和 present 等待。

评估：该项对混合显卡用户可持续存在，且与“单独运行尚可、OBS/浏览器并发时下降”这一既有现象高度吻合；对单 GPU 用户解释力较弱。

> 审查（2026-08-05）：**代码描述成立，但触发场景暂未遇到——实测与代码复核都不支持这条当前正在发生。降级为「待触发」，不作为排查重点。**
>
> 代码事实无误：`gpuBindHighPerformanceEnabled()` 无环境变量时确实 `return true`，根窗口 `preferVideoShareDevice=false`、预览合成 `=true` 的分流也与描述一致。报告点出的注释矛盾属实，`gpu_device_provider.cpp:109` 的「opt-in until it is validated」自 `41cf08a8` 默认翻转起就不再成立，已在 `cd3c446a` 改写。
>
> 但本节描述的双适配器结构，有两道独立的门把它挡住了：
>
> **门一：这台目标机器上高性能适配器就是默认适配器。** 2026-08-04 的两次会话日志（`logs`、`logs 2`）里，两个 surface 都是 `action=skip`：
>
> ```
> surface=quick_shell_preview_composite  action=skip  reason=video_surface_keeps_default_for_decode_bridge
> surface=quick_shell_root_window        action=skip  reason=high_perf_equals_default_adapter  luid=0x0:0xf856
> ```
>
> 原因在 `idle/vram_gauge` 的适配器扫描：DXGI adapter 0 是 MX450（`0xf856`），adapter 1 才是 Iris Xe（`0xf25b`）。**独显本身就是默认适配器**，所以 `gpu_device_provider.cpp:150-160` 的「高性能 == 默认则跳过」保护直接命中，绑定压根没执行。设备探针也确认根窗口落在 `adapter="NVIDIA GeForce MX450"`。本节假设的「根窗口在独显、预览在集显」在这台机器上是反的。
>
> 顺带否掉配套的显存假设：MX450 显存用量峰值 79 MB / 预算 1639 MB，所有采样的 `local_over_budget` 全为 0。
>
> **门二：独立合成窗口在当前代码里根本不会上屏。** 这一条比门一更彻底，且与机器无关——`MainWindow::PreviewSection::quickShellPreviewUsesSeparateSurface()` 现在硬编码 `return false`（`3f89c397` 起；此前它是按 `hasVideoMedia()` 动态决定的）。`QuickShellMain.qml` 里两个承载 `previewCompositeWindow` 的 `Loader` 都以 `controller.previewUsesSeparateSurface` 为 `active` 条件，于是永远不激活。
>
> 日志完全印证：`preview_composite` 全程 `from=Hidden to=Hidden exposed=0`，且全日志没有一条 `quick_shell/device surface=quick_shell_preview_composite` 探针（该行只在首次渲染时打），说明它一帧都没画过、也从未初始化 QRhi。预览实际以 `role=embedded_inline` 跑在根窗口内，`external_stage_media.separate_surface_active=0`。
>
> 也就是说：当前构建里只存在**一个**在渲染的 Quick 窗口，「两套 QRhi 并存 + DWM 跨适配器合成」这个前提不成立。`QuickShellPreviewCompositeSurface` 仍会在启动时构造一个 `QQuickView`（这就是那条 `gpu_provider` 日志的来源），但它永远不上屏。
>
> **结论：** 本节保留为架构风险记录——一旦 `quickShellPreviewUsesSeparateSurface()` 重新启用，或换到一台「高性能适配器 ≠ 默认适配器」的机器（例如 iGPU 驱动主显示器的常见混合显卡配置），两道门会同时打开，本节描述即刻成立。但作为当前掉帧的解释，它没有证据，不应占用排查预算。默认值也因此不需要改动。
>
> 附带发现（不属本节）：`shouldUseSeparatePreviewSurface()` 及其 `quickshell_preview_surface_policy_spec` 仍在仓库里并持续跑测试，但生产端唯一调用者已被 `return false` 取代，成了孤儿策略。

### 4.3 PV 默认硬件解码进入 D3D11VA 两设备共享纹理与 180 ms keyed-mutex 路径

- 可疑度：高，依赖 Windows、视频背景和解码偏好
- 日期：2026-06-18
- 版本：H2/共享桥引入于 `0.5.0-beta10`；硬件默认偏好位于 `0.5.0-beta16`
- 提交：`04f95813e1f17ccd619464d26eed59b8d0da05df`；`2d22f22ba759c11a2c23fa1d4d76af44cff34279`
- 模块：预览舞台媒体、QtAVPlayer、FFmpeg D3D11VA、QRhi 视频纹理
- 目录：`src/preview/runtime/`、`third_party/QtAVPlayer/src/QtAVPlayer/`
- 触发链路：`加载 pv.mp4/bg 视频` → 默认 `videoDecodePrefersSoftware=false` → FFmpeg 创建 D3D11VA 解码池 → 生成 D3D11 硬件帧 → H2 单设备关闭时建立共享纹理和 keyed mutex → `AcquireSync`/`CopyResource`/共享句柄导入 → `QVideoSink`/`VideoOutput` → QSG 合成与 present → 同步等待或帧丢弃 → 预览帧率下降

代码事实：

- `2d22f22b` 明确移除了“Auto 模式下集显自动使用软件解码”的行为，默认偏好变成所有 GPU 使用硬件解码；集显探测现在主要保留给诊断输出。
- H2 单设备共享模式默认关闭；在普通默认配置下，解码器和预览 QRhi 仍可能是两个 D3D11 device。
- QtAVPlayer 的两设备路径创建 `D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX` 纹理，并在源、目标同步点执行 `AcquireSync` 与复制。
- 当前 `kPreviewAcquireSyncTimeoutMs` 为 180 ms。它比历史无限等待安全，但仍远大于 60 Hz 的 16.67 ms 帧预算；超时路径会丢弃帧并重建相关共享对象。
- 默认 QSG/每像素 alpha 路径不会逐帧调用 `QVideoFrame::toImage()`；CPU 图像转换只服务于特定 DComp fallback。因此本报告没有把默认视频路径误判为逐帧 GPU→CPU readback。
- AV1 当前强制使用 libdav1d 软件解码；上述硬解结论主要覆盖 H.264/HEVC/VP9 等 D3D11VA 可用格式。

评估：这是一条明确的逐视频帧同步/复制链，可同时受到集显负载、OBS、跨窗口 GPU 策略和解码池压力影响；不含 PV/BG 视频的谱面不会触发。

> 审查（2026-08-05）：**结论成立，但可选方向都是产品取舍，未直接改动。**
>
> 三点事实全部核对无误：`videoDecodePrefersSoftware_` 默认 `false`（硬解）、H2 单设备默认关闭、`kPreviewAcquireSyncTimeoutMs = 180`（`qavhwdevice_d3d11.cpp:82`）。
>
> 报告说对了一件值得单独留档的事：默认路径确实不做逐帧 `toImage()`。`handleDecodedVideoFrame()` 里 `needsCpuImageForDComp` 同时要求 `previewUseDCompEnabled()` 且 `!previewDCompPerPixelAlphaEnabled()`，默认配置两个条件都不满足；那段注释还记录了在 GUI 线程对 D3D11VA 硬件帧调 `toImage()` 曾在 Intel iGPU 上造成 use-after-free。这条不能因为「看起来能省一次拷贝」被反向改回去。
>
> 两个可选方向各自带包袱：打开 H2 单设备（`DebugOptions.h:280` 明确标注 RESERVED / UI 隐藏 / 不再推进）、或对集显恢复软解默认（`2d22f22b` 刚刚移除该行为）。任一都必须先在受影响的 iGPU 机器上验收，不该凭静态审计翻默认值。

### 4.4 HUD 诊断详情在关闭时仍被提前格式化，开启时还可逐文本强制 flush

- 可疑度：高至中；默认存在固定成本，诊断开启时风险显著放大
- 日期：2026-07-05
- 版本：`1.0.1-beta.3`
- 提交：`8a4085b4e42181d8f7aff0346f97b3ae80c965c2`
- 模块：预览 HUD、QQuickPaintedItem、运行时诊断日志
- 目录：`src/preview/quick_scene/PreviewQuickHudLayer.cpp`、`src/common/DebugOptions.h`、`src/common/DebugLog.cpp`
- 触发链路：`frameStateChanged` → HUD 100 ms 节流更新 → `PreviewQuickHudLayer::paint()` → 构造 `paint_enter`/`paint_overlay_call`/`overlay_enter`/分支/文本前后诊断详情字符串 → `appendHudPaintDiagLine()` 内部才检查诊断开关 → 默认丢弃已构造字符串；若开关开启则写日志并在部分调用上 `flushAsyncLogWriter(100)` → GUI/渲染合成预算被占用

代码事实：

- 诊断开关判断位于 `appendHudPaintDiagLine()` 函数体内部，但调用者先完成 `QStringLiteral(...).arg(...)`、`pointerHex()`、`painterDiagPayload()` 和 `logTextPreview()`，因此关闭诊断并不消除详情字符串构造。
- 默认显示时间戳时，每次 HUD paint 至少经过 paint 入口、overlay 入口、时间戳分支、`draw_text_before`、`draw_text_after`、对象统计跳过和 paint 出口等多组字符串格式化；HUD 更新已被旧代码限制在约 10 Hz，所以不是 60/120 Hz 逐帧执行，但仍是稳定新增分配。
- `drawHudText()` 的 `draw_text_before` 标记为 `durable=true`。开启 HUD paint 诊断时，每个实际绘制文本都可调用 `flushAsyncLogWriter(100)`；开启 debug HUD 后一帧有多行文本，flush 数量同步增加。
- 同一提交既引入该诊断包装，也引入第 4.1 节的状态快照，因此两项会共同出现在 `1.0.1-beta.3` 之后。

评估：默认关闭诊断时，10 Hz 的字符串分配单独造成大幅 FPS 回退的解释力低于前三项，但它是确定存在的无条件新增工作；诊断开关开启后，逐文本强制 flush 足以成为显著自扰动源。

> 审查（2026-08-05）：**结论成立，已修（`cd3c446a`）。**
>
> 报告还漏算了一半成本：`previewHudPaintDiagnosticsEnabled()` 不做缓存，每次调用都是一次 `qEnvironmentVariable` + `trimmed()` + `toLower()` + 最多 4 次字符串比较，而关闭诊断时每次 HUD paint 仍会命中约 20 次（每个 diag 点一次）。
>
> 修法：所有调用点改走惰性包装 `appendHudPaintDiag(action, detailFn)`，先判开关再构造 detail；开关本身按 `DebugLog.cpp` 里 `skipAsyncLogFlush()` 的既有做法只读一次。诊断开启后的行为（包括 `draw_text_before` 的 `durable=true` 逐文本 flush）保持原样——那是这条诊断刻意要的持久性，收紧它属于第 4.13 节同类的取舍。

### 4.5 扩展事件总线每 tick 构造 JSON，启用扩展后在 GUI 线程执行 JS 回调

- 可疑度：中高，基础成本广泛，重成本取决于扩展运行状态和订阅
- 日期：2026-08-03
- 版本：`1.1.0-beta.7`
- 提交：`35f6ad90a9910b84a3c57f6e2b446188de985675`
- 模块：预览 tick、扩展管理器、嵌入式 JavaScript 运行时、事件队列
- 目录：`src/app/mainwindow/sections/timeline/MainWindow.PreviewTick.cpp`、`src/extensions/ExtensionManager.cpp`、`src/extensions/EmbeddedExtensionRuntime.cpp`
- 触发链路：`onQtPreviewTick()` → 构造嵌套 `QJsonObject{source,data.second}` → `ExtensionManager::publishEvent("preview.position.changed", ..., true)` → 运行时存在时补充 kind/name/version/sequence/两个时间戳 → 扫描订阅和 pending 队列并合并事件 → 16 ms 单次定时器 → GUI 线程 `QJSEngine` 回调 → tick 或渲染请求延后

代码事实：

- `extensionManager_` 在主窗口 bootstrap 中无条件 `make_unique`，所以播放 tick 会无条件构造两层 `QJsonObject` 并进入 `publishEvent()`；只有进入 manager 后才判断 runtime 是否存在且正在运行。
- 运行时活跃时，每次 dispatch 复制 payload，并插入序列号、毫秒时间戳和 UTC ISO 时间字符串。
- dispatch 每次重建 `ownerBySubscription` 与 `queueDepthByExtension` 哈希表，扫描 callback 列表和 pending 列表；可合并事件还要 `find_if` 查找已有项。
- 回调由 GUI 线程调用。单次执行超过 16 ms 不会立即停止；同一回调连续 5 次超过 16 ms 后才被 suspend，因此可连续占用至少 5 个明显超帧预算的回调周期。
- 事件 coalescing 限制了队列增长，但不限制被保留的最新回调本身执行多久。

评估：没有扩展运行时或订阅时，主要是每 tick JSON 构造；有预览位置订阅时，成本可放大为 GUI 线程的 JavaScript 执行。它是当前版本才出现的常驻 tick 增量，但实际严重程度高度依赖扩展集合。

> 审查（2026-08-05）：**结论成立，已修（`cd3c446a`）；且报告低估了无订阅时的成本。**
>
> 报告把「没有订阅」的成本描述为「主要是每 tick JSON 构造」，实际上更贵：只要运行时在跑，`dispatchEvent()` 就会先复制 payload、插入 6 个键（其中 `timestamp` 要走 `QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)`）、再建两张 `QHash` 分别扫描 `eventCallbacks_` 与 `pendingEvents_`，**之后**才在投递循环里发现没有一个 callback 匹配。
>
> 修法：新增 `EmbeddedExtensionRuntime::hasEventSubscriber()` / `ExtensionManager::hasEventSubscribers()`，通配符匹配逻辑与投递循环共用一个 `eventPatternMatchesKind()` 以免两处判断漂移。`dispatchEvent()` 在 enrichment 之前先做这个预检，`onQtPreviewTick()` 也用它跳过 JSON 构造本身。`nextEventSequence_` 因此不再为无人接收的事件递增——序号仍然单调，只是少了空洞，投递方看不出差别。
>
> 「同一回调连续 5 次超过 16 ms 才被 suspend」与代码一致，未改：那是扩展 API 的容错策略，收紧它会改变扩展作者可依赖的行为，属取舍。

### 4.6 烟花 PSO 预热在确认绘制前随 playhead 反复提升场景 revision

- 可疑度：中高，但通常是启动/资源加载后的瞬时回退
- 日期：2026-06-19
- 版本：`0.5.0-beta16`
- 提交：`688e97b14820e804a052d12edf6097e098a56988`
- 模块：烟花判定特效预热、场景 revision、prepared scene cache
- 目录：`src/preview/runtime/PreviewRuntime.cpp`、`src/core/scene/PreviewPreparedSceneCache.cpp`、`src/preview/quick_scene/PreviewQuickSceneRoot.cpp`
- 触发链路：`核心皮肤/烟花纹理加载完成` → `armFireworkPsoWarmupIfReady()` 插入合成 marker → 播放 tick 的一个或两个 `setPlayheadSeconds()` → `refreshFireworkWarmupForPlayheadChange()` 删除并重新追加 marker、`sceneContentRevision++` → 渲染线程 `PreviewPreparedSceneCache::sync()` 发现 revision 变化 → `rebuild()` 清空并重建所有 prepared layer、扫描所有 note marker、生成哈希表和 HS histogram 字符串 → 烟花节点真正产出后结束；否则最多 240 次 present

代码事实：

- 旧逻辑只等待两个 present；该提交改为必须观察到烟花层节点信号，并把 240 次 present 作为后备上限，注释估计约 2–4 秒。
- armed 且未完成时，每次 playhead 变化都会移除/追加合成 marker 并提升 `sceneContentRevision`。
- 默认播放链先写音频时间，视觉平滑不同时又写视觉时间，因此同一 tick 最多两次 revision 增长；渲染线程一般只看到最终 revision，但仍会在每帧执行一次完整 prepared cache rebuild。
- `PreviewPreparedSceneCache::rebuild()` 清空 10 个 prepared layer，扫描所有 marker，建立事件 key/代表 marker 哈希，并无条件构造 HS 分布 `QHash` 与 `QString/QTextStream`；日志 sink 是否输出是在字符串生成之后才体现。
- 一旦烟花层真正产生节点或达到 240 present，后续正常播放热路径不再执行该 recenter 工作。

评估：该项可清楚解释“刚开始播放、切皮肤或资源刚加载后的几秒帧率较低”，不能单独解释整段会话持续掉帧，除非烟花层长期无法完成且呈现计数也不前进。

> 审查（2026-08-05）：**结论成立；其中最贵的一块已修（`cd3c446a`），重建本身仍是取舍。**
>
> `kFireworkWarmupMaxPresents = 240`、armed 期间每次 playhead 变化都 `sceneContentRevision += 1`、`rebuild()` 清空 10 个 prepared layer 并扫描全部 marker，均与代码一致。
>
> 报告点出的「HS 分布字符串在日志 sink 判定之前生成」是这条里唯一能无损修掉的部分，已修：那段的原注释写着「Cheap（一 marker 一次 map 插入，一行日志）」——在重建罕见的前提下确实便宜，但正是本节描述的 warm-up recenter 让它变成了逐帧执行，于是每帧都要对全谱 marker 建一次 `QHash` 再拼一段 `QTextStream`，然后在 `appendLine()` 里因 Runtime 通道关闭而丢弃。现已前置 `runtimeDebugOutputEnabled()`（一次原子读）。
>
> `rebuild()` 的全量重建没动：`sceneContentRevision` 变化即整体失效是既定语义，要收敛得改成增量失效或让合成 marker 走局部路径，属结构性取舍。另一条更省事的方向是让 warm-up 不再借 playhead recenter 触发全局 revision，但那会动到烟花 PSO 预热的正确性前提（`688e97b1` 换成「必须观察到烟花层出节点」正是为了修早期只数 present 的老 bug），同样需要单独评估。

### 4.7 Touch 判定特效由每事件 9 个精灵增至 17 个

- 可疑度：中，谱面密度相关
- 日期：2026-07-13
- 版本：`1.1.0-beta.5`
- 提交：`86e817591e128361763dc5910e9529ce34f0c7f9`
- 模块：Touch 判定特效状态生成、QSG 精灵层
- 目录：`src/core/scene/PreviewTouchJudgeLayerState.cpp`、`src/preview/quick_scene/PreviewQuickTouchJudgeLayer.cpp`
- 触发链路：`当前活动窗口内出现 Touch 判定事件` → `PreviewPreparedSceneCache::touchJudgeLayer()` → `buildPreviewTouchJudgeLayerState()` → 每个位置触发生成 1 个中心圆 + 内圈 8 星 + 外圈 8 星 → 17 个 sprite descriptor → QSG 几何更新、纹理批次与 alpha blending → Touch 密集段渲染时间增加

代码事实：

- 提交前 `reserve(positionTriggers.size() * 9)`，实现为中心圆和合计 8 个星形部件。
- 提交后 `reserve(... * 17)`，实现明确调用两次 8 点 `appendSparkleRing()`，再加中心圆。
- 精灵数量增加约 88.9%；同时每点增加三角函数位置计算、纹理选择、旋转和宽高计算。
- 多个同时活动 Touch 位置按线性比例放大；没有 Touch 判定事件的帧返回空 layer state。

评估：这是定量最明确的谱面内容回归之一。它不影响无 Touch 或稀疏段，但能造成 Touch 密集区相对于代理基线更明显的 CPU 建模、顶点更新和透明过绘制。

> 审查（2026-08-05）：**数字属实，但定性应从「回归」改为「视觉设计取舍」，未改动。**
>
> `reserve(positionTriggers.size() * 17)` 与两次 8 点 `appendSparkleRing()`（`kJudgeEffectTouchSparklePointCount = 8`）都已核对，17 = 1 中心圆 + 8 内圈 + 8 外圈无误。
>
> 但 9→17 是 `86e81759` 刻意做的判定特效升级，不是无意引入的开销。把它当缺陷回退需要产品决定，本次不动。若确认这里是瓶颈，正确的优化方向是让同一 `QImage` 的 17 个精灵合批（减少批次与纹理绑定），而不是砍精灵数量——那会直接改变观感。

### 4.8 内圆适配外部填充模式对同一视频维护双 sink、双 VideoOutput 和离屏遮罩

- 可疑度：中，依赖背景缩放模式 3
- 日期：2026-07-05
- 版本：`1.0.1-beta.5-test`
- 提交：`ebc18174bae74f779be1861ed86d7bbd5e3cfcd3`
- 模块：舞台媒体 QML、视频 sink 绑定、背景合成
- 目录：`src/preview/runtime/qml/PreviewStageMediaItem.qml`、`src/preview/runtime/PreviewStageMediaHost_Media.cpp`、`src/preview/runtime/PreviewStageMediaHost_Diagnostics.cpp`、`src/app/quick_shell/qml/QuickShellPreviewSurface.qml`
- 触发链路：`backgroundScaleMode=InnerCircleFitOuterFill(3)` + `BG/PV` → 解码帧同时写入 outer/inner `QVideoSink` → 两个 `VideoOutput` 分别执行 crop 与 fit → inner source 进入 `ShaderEffectSource` → 圆形 mask 进入第二个 `ShaderEffectSource` → `MultiEffect` 遮罩合成 → 额外 QRhi pass、纹理采样和透明合成 → 预览帧率下降

代码事实：

- QML 同时存在 outer `VideoOutput` 和 `previewStageInnerVideoOutput`。
- `handleDecodedVideoFrame()` 对每个有效帧都写 outer sink，并在两个 sink 不同的情况下写 inner sink；该推送不检查当前是否正在使用模式 3。
- 模式 3 可见时，inner source 与 mask 都以 `ShaderEffectSource.live=true` 工作，随后由 `MultiEffect` 执行圆形遮罩。
- 对静态图片也存在外层采样和内层 fit/遮罩的双重合成，但视频路径还叠加双 sink 和硬解共享纹理链。
- 默认缩放模式是 `FillCrop`，因此完整额外 pass 不是默认状态；双 sink 帧赋值只要 inner output 已绑定就存在。

评估：该项与第 4.3 节可叠加，在模式 3 的视频背景上同时增加视频消费者、GPU pass 和同步压力。

> 审查（2026-08-05）：**结论成立；报告点出的那半已修（`cd3c446a`），模式 3 自身的合成成本保留。**
>
> 「该推送不检查当前是否正在使用模式 3」是这一节最有价值的观察，且后果比报告写的更具体：非模式 3 时那个 `VideoOutput` 的 `visible` 为假、不会产出 QSG 节点，所以推进去的帧没有任何消费者——但 sink 仍然持有这个 `QVideoFrame`，也就是钉住一块解码池 surface，而这恰恰是 D3D11VA 两设备桥最需要归还的资源。现已按 `backgroundScaleMode_` 门控；`refreshInnerVideoSinkForScaleMode()` 在切入模式 3 时用 `lastVideoFrame_` 补一帧、切出时清空，所以播放中切模式仍然立刻有画面。
>
> 模式 3 本身的双 `ShaderEffectSource` + `MultiEffect` 遮罩没动：QML 已把两个 source 的 `live` 绑定到 `innerCircleFitOuterFill`，非模式 3 时着色器 pass 不运行；剩下的开销是该显示模式的固有代价，取消它等于取消这个功能。

### 4.9 Starry/DX 非 Break Tap 每事件增加两圈共 16 个星光精灵

- 可疑度：中，样式与 Tap 密度相关
- 日期：2026-07-15；同日继续调参
- 版本：`1.1.0-beta.5`
- 提交：`15f6e6eb8736e7ebf78ed76f464212c248e75817`；`26c8ae750c75e8679b0485782137983cfabf497a`
- 模块：Tap 判定特效、DX/Starry 视觉样式、QSG 精灵批次
- 目录：`src/core/scene/PreviewJudgeEffectLayerState.cpp`、`src/preview/quick_scene/PreviewQuickJudgeEffectLayer.cpp`
- 触发链路：`judgeEffectStyle=Starry` → 活动的非 Break Tap 判定事件 → `appendSparkleRing()` 内圈 8 次 + 外圈 8 次 → 每事件 16 个星光 descriptor → QSG 批次/顶点和透明过绘制增加 → Tap 密集段帧率下降

代码事实：

- Starry 且非 Break 的分支在两次 8 点 ring 后直接返回，不再走标准形状路径。
- 每个点执行角度、正余弦、宽高、旋转和纹理选择计算。
- `PreviewRenderState` 和主窗口默认值仍是 `PreviewJudgeEffectStyle::Standard`，所以该项只影响选择了 Starry/DX 风格的用户或任务状态。

评估：精灵增量明确，但不是默认样式。它与 Touch 判定精灵翻倍、烟花和高密度谱面可叠加。

> 审查（2026-08-05）：**结论成立，同第 4.7 节属样式取舍，未改动。**
>
> Starry 且非 Break 的分支确实在两次 8 点 ring 后直接 `return`，不再走标准形状路径；`PreviewRenderState::judgeEffectStyle` 默认值仍是 `Standard`，所以影响面仅限主动选择 Starry/DX 的用户，报告的限定准确。

### 4.10 自定义应用背景升级为全窗口纹理与 MultiEffect 模糊

- 可疑度：中，设置相关
- 日期：2026-07-11 引入 QuickShell 背景桥；2026-07-13 扩展到根窗口背景
- 版本：`1.1.0-beta.5`
- 提交：`8f03411bcfe1a0ffd3f313714984664ae8a29171`；`6c258a06d7c83838f4e4d0b92abf26d316752c10`
- 模块：QuickShell 根窗口背景、外观设置、Qt Quick Effects
- 目录：`src/app/quick_shell/qml/QuickShellMain.qml`、`src/app/quick_shell/QuickShellStyleBridge.cpp`、`src/app/ui/AppBackgroundPainter.cpp`
- 触发链路：`自定义应用背景 active` + `blur>0` → 根 `ApplicationWindow.background` 全窗口 `Image` → mipmap/cache + `layer.enabled` → 离屏 layer texture → `MultiEffect` blur（`blurMax=64`）→ 根 QRhi 窗口渲染/呈现 → 与独立预览复合窗口同时竞争 GPU、显存和 fill-rate → 预览帧率下降

代码事实：

- 当前背景 `Image` 覆盖整个根窗口，启用 smooth、mipmap、asynchronous、cache。
- 模糊大于 0 时开启 QML layer 并应用 `MultiEffect`，形成额外离屏纹理和效果 pass。
- 2026-07-13 的提交把此前预览面板内的背景采样改为根窗口统一背景，同时预览面板保留一层半透明 overlay。
- 根窗口在第 4.2 节的默认 GPU 策略下可能位于独显，预览复合窗口则位于默认集显；两套窗口并行 present。
- 未启用自定义背景或 blur 为 0 时，不触发模糊 layer。

评估：单张静态图片不会每帧重新解码，但全窗口离屏纹理、模糊和合成仍增加驻留与 fill-rate。它在低显存、双 GPU、OBS 捕获或高分辨率窗口下更有解释力。

> 审查（2026-08-05）：**结论成立，属设置取舍，未改动。**
>
> `QuickShellMain.qml` 的 `smooth` / `mipmap` / `asynchronous` / `cache`、`layer.enabled: visible && appBackgroundBlur() > 0`、`MultiEffect` 的 `blurMax: 64` 均与描述一致，未启用自定义背景或模糊为 0 时不建离屏层也属实。
>
> 这是用户显式开启的外观特性，成本与视觉效果直接绑定，代码侧没有「不改观感就变便宜」的空间——能做的只有关掉或调低模糊值，那是用户的选择。唯一值得记一笔的是 `blurMax: 64` 偏高：`MultiEffect` 的模糊采样代价随 `blurMax` 增长，而不是随实际 `blur` 值，所以即使用户只开了很轻的模糊也在按 64 的规模采样。调低它会改变高模糊档位的观感上限，仍需产品确认，故未动。

### 4.11 波形缓存未命中时读入压缩文件并完整解码为单声道 float（含 BASS 的构建：Windows/macOS）

- 可疑度：中，主要是开谱/换谱阶段
- 日期：2026-06-24
- 版本：`0.5.2-beta4`
- 提交：`12d2d3eb9988d1b26cbe77cb2f5efaed35e851fa`
- 模块：时间轴波形缓存、BASS 离线解码、后台线程池
- 目录：`src/common/WaveformCache.cpp`、`src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- 触发链路：`打开/切换谱面` → `refreshWaveformCache()` → 缓存未命中 → `timelineSlowRefreshPool` worker → `QFile::readAll()` 读完整压缩音频 → `BASS_Init(..., BASS_DEVICE_NOSPEAKER)` → `BASS_StreamCreateFile(..., BASS_STREAM_DECODE | BASS_STREAM_PRESCAN | BASS_SAMPLE_FLOAT)` → 分块解码但持续追加完整单声道 `QVector<float>` → 波形降采样与缓存写入 → CPU/内存/分页/BASS 生命周期与预览竞争

代码事实：

- BASS 路径先把整个压缩文件读入 `QByteArray`，随后再持有完整的 float 解码数组。该分支的条件是 `#ifdef MIACODE_HAS_BASS_AUDIO`（Windows 与 macOS 均成立），不是 Windows 专属；只有无 BASS 的构建才回落到 miniaudio。
- 解码输出按采样率为单声道 float；以 44.1 kHz 估算约 176.4 KB/秒，即一小时约 606 MiB，不含压缩文件副本、临时交错缓冲和最终波形层级。
- 工作在后台线程池而非 GUI 线程，但仍与预览共享 CPU 核、内存带宽、文件缓存和进程地址空间。
- 缓存命中或解码完成后，这一主要峰值结束；它不是稳定整段播放的逐帧工作。

既有审计结合：`WINDOWS_IDLE_FREEZE_POST_V1_0_0_AUDIT_ZH.md` 已把该路径列为 F-01，并确认其静态资源峰值成立。

评估：可解释“新打开长音频谱面时预览先慢、过一段时间恢复”，不充分解释缓存命中后的持续掉帧。

> 审查（2026-08-05）：**机制属实，但「覆盖范围」写窄了；修复方向属取舍，未改动。**
>
> `QFile::readAll()` 读完整压缩文件、`BASS_STREAM_DECODE | BASS_STREAM_PRESCAN | BASS_SAMPLE_FLOAT`、分块解码但持续追加完整单声道 `QVector<float>`，逐条核对无误。
>
> 需要更正的是范围：`decodeMonoSamples()` 里选 BASS 路径的条件是 `#ifdef MIACODE_HAS_BASS_AUDIO`，不是 Windows。macOS 的 BASS 构建走的是同一条全量解码路径。标题与第 3 节表格的「Windows」应改为「含 BASS 的构建（Windows/macOS）」——这一点与第 4.16 节自己写的「Windows/macOS BASS 构建相关」也不一致。
>
> 修复方向（边解码边降采样，不驻留完整 float 数组）是有价值的，但要重排 `WaveformCache` 的解码—降采样结构与多级 level 生成，属取舍，不在本轮低风险范围内。

### 4.12 每预览 tick 查询一次 BASS mixer 播放位置并在 50 ms 偏差时排队重锚

- 可疑度：中低；这是当前审计日新增、尚无性能实测的热路径
- 日期：2026-08-05
- 版本：`1.1.0-beta.7-test.4`
- 提交：`188411d99d9ed048f0bcc1cf520ab1d8e7b3ba00`；`6c0cd3e90f92ceef4c339a6fb0b11b9ad53f2e75`
- 模块：预览 SFX 调度、BASS 音频时钟、输出设备恢复
- 目录：`src/app/mainwindow/sections/timeline/MainWindow.PreviewTick.cpp`、`src/audio/BassPreviewAudioBackend_Transport.cpp`、`src/audio/BassPreviewAudioBackend_PlaybackClock.cpp`、`src/audio/BassPreviewAudioBackendSample.h`、`src/audio/PreviewAudioRecoveryPolicy.h`
- 触发链路：`onQtPreviewTickAtSecond()` → `sfxDrainSecond(second)` → `QtPreviewSfxRuntime::audioClockChartSecond()` → `BassPreviewAudioBackend::audioClockChartSecond()` → `Sample::currentSec()` → `BASS_Mixer_ChannelGetPosition` + `BASS_ChannelBytes2Seconds` → 比较 wall/audio 偏差 → 偏差达到 50 ms 时 `QTimer::singleShot(0)` → 再次查询并 `reanchorPlayingTransportAtChartSecond()` → GUI tick 时间增加

代码事实：

- BGM 正在运行且未到末尾时，每个预览 tick 都查询一次 BASS mixer channel 位置。
- 60/120/180 Hz 分别对应约 60/120/180 次查询/秒；查询发生在 GUI tick 链上。
- 恢复阈值当前为 `kMaxClockDivergenceSeconds = 0.050`，即 50 ms，不是 250 ms。
- 重锚任务会在下一轮事件循环中再次查询前后时钟，并重新 anchor/start transport。
- Linux 或无 BASS 后端时 `audioClockChartSecond()` 返回 false；BGM 尚未运行、pending start 或已到末尾时也不进入 mixer 位置读取。

既有审计结合：早先 `AUDIO_CLOCK_DESYNC_AUDIT_ZH.md` 指出的 Windows/macOS 无输出设备变化恢复问题在当前分支已由 2026-08-05 的重锚链覆盖；本节审计的是这次覆盖新增的热路径成本，而不是重复沿用旧结论。

评估：单次 BASS 查询通常应较小，但它可能触及 mixer 内部同步，且频率随预览目标刷新率线性增加。当前没有实测证明其毫秒量级，故排在中低。

> 审查（2026-08-05）：**结论成立，暂不改动——但值得担心的不是查询成本，是阈值本身。**
>
> `kMaxClockDivergenceSeconds = 0.050`、每 tick 一次 mixer 位置查询、Linux / BGM 未运行 / pending start / 已到末尾时的早退，均与代码一致。
>
> 单次 `BASS_Mixer_ChannelGetPosition` 大概率不构成帧预算问题，本节把它排在中低是合理的。真正需要 Windows 实测回答的是另一个问题：50 ms 是排队重锚的触发线，如果某台机器的 wall/audio 稳态偏差本来就在这个量级附近徘徊，就会变成反复重锚——而每次重锚都要在下一轮事件循环里再查一次时钟并重启 transport。这是一条比查询开销更值得看的失败模式，但它需要 `bgm_delta_ms` 的实际分布才能定，不宜凭静态代码调阈值。建议下一次 Windows 采样时专门统计一下 `action=automatic_reanchor reason=clock_divergence` 的发生频率。

### 4.13 帧节奏诊断为每帧安装计时并对慢帧无条件写日志

- 可疑度：中低，仅诊断配置下成立
- 日期：2026-08-04
- 版本：`1.1.0-beta.7`；日志降量位于 `1.1.0-beta.7-test`
- 提交：`a3a49cf7edc295ee82fcc75f41c29d5b1ab85647`；`59301b458b9c42f06836ccb4a793eb227166527e`
- 模块：QSG render phase profiling、预览 tick profiling、异步日志
- 目录：`src/preview/quick_scene/PreviewQuickSceneRoot.cpp`、`src/app/mainwindow/sections/timeline/MainWindow.PreviewTick.cpp`、`src/common/DebugLog.cpp`、`src/common/DebugOptions.h`
- 触发链路：`MIACODE_PREVIEW_FRAME_PACING_DIAG/--debug` → QQuickWindow 的 before/after sync/render/frameSwapped direct connection → 每帧读取 `QElapsedTimer`、维护 per-layer profile → `total_ms >= 30` 判为 slow frame → 不受普通采样间隔限制地构造 `render_frame_profile` 并入日志队列 → render/GUI 线程额外工作 → 更容易继续成为 slow frame

代码事实：

- 诊断开启后，`updatePaintNode()` 为每个 layer 记录 build time，并在 frameSwapped 汇总 sync、render submit、GPU/vsync 区间。
- 普通样本已按间隔限流，但 `slowFrame` 仍可无条件记录；持续争用时可能连续触发。
- 2026-08-04 的后续提交减少约 70% 常规日志量，降低但没有消除计时、字符串构造、队列互斥和慢帧日志。
- 默认 release、未设置相关诊断环境变量时该 profile 链不运行；第 4.4 节 HUD 提前格式化是另一个独立问题。

评估：它更可能污染诊断测量或放大已有卡顿，而不是普通用户默认配置的根因。

> 审查（2026-08-05）：**结论成立，但收紧它是取舍，建议保持现状。**
>
> `kSlowFrameMs = 30.0`、`if (!slowFrame && !sampleReady) return;`——慢帧确实绕过 `sampleMs` 限流无条件写日志，描述准确。
>
> 不改的理由：给慢帧也加限流（比如每窗口只记一条 + 计一个被抑制的慢帧计数）确实能降低自扰动，但也正好削掉这条诊断存在的意义——它就是为了不漏掉任何一个慢帧。加上这条链默认不运行，代价只落在主动开诊断的人身上。更合适的处理是在读日志时把这条自扰动当已知量，而不是改代码。本节自己的定性（「污染测量」而非「根因」）已经是对的。

### 4.14 预览纹理仓库越界后整代清空并在同一帧重建

- 可疑度：低，突发型
- 日期：2026-07-11
- 版本：`1.1.0-beta.5`
- 提交：`765180e0781a25100204c0434d020b6ed22d76cb`
- 模块：预览 QSG 纹理仓库、场景图 generation
- 目录：`src/preview/quick_scene/PreviewTextureRepository.cpp`、`src/preview/quick_scene/PreviewTextureGenerationPolicy.h`、`src/preview/quick_scene/PreviewQuickSceneRoot.cpp`
- 触发链路：`新 QImage/尺寸/DPR/皮肤内容进入缓存` → cached texture 超过 96 项或 96 MiB，或 fast-key 超过 8192 项 → 下一次 `updatePaintNode()` 的 `resetRequiredBeforeFrame()` 返回 true → `root->resetTextureGeneration()` → 删除整棵 layer 内容和纹理仓库 → 当帧按当前状态重建所有 layer 并重新上传纹理 → 单帧或数帧 hitch、统计 FPS 下降

代码事实：

- 容量策略不是逐项 LRU；任一上限越界会触发整个 texture generation 重置。
- 重置点位于 render-thread `updatePaintNode()` 开头，随后本帧继续构建场景。
- 已有 `CHART_SWITCH_RESOURCE_RELEASE_AUDIT_ZH.md` 的真实切谱测试记录预览纹理通常稳定在约 92 项、32 MiB，零次 reset，说明普通样本没有触发该边界。

评估：机制具备明确的单帧尖峰形态，但既有样本把其普通场景概率降为低。高 DPR、更多资产变体或异常 fast-key 增长仍可能触发。

> 审查（2026-08-05）：**结论成立且定性正确，无需改动。**
>
> `previewTextureGenerationResetRequired()` 确为「任一上限越界即整代重置」而非逐项 LRU，重置点也确实在 render-thread `updatePaintNode()` 开头、当帧继续重建。既有实测（约 92 项 / 32 MiB / 0 次 reset）已把普通场景概率压到低位，本节把它排在第 14 位是准确的。

### 4.15 QuickShell 几何变化与 VideoOutput 绑定定时器形成切换期抖动链

- 可疑度：低，过渡状态相关
- 日期：2026-06-25
- 版本：`0.5.2-beta5`
- 提交：`0fa6b90eaf0d4b4e3232f8306e1e1dfdcd0f13e3`
- 模块：QuickShell 预览布局、内联 surface 激活、视频输出绑定
- 目录：`src/app/quick_shell/qml/QuickShellMain.qml`、`src/app/quick_shell/qml/QuickShellPreviewSurface.qml`、`src/preview/runtime/PreviewStageMediaHost_Media.cpp`
- 触发链路：`根窗口/预览面板宽高变化` → preview pane 自适应布局 → 16 ms inline-surface activation timer → `PreviewQuickSceneRoot`/media item 几何变化 → 0 ms `geometryLogTimer` → `syncVideoOutputBinding()` → 必要时 detach/attach outer 与 inner VideoOutput → sink 重绑和最后视频帧重推 → resize/DPI/fullscreen 过渡期掉帧

代码事实：

- `QuickShellMain.qml` 有 16 ms 的 embedded/fullscreen activation timer；`QuickShellPreviewSurface.qml` 对 x/y/width/height 变化重启 0 ms timer。
- 当前 `syncVideoOutputBinding()` 会比较已附着 host、outer output 和 inner output，仅对象身份真正改变时 attach/detach；稳定几何变化不会无条件重绑。
- 既有 `WINDOWS_IDLE_FREEZE_POST_V1_0_0_AUDIT_ZH.md` 曾把此链列为 F-02，后续审查因幂等判断和缺少稳定状态自反馈证据而降级。

评估：可解释窗口缩放、全屏切换、DPI/显示器迁移和启动布局收敛期间的帧率波动，不支持把它认定为稳定播放中的持续回退。

> 审查（2026-08-05）：**结论成立，降级正确，无需改动。**
>
> 已复核 `QuickShellPreviewSurface.qml`：`syncVideoOutputBinding()` 只在 host / outer output / inner output 三个对象身份真正改变时才 attach/detach，稳定几何变化不会重绑，本节据此降级是对的。
>
> 补一处本节没写的小项：`geometryLogTimer` 每次触发除了调 `syncVideoOutputBinding()`，还会无条件构造 `surfaceGeometryPayload()` 字符串再交给 `logSurface()`。这只在拖拽缩放期间成立，量级远小于本报告前几项，不值得单独改。

### 4.16 BGM 默认使用 BASS_FX tempo 与 compact40 窗口

- 可疑度：低，平台与音频条件相关
- 日期：2026-06-24
- 版本：`0.5.2-beta3`
- 提交：`470d0e4e9923e434ca4e097e405008e3402d0c13`
- 模块：BASS 预览 BGM、BASS_FX SoundTouch tempo
- 目录：`src/audio/BassPreviewAudioBackendImpl.h`、`src/audio/BassPreviewAudioBackendSample.h`、`src/audio/BassPreviewAudioBackend_Assets.cpp`
- 触发链路：`加载 BGM` → `backgroundTrackSpeedMode()` 默认 Tempo → decode stream → `BASS_FX_TempoCreate` → compact40 参数（sequence 40 ms、seek window 15 ms、overlap 8 ms）→ mixer 持续拉取 tempo 输出 → 音频线程 CPU 与缓存压力 → 在低核数/OBS 并发时与 GUI/QSG 调度竞争 → 预览帧率下降

代码事实：

- 默认 BGM speed mode 返回 `Tempo`，默认窗口 preset 为空时选择 compact40。
- tempo stream 在 BGM sample 生命周期内持续参与拉取，即使播放率为 1.0 也保留这条包装结构。
- 该路径没有直接在 GUI/render thread 上绘制；影响只能通过 CPU、调度和可能的音频内部锁竞争传导。
- Windows/macOS BASS 构建相关；其他后端不适用。

既有审计结合：`WINDOWS_IDLE_FREEZE_POST_V1_0_0_AUDIT_ZH.md` 将其列为 F-03，并因缺少直接帧率证据而保持条件性。

评估：可作为低核 CPU 或并发录制时的放大因子，静态证据不足以把它提升为主要预览回退源。

> 审查（2026-08-05）：**结论成立，条件性定性正确，未改动。**
>
> `backgroundTrackSpeedMode()` 无环境变量时返回 `Tempo`、compact40 预设为 sequence 40 ms / seek window 15 ms / overlap 8 ms，均已核对。
>
> 「播放率为 1.0 时仍保留 tempo 包装」是可以优化的（1.0 时直通 decode stream），但那会改变切换播放率时的流生命周期——需要在切速时重建包装，而重建点正好落在播放中，属取舍，未动。

### 4.17 时间轴纹理缓存跨谱面增长，但已有实测未显示帧时间恶化

- 可疑度：低，且被现有实测明显降级
- 日期：增长路径早于当前审计；2026-06-27 的皮肤跟随扩大 key 来源；2026-08-05 增加容量逃逸阈值
- 版本：皮肤跟随位于 `1.0.1-beta.2`；当前阈值提交为 `1.1.0-beta.7-test.4`
- 提交：`aa6f8e44`；`78ca9488b8390c447c111fd7d68463bda81f5c28`；`ca8aa41fa6b24889b2442275739da4b951924e2a`
- 模块：时间轴 QSG 纹理、旋转 slide arrow、文字/hold pixmap 缓存
- 目录：`src/timeline/quick/TimelineQuickTextureCache.cpp`、`src/timeline/quick/TimelineQuickTextureCachePolicy.h`、`src/timeline/quick/TimelineQuickItem.cpp`
- 触发链路：`反复切谱/切皮肤/改变缩放或 DPR` → 新文字、音符和量化到 0.1° 的 slide-arrow rotation key → CPU pixmap 与 QSG texture 跨谱面保留 → 驻留量逐步增加 → 当前达到 8192 项或 64 MiB 逃逸阈值时删除 node tree 并 `invalidateAll()` → 极端会话出现重建尖峰

代码事实：

- 当前注释记录 slide-arrow 旋转约每次换谱新增 30 个 key，旧缓存没有容量边界。
- 2026-08-05 的最终阈值是 8192 texture entries、8192 transformed pixmaps 和 64 MiB texture bytes，定位为 runaway guard，不期望正常触发。
- `CHART_SWITCH_RESOURCE_RELEASE_AUDIT_ZH.md` 的 20 次切谱实测得到约 360 条目、约 1.3 MiB；`updatePaintNode` 平均时间从 0.334 ms 降至 0.033 ms，未观察到缓存规模导致的帧时间恶化。
- 因缓存主体已存在于代理基线附近，本项不是纯粹由 beta9 之后新建的完整机制；增量主要来自更多皮肤/几何 key 来源和当前极端全清分支。

评估：它是长期驻留与极端 flush 风险，不是现有证据支持的普通预览 FPS 回退根因。

> 审查（2026-08-05）：**结论成立，但本节内部有一处数字自相矛盾，需以代码为准。**
>
> 触发链路一段写的是「当前达到 8192 项或 **512 MiB** 级逃逸阈值」，而同节「代码事实」写的是 64 MiB——后者才对：`TimelineQuickTextureCache.cpp` 的 `kTimelineCachedTextureByteLimit = 64LL * 1024 * 1024`。512 MiB 这个数字在代码里不存在，应删除或改为 64 MiB。
>
> 其余结论（8192 texture entries / 8192 transformed pixmaps、定位为 runaway guard、既有实测约 360 条目 ≈ 1.3 MiB 且 `updatePaintNode` 均值从 0.334 ms 降到 0.033 ms）均与代码和既有审计一致，无需改动。

### 4.18 Touch 创作悬停增加输入命中与一个 QSG 图层

- 可疑度：很低，交互条件严格
- 日期：2026-07-13；2026-07-20 扩展 pressed 状态与偏好持久化
- 版本：`1.1.0-beta.5`
- 提交：`4c53c1c785c69e9ac3032ea415b7a43ab1ff28ea`；`2e7460efd50b6cfb6b05dc8574cb8beacc3d76d3`
- 模块：Touch 区域创作、鼠标命中、预览 hover layer
- 目录：`src/core/scene/TouchPadAuthoringState.h`、`src/preview/quick_scene/PreviewQuickTouchHoverLayer.cpp`、`src/preview/quick_scene/PreviewQuickSceneRoot.cpp`、`src/preview/runtime/PreviewRuntime.cpp`
- 触发链路：`暂停预览` + `Touch 创作快捷状态开启` → `PreviewQuickSceneRoot` 接收 hover/mouse event → 把 item 坐标转换为逻辑坐标 → 遍历 A/B/D/E 四圈各 8 区和中心区做命中 → 更新 hovered/pressed pad 到 frame state → 发布快照/请求更新 → `touch_hover` QSG slot 生成额外半透明节点

代码事实：

- scene root 从该提交起常驻 `setAcceptHoverEvents(true)` 和鼠标按钮接收。
- 真正的 pad 命中在 `touchPadAuthoringEnabled` 为 true 时才进行；当前 UI 通过暂停且 Ctrl hold/创作上下文控制该状态。
- hover layer 没有 hovered pad 时直接返回空节点；启用时通常只绘制一个区域提示。

评估：它不会解释普通播放期间的持续帧率下降，只可能影响暂停创作交互或鼠标高频移动期间的局部刷新。

> 审查（2026-08-05）：**结论成立，无需改动。**
>
> `setAcceptHoverEvents(true)` 常驻、真正的 pad 命中受 `touchPadAuthoringEnabled` 门控、hover layer 无 hovered pad 时直接返回空节点，三点均已核对。

### 4.19 分区 HUD 字体使每次 HUD 重绘额外构造 chart-info 字体路径

- 可疑度：很低
- 日期：2026-07-13
- 版本：`1.1.0-beta.5`
- 提交：`b49b539d2cc5d594f7e09d252acf45ef4c655253`
- 模块：HUD 字体选择、QPainter HUD
- 目录：`src/core/scene/PreviewHudState.cpp`、`src/preview/quick_scene/PreviewQuickHudLayer.cpp`
- 触发链路：`HUD 约 10 Hz 重绘` → `paintPreviewHudOverlay()` → 分别构造 timestamp 与 chart-info `QFont` → `customHudFontFamily(area)` 哈希查询/必要时字体加载 → `QFontInfo` 验证/fallback → 不同 area 可能使用不同字体与字形缓存 → HUD QImage/texture 更新

代码事实：

- 自定义字体加载结果按 area 缓存，不会每次重新从文件加载。
- 当前 overlay 在进入 debug/chart-info 分支前就无条件构造 timestampFont 和 chartInfoFont，即使 chart-info HUD 关闭。
- 默认嵌入字体 family 是静态初始化，但每次仍构造 `QFont`，并可能执行 `QFontInfo(font).family()` 验证。
- HUD 重绘本身与 100 ms 节流早于比较窗口；本项增量主要是第二套 area 字体选择，而第 4.4 节的诊断详情字符串成本更大。

评估：属于可确认但规模很小的固定增量，不足以单独解释显著 FPS 回退。

> 审查（2026-08-05）：**结论成立，已修（`cd3c446a`）。**
>
> 「在进入 debug/chart-info 分支前就无条件构造 timestampFont 和 chartInfoFont」属实。规模比本节估计的略大一点：`previewHudTimestampFontForArea()` 除了按 area 查自定义字体族，还会对内嵌 fallback 走一次 `QFontInfo(font).family()` 解析——这不是纯粹的 `QFont` 构造。chart-info HUD 默认关闭，所以这一路每次 HUD 重绘都白跑。改为在自己的分支内按需构造。

### 4.20 自定义轮廓暂停区合成增加逐像素亮度处理和多次 QPainter 合成

- 可疑度：很低，加载期
- 日期：2026-07-05；2026-07-13 调整合成内容
- 版本：首次合成位于 `1.0.1-beta.5-test`；后续调整位于 `1.1.0-beta.5`
- 提交：`472de9fd`；`e3cbe381`
- 模块：预览轮廓资产加载、暂停判定区合成
- 目录：`src/preview/runtime/PreviewSceneAssetLoader.cpp`、`src/preview/runtime/PreviewSceneAssetRepository.cpp`
- 触发链路：`选择自定义轮廓/暂停判定区 composite` → 后台 asset repository reload → 加载 custom outline、judge area、default outline、labels overlay → `brightnessAdjustedImage()` 逐像素处理 labels → 多个目标尺寸 `QPainter::drawImage` 与 DestinationOut 合成 → 新资产状态发布/纹理更新 → 皮肤或轮廓切换阶段短暂资源峰值

代码事实：

- 逐像素循环发生在资产构建时，不在每个预览帧执行。
- 合成结果作为单个 `QImage` 存入 asset state，后续由纹理仓库复用。
- 只有 `PausedJudgeAreaComposite` 模式和有效自定义资源触发完整路径。

评估：能形成轮廓切换或资源重载瞬间的 CPU/上传峰值，不具备持续 FPS 回退形态。

> 审查（2026-08-05）：**结论成立，无需改动。**
>
> `brightnessAdjustedImage()` 的逐像素循环确实只在资产构建时运行，合成结果作为单个 `QImage` 存入 asset state 后由纹理仓库复用，不进入每帧路径。

## 5. 已有审查结论的合并与降级项

### 5.1 PV 播放结束自动暂停属于历史问题，当前已修复，不是当前持续低 FPS 原因

- 可疑度：当前排除；历史上可表现为“FPS 归零/预览停住”
- 日期：2026-07-22 修复
- 版本：`1.1.0-beta.6`
- 提交：`5e77fc72419e6f14ca4fb7b6ef5f02ba5cb3bdad`
- 模块：舞台背景视频结束、预览 transport
- 目录：`src/preview/runtime/`、`src/app/mainwindow/sections/preview/`、`src/app/mainwindow/sections/timeline/`
- 触发链路：历史路径为 `短 pv.mp4 到达 EndOfMedia` → stage media 发出 playbackFinished → transport 被自动暂停 → 用户观察为预览停止；当前路径已把背景视频结束与主 transport 解耦

结合结论：工作区现有 `PREVIEW_AUTO_PAUSE_INITIAL_DIAGNOSIS_ZH.md` 已定位并记录该问题；当前提交历史包含明确修复。因此它不应与“当前仍在播放但 FPS 较低”混为一谈。

> 审查（2026-08-05）：**复核通过。** 排除依据成立，且这个区分很重要——“FPS 归零/预览停住”与“仍在播放但帧率低”是两类现象，把前者混入本报告会污染排序。

### 5.2 默认实时预览不走 DComp，也不执行默认逐帧 QVideoFrame::toImage

- 可疑度：当前默认路径排除
- 日期：DComp 实验路径早于代理基线；当前架构审计日期 2026-08-05
- 版本：当前 `1.1.0-beta.7-test.4`
- 提交：多个早期 DComp 提交；比较窗口内没有把 DComp 改为默认的提交
- 模块：实时预览渲染架构、舞台媒体 fallback
- 目录：`src/render/backend_d3d11/`、`src/preview/runtime/`、`src/preview/quick_scene/`、`src/common/DebugOptions.h`
- 触发链路：默认 `QSG main path` → `PreviewQuickSceneRoot` + `PreviewStageMediaItem`；只有显式 DComp 环境开关/回退组合才进入 DComp CPU 图像路径

结合结论：仓库开发指南和当前代码均把 in-process QSG 定义为主路径，DComp 默认关闭且正在解耦。视频硬件帧默认通过 `QVideoSink/VideoOutput` 保持 GPU handle；`toImage()` 被限制到特定 DComp fallback。因此不能把默认掉帧归因于一个并不存在的常态 GPU→CPU 拷贝。

> 审查（2026-08-05）：**复核通过。** 排除依据在 `handleDecodedVideoFrame()` 里可直接确认：`needsCpuImageForDComp` 同时要求 `previewUseDCompEnabled()` 且 `!previewDCompPerPixelAlphaEnabled()`，默认配置两个条件都不成立。该处注释还记录了为什么这条不能反向放宽——在 GUI 线程对 D3D11VA 硬件帧调 `toImage()` 会映射一块解码线程仍在回收的 surface，曾在 Intel iGPU 上造成 use-after-free 崩溃。

### 5.3 预览/时间轴默认刷新率配置本身没有从代理基线发生方向性下降

- 可疑度：作为 beta9 后直接回归原因排除
- 日期：代理基线 2026-06-16；当前 2026-08-05
- 版本：代理基线至 `1.1.0-beta.7-test.4`
- 提交：比较窗口内未发现改变这些默认值的有效提交
- 模块：预览画布、PV、时间轴帧率偏好
- 目录：`src/app/mainwindow/MainWindowMemberStorage.inc`、`src/app/mainwindow/sections/timeline/MainWindow.TimelineFramePacing.cpp`
- 触发链路：`加载默认偏好` → canvas `DisplayRefresh`、PV `30 FPS`、timeline `DisplayRefresh` → 计时器/呈现调度

结合结论：代理基线和当前代码的主要默认模式一致。当前 `previewCanvasUsesFrameSwappedPacing()` 固定返回 false，注释所述 present-driven gate 的启用与撤销发生在代理基线之前；比较窗口内主要是文件拆分和诊断扩展。因此“默认 FPS 选项被直接改低”没有代码证据。

> 审查（2026-08-05）：**复核通过。** `previewCanvasUsesFrameSwappedPacing()` 确为固定 `return false`，其注释记录的撤销理由（把播放 tick 耦合到 frameSwapped，导致任何渲染打嗝直接拖停播放时钟）也正是第 4.1 节路线 (a) 需要引以为戒的先例。

### 5.4 切谱资源释放总体正确，未形成可重复的预览纹理泄漏证据

- 可疑度：低，现有实测不支持
- 日期：已有专项审计 2026-08-05 前后
- 版本：当前 `1.1.0-beta.7-test.4`
- 提交：`765180e0` 及后续资源治理提交
- 模块：音频、视频、预览 QSG、时间轴 QSG、谱面切换
- 目录：`src/audio/`、`src/preview/`、`src/timeline/quick/`、`src/app/mainwindow/sections/preview/`
- 触发链路：`切换谱面` → 关闭旧媒体/音频与代际更新 → 新状态/资产发布 → 纹理缓存复用或按边界失效

结合结论：`CHART_SWITCH_RESOURCE_RELEASE_AUDIT_ZH.md` 对主要生命周期逐项检查后，确认视频/音频/QSG 所有权大体正确；预览纹理缓存正常样本稳定，时间轴缓存虽增长但命中改善且未导致 paint time 上升。故“每换一张谱就稳定泄漏并持续压低预览 FPS”目前没有证据。

> 审查（2026-08-05）：**复核通过。** 本轮第 4.8 节的内圈 sink 修复与这一节相邻但不冲突：那不是所有权泄漏，而是一个正确释放、只是在用不到它的模式下也照样持有帧的消费者。既有审计对生命周期正确性的结论不受影响。

### 5.5 日志系统重构整体不是默认回退源，只有特定热路径存在提前构造或诊断自扰动

- 可疑度：整体低；第 4.4、4.6、4.13 节为具体例外
- 日期：2026-06-19 日志重构；2026-08-04 诊断扩展
- 版本：`0.5.0-beta16` 之后
- 提交：`e4f3a944`、`a3a49cf7`、`59301b45`
- 模块：运行时日志、异步 writer、诊断通道
- 目录：`src/common/DebugLog.cpp`、`src/common/DebugOptions.h` 及各调用方
- 触发链路：`调用方构造 payload` → debug option/channel gate → 异步队列/文件写入

结合结论：日志 writer 已异步化并有降量提交，默认关闭的通道通常快速返回；不能把所有新增日志语句等同于文件 I/O。真正仍进入热路径的部分，是调用方在 gate 之前就完成昂贵 payload 构造，或诊断开启后对慢帧/文本执行高频记录和 flush，已分别列入详细结果。

> 审查（2026-08-05）：**复核通过，且这一节的定性是本报告最有实操价值的一条。**
>
> 「gate 在 payload 之后」这个模式确实是本轮唯一被直接修掉的一类问题——第 4.4、4.6 三处（HUD diag detail、chart-info 字体、prepared-cache HS 直方图）加上第 4.5 的扩展事件 payload，都是同一个形状：调用方先把字符串/容器建好，被调用方才去看开关。它们共同的特点是修复无损、可验证、不涉及任何取舍，因此值得在后续审计里作为独立的检查项固定下来。
>
> 一个可复用的判据：凡是「gate 函数在被调函数体内」的日志/诊断辅助，都要检查调用方是否已经付了构造成本；`DebugLog.cpp` 里 `skipAsyncLogFlush()` 的缓存注释是这类问题的既有正解范例。

## 6. 触发条件交叉矩阵

| 条件 | 会叠加的高/中可疑项 |
|---|---|
| 所有实时预览 | 4.1 帧状态快照；4.4 HUD 提前格式化；4.5 扩展 JSON 基础构造 |
| Windows 混合显卡 | 4.2 双适配器 Quick 窗口 |
| Windows + PV/BG 视频 | 4.3 D3D11VA 两设备桥；可能叠加 4.2 |
| Windows + PV/BG + 内圆适配外部填充 | 4.3 + 4.8；混合显卡时再叠加 4.2 |
| OBS/浏览器硬件加速并发 | 放大 4.2、4.3、4.10；CPU 并发也可放大 4.11、4.16 |
| Touch 密集谱面 | 4.7；若 Starry Tap 同时密集再叠加 4.9 |
| 刚启动播放/刚载入皮肤 | 4.6；可能同时遇到 4.11、4.20 |
| 自定义背景且开启模糊 | 4.10 |
| 启用扩展且订阅预览位置 | 4.5 的完整 JS 回调链 |
| 开启帧节奏/HUD 诊断 | 4.4 诊断 flush + 4.13 render/tick profiling |
| 高刷新率 120/180 Hz | 线性放大 4.1、4.5、4.12；缩短所有 GUI/render 工作的单帧预算 |
| 长会话反复切谱/皮肤/缩放 | 4.17 驻留增长；极端时 4.14/4.17 全量重建尖峰 |

## 7. 与版本时间线的对应

| 日期 | 内部版本 | 提交 | 与预览 FPS 相关的审计变化 |
|---|---|---|---|
| 2026-06-16 | beta9 代理边界 / 下一提交 `0.5.0-beta10` | `92194b3d` / `037654a0` | 审计比较起点 |
| 2026-06-18 | `0.5.0-beta10` | `04f95813` | D3D11VA H2 与两设备桥进入预览 |
| 2026-06-18 | `0.5.0-beta16` | `2d22f22b` | 硬件解码成为默认偏好，移除集显自动软件解码 |
| 2026-06-19 | `0.5.0-beta16` | `688e97b1` | 烟花预热等待确认绘制并随 playhead recenter |
| 2026-06-24 | `0.5.2-beta3` | `470d0e4e` | BGM 默认 compact40 tempo |
| 2026-06-24 | `0.5.2-beta4` | `12d2d3eb` | Windows 波形 BASS 全量解码路径 |
| 2026-06-25 | `0.5.2-beta5` | `0fa6b90e` | QuickShell 预览启动尺寸/激活定时链 |
| 2026-07-04 | `1.0.1-beta.5-test` | `41cf08a8` | 高性能 GPU 绑定默认开启 |
| 2026-07-05 | `1.0.1-beta.3` | `8a4085b4` | 完整帧状态快照；HUD 诊断包装 |
| 2026-07-05 | `1.0.1-beta.5-test` | `ebc18174` | 内圆 fit/外部 fill 双视频输出和遮罩 |
| 2026-07-11 | `1.1.0-beta.5` | `765180e0` | 预览纹理 generation 容量重置策略 |
| 2026-07-13 | `1.1.0-beta.5` | `6c258a06` | 全窗口应用背景与模糊合成 |
| 2026-07-13 | `1.1.0-beta.5` | `86e81759` | Touch 判定精灵 9→17 |
| 2026-07-13 | `1.1.0-beta.5` | `b49b539d` | 分区 HUD 字体 |
| 2026-07-15 | `1.1.0-beta.5` | `15f6e6eb` | Starry/DX 判定特效 |
| 2026-07-20 | `1.1.0-beta.5` | `2e7460ef` | 快照原子实现调整；Touch pressed 状态 |
| 2026-07-22 | `1.1.0-beta.6` | `5e77fc72` | 修复 PV 结束导致 transport 自动暂停 |
| 2026-08-03 | `1.1.0-beta.7` | `35f6ad90` | 统一扩展事件总线与逐 tick 预览位置事件 |
| 2026-08-04 | `1.1.0-beta.7` | `a3a49cf7` | 争用型帧节奏诊断 |
| 2026-08-05 | `1.1.0-beta.7-test.4` | `188411d9` / `6c0cd3e9` | SFX 改用 BASS 音频时钟并增加自动重锚 |
| 2026-08-05 | `1.1.0-beta.7-test.4` | `78ca9488` / `ca8aa41f` | 时间轴纹理缓存增加高阈值 runaway guard |

## 8. 审计覆盖文件与既有材料

本报告交叉检查了以下主要代码区域：

- `src/preview/runtime/`
- `src/preview/quick_scene/`
- `src/core/scene/`
- `src/app/quick_shell/`
- `src/app/mainwindow/sections/timeline/`
- `src/app/mainwindow/sections/preview/`
- `src/app/gpu_device_provider.cpp`
- `src/audio/`
- `src/timeline/quick/`
- `src/extensions/`
- `src/common/WaveformCache.cpp`
- `third_party/QtAVPlayer/src/QtAVPlayer/`

合并的已有审查/诊断材料包括：

- `docs/audit/AUDIO_CLOCK_DESYNC_AUDIT_ZH.md`（从 `origin/dev` 审阅）
- `docs/audit/OBS_CONTENTION_PLAYBACK_STUTTER_AUDIT_ZH.md`（从 `origin/dev` 审阅）
- `docs/audit/WINDOWS_IDLE_FREEZE_AUDIT_REVIEW_ZH.md`（从 `origin/dev` 审阅）
- `docs/audit/WINDOWS_IDLE_FREEZE_POST_V1_0_0_AUDIT_ZH.md`
- `docs/audit/CHART_SWITCH_RESOURCE_RELEASE_AUDIT_ZH.md`
- 工作区现有未跟踪材料 `docs/audit/PREVIEW_AUTO_PAUSE_INITIAL_DIAGNOSIS_ZH.md`

## 9. 最终审计判断

从 beta9 代理边界到当前版本，最可能造成“普遍、持续、刷新率越高越明显”的代码级回退是第 4.1 节的帧状态快照放大；最可能造成“特定 Windows 机器显著下降，尤其与 OBS/PV/混合显卡共同出现”的是第 4.2 和 4.3 节的双适配器 Quick 窗口与 D3D11VA 两设备视频桥。第 4.4 和 4.5 节属于默认路径上的额外 CPU 分配/格式化，其中扩展运行时和 HUD 诊断开关能进一步放大。其余项目主要解释谱面密度、视觉样式、背景设置、开谱阶段或诊断状态下的局部下降和瞬时抖动。

已有专项审计同时排除了几种容易混淆的表象：PV 到尾自动暂停已修复；默认 DComp/默认逐帧 `toImage()` 不成立；默认帧率选项没有方向性下调；普通切谱样本没有显示预览纹理泄漏或时间轴缓存导致帧时间持续恶化。

## 10. 逐条审查回执（2026-08-05）

对第 4、5 两节的每个条目做了代码复核，判定分四类：**已修**（结论成立且修复无损、可验证）、**取舍**（结论成立但修复涉及产品/结构决策，需实测或产品确认）、**待触发**（代码描述成立，但当前构建或目标机器不满足触发条件，暂不占排查预算）、**通过**（结论成立且当前不需要动作）。**没有条目被判定为误报**——20 条详细结果与 5 条合并结论描述的代码事实全部与当前分支一致。

第 4.2 节在 2026-08-04 实测日志（`logs`、`logs 2`）到手后由「取舍」改判为「待触发」：那两次会话中两个 surface 的绑定都被跳过，且独立合成窗口在当前代码里被 `quickShellPreviewUsesSeparateSurface()` 硬编码关闭，从未上屏。详见该节。

已落地的代码修复在提交 `cd3c446a`：Release 构建通过，ctest 43/46；3 个失败（`oplog_self_test`、`plain_code_editor_spec`、`preview_firework_lifecycle_spec`）在未改动的分支头上同样复现，与本次改动无关。

| 条目 | 判定 | 说明 |
|---|---|---|
| 4.1 帧状态快照放大 | 取舍 | 结论与计数全部核实。合并发布（路线 a）与收窄快照（路线 b）都不属低风险；建议先用 (a) 做 Windows A/B 取数 |
| 4.2 高性能 GPU 默认绑定 | **待触发** | 过期注释已改写；但实测日志与代码复核都表明双适配器结构当前不会发生，见该节。默认值无需改动 |
| 4.3 PV 默认硬解 + 两设备桥 | 取舍 | 三点事实无误；H2 已标注 RESERVED，集显软解默认刚被移除，任一方向都要 iGPU 验收 |
| 4.4 HUD 诊断提前格式化 | 已修 | 惰性 detail + 开关单次读取；报告漏算了未缓存的环境变量读取 |
| 4.5 扩展事件总线每 tick JSON | 已修 | 新增订阅预检；报告低估了无订阅时 `dispatchEvent()` 的 enrichment 成本 |
| 4.6 烟花预热触发全量重建 | 已修（HS 直方图）/ 取舍（重建本身） | 直方图字符串已前置门控；prepared cache 全量失效属结构性取舍 |
| 4.7 Touch 判定精灵 9→17 | 取舍 | 数字属实，但属刻意的视觉升级；优化方向应是合批而非减量 |
| 4.8 内圆模式双 sink | 已修（越模式推帧）/ 取舍（模式 3 合成） | 非模式 3 不再向内圈 sink 推帧，避免多钉一块解码池 surface |
| 4.9 Starry 判定星光 | 取舍 | 同 4.7；非默认样式 |
| 4.10 全窗口背景模糊 | 取舍 | 用户显式开启的外观特性；另注意 `blurMax: 64` 决定采样规模而非实际模糊值 |
| 4.11 波形全量解码 | 通过（需更正范围） | 覆盖范围应从「Windows」改为「含 BASS 的构建（Windows/macOS）」 |
| 4.12 每 tick BASS 时钟查询 | 取舍 | 查询成本次要；真正要看的是 50 ms 重锚阈值是否偏紧，需 `bgm_delta_ms` 分布 |
| 4.13 慢帧无条件写日志 | 取舍 | 收紧会削弱诊断本身的意义；默认不运行，建议保持现状 |
| 4.14 纹理仓库整代重置 | 通过 | 机制与定性均正确 |
| 4.15 QuickShell 几何抖动链 | 通过 | 幂等判断已核实，降级正确 |
| 4.16 BASS_FX tempo | 取舍 | 1.0 倍速直通可优化，但会动到切速时的流生命周期 |
| 4.17 时间轴纹理缓存增长 | 通过（需更正数字） | 触发链路写的「512 MiB」在代码中不存在，应为 64 MiB |
| 4.18 Touch 创作悬停 | 通过 | — |
| 4.19 分区 HUD 字体 | 已修 | chart-info 字体改为按需构造 |
| 4.20 轮廓暂停区合成 | 通过 | 确认只在资产构建时运行 |
| 5.1–5.5 合并与降级项 | 通过 | 5.2 / 5.3 的排除依据已在代码中逐点确认 |

两处事实性错误已直接在正文更正：

1. 第 3 节表格与第 4.11 节标题原写「Windows 波形缓存」，实际 `decodeMonoSamples()` 的分支条件是 `#ifdef MIACODE_HAS_BASS_AUDIO`，Windows 与 macOS 都走同一条全量解码路径（与第 4.16 节自己的「Windows/macOS BASS 构建相关」也不一致）。已改为「含 BASS 的构建（Windows/macOS）」。
2. 第 4.17 节触发链路原写「512 MiB 级逃逸阈值」，代码中不存在这个数字；`kTimelineCachedTextureByteLimit = 64LL * 1024 * 1024`，与同节「代码事实」一致。已改为 64 MiB。

另有一处措辞可以写得更死，未改正文、记在此处：第 4.1 节把视觉平滑那次发布描述为条件性的（“视觉平滑时间不同于音频时间时再发布一次”），实际它必然发生——`previewVisualLookaheadVsyncs()` 默认 1.0 而非 0，`visualSecond` 恒不等于 `second`，所以稳态就是每 tick 4 次 + 每呈现 1 次，不存在「只有 4 次」的常见情形。
