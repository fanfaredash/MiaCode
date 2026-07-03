# GPU 渲染设备策略与 QRhi 导出计划

## 目标

最终目标：

- 默认走高性能渲染与高性能导出。
- 第一阶段先做出稳定可用、可回退、debug 可见的默认高性能链路；用户 UI 和精细交互后置。
- 后续再允许用户配置 MiaCode 运行在核显、独显或指定 GPU 设备上。
- 实时谱面渲染与导出使用同一套设备策略；为此新增一条 QRhi/D3D11 导出链路，并保留当前 OpenGL 导出作为回退与对照。
- debug 日志能够明确回答“当前进程是谁、Qt Quick 用了什么 RHI、实际绑定了哪个 GPU adapter、导出走哪条链路”。
- 评估并持续验证当前默认 QuickShell 线路与隐藏 `MainWindow` 后端并存的风险。

本文是调研后的实施计划，不代表所有接口细节已锁定。QRhi 导出链路仍需要先做 API spike 和小样例验证。

## 当前事实

### Windows 包入口

Windows release 包根目录的 `MiaCode.exe` 是 launcher，真正的 GUI/导出 worker 程序在 `app\MiaCode.exe`。

证据：

- `scripts/build/package-win.ps1` 将真实 app 拷贝到 `app\MiaCode.exe`，再把 `MiaCodeLauncher.exe` 拷贝为包根目录 `MiaCode.exe`。
- `src/wrapper/MiaCodeLauncher.cpp` 通过 `CreateProcessW` 启动 `app\MiaCode.exe`。

影响：

- Windows 图形设置和 NVIDIA 控制面板如果只配置包根目录 `MiaCode.exe`，很可能只命中 launcher，不命中真正渲染的 `app\MiaCode.exe`。
- 后续文档、日志和 UI 都必须明确显示真实 exe 路径。
- 高性能 GPU 导出符号应同时放在 launcher 和真实 app 上；真正决定渲染进程的是 `app\MiaCode.exe`。

### GUI RHI 选择

当前 GUI 不再强制 OpenGL：

- `src/app/main.cpp` 中 CLI 导出和导出 worker 会强制 `QSGRendererInterface::OpenGL`。
- GUI 启动会读取 `--rhi=<name>`、持久化配置或平台默认后端。
- `src/app/graphics_backend.cpp` 支持 `d3d11`、`d3d12`、`opengl`、`vulkan`、`metal`、`software`。
- 因此当前前端默认不是 MiaCode 显式强制 D3D11，而是 `platform_default`；在 Windows / Qt 6 Quick 场景下通常会解析为 Direct3D11。`QuickShellPreviewCompositeSurface.cpp` 也按这个假设处理 `QQuickWindow::graphicsApi() == Unknown` 的启动阶段。

已有日志：

- `startup/graphics_backend` 可以看到应用到 Qt Quick 的后端选择来源。
- `preview/quick_scene action=rhi_backend` 可以看到运行时 scene graph 报告的 `graphics_api`。

限制：

- 这些日志能判断“RHI/API 路径”，不能可靠判断实际 GPU adapter 是核显还是独显。
- `startup_diagnostics_win32.cpp` 的 D3D11 adapter 探针是单独创建的默认 D3D11 设备，不等价于 Qt Quick 的实际 RHI device。
- `preview/stage_media action=media_backend` 中的 adapter 字段来自枚举/启发式，不等价于 Qt Quick 实际 device。
- 当前缺少 OpenGL `GL_VENDOR` / `GL_RENDERER` / `GL_VERSION` 导出日志。

### 导出渲染路径

当前导出强制 OpenGL：

- `src/app/main.cpp` 对 `--export-video` / `--export-video-worker` 强制 `QQuickWindow::setGraphicsApi(OpenGL)`。
- `src/preview/runtime/PreviewQuickExportSession.cpp` 创建 `QOffscreenSurface`、`QOpenGLContext`、`QQuickRenderControl`。
- 导出窗口通过 `QQuickGraphicsDevice::fromOpenGLContext(context_)` 绑定到 OpenGL context。
- 渲染目标是 `QOpenGLFramebufferObject`，读回通过同步 `glReadPixels` 或 PBO。

历史原因：

- `33df87d2` 引入 Qt Quick runtime/export，移除了旧 `PreviewCanvas` / `PreviewGLRenderer`，新增 `PreviewQuickExportSession` 和 `VideoExportQuickRenderBackend`。
- `7ee78ab2` 将 GUI 改为平台默认 RHI，但保留 CLI export / worker 强制 OpenGL。
- `3b9aec92` 修复 DComp exclusive 下导出空谱面，说明导出是 `QQuickRenderControl + framebuffer` 的离屏路径，没有 DComp popup 可用。
- `de8cc9ed`、`b2551e0c` 等提交围绕 OpenGL FBO/PBO/readback 做过稳定性修复，说明当前导出链路已经深度绑定 GL readback 细节。

结论：

- “导出强制 OpenGL”不是历史遗留文档误写，而是当前实现的硬依赖。
- 不能简单把导出 worker 改成 `d3d11`；必须新增 D3D11/QRhi 导出 session，并保留 OpenGL 作为回退。
- 这也意味着当前 GUI 前端和导出并不对齐：GUI 多数 Windows 设备上会走 Qt 默认 D3D11，导出仍固定 OpenGL。

### QuickShell 与隐藏 MainWindow

当前默认 GUI 线路是 QuickShell + 隐藏 `MainWindow` 后端。

证据：

- `src/app/main.cpp` 当前 GUI 路径直接创建 `QuickShellBootstrap`；`--quick-shell-beta` 只影响 DComp env 的自包含开启逻辑。
- `QuickShellBootstrap::start()` 构造 `MainWindow(true)`，调用 `setQuickShellBackendActive(true)`，随后 `hide()` / `setVisible(false)`。
- `MainWindow` 构造时设置 `WA_DontShowOnScreen`、`WA_NativeWindow` 并创建 native window id。
- `QuickShellNativeSurfaceHost` 创建桥接用 `Qt::Tool` / frameless native surfaces，并把旧 `MainWindow` 的菜单栏、工具栏、状态栏、侧栏、workspace、bottom tabs 等 QWidget rehost 到 QuickShell。
- `MainWindow::PreviewSection::previewStageMediaRoute()` 当前固定返回 `QuickShellStageHost`。

风险判断：

- 这不是两个普通主窗口同时展示，但确实是“一个 QML root window + 一个隐藏 QMainWindow 后端 + 多个 native bridge surface / preview surface”的混合拓扑。
- 风险可控，但必须纳入设备策略验证，因为每个 `QQuickWindow` / `QQuickView` 都可能创建或导入自己的 QRhi device。
- 现有 `PreviewSharedD3D11Device` 只覆盖 quickshell video composite preview 的 `QQuickView`，不等价于根 QuickShell window、timeline quick scene、导出 worker 全部共享同一 device。
- 关闭、focus、dialog parent、native surface reparent 都已有大量日志和专项修复；新增 GPU 策略时不能破坏这些生命周期假设。

## 概念关系

- GPU 设备：Windows/DXGI 层面的物理或逻辑 adapter，例如 Intel iGPU、NVIDIA dGPU。
- D3D11：显式创建 `ID3D11Device` 的渲染 API；可以通过 adapter LUID 或 `IDXGIAdapter` 明确选择 GPU。
- OpenGL：Qt/driver 管理 context；在 Windows 双显卡环境下通常更依赖 OS/驱动策略和 vendor hint，应用内精确指定 adapter 较难。
- Qt RHI / QRhi：Qt Quick scene graph 的后端抽象，可运行在 D3D11、D3D12、OpenGL、Vulkan、Metal、Software 等 API 上。
- `QQuickGraphicsDevice`：Qt Quick 允许应用把已有 device 或 adapter 交给某个 `QQuickWindow` 的公开入口。Qt 6.8 提供 `fromAdapter(luidLow, luidHigh)`、`fromDeviceAndContext(...)`、`fromOpenGLContext(...)`。
- `QQuickRenderTarget`：Qt Quick 离屏 render target 入口。Qt 6.8 提供 `fromD3D11Texture(...)` 和 `fromOpenGLTexture(...)`，这使 D3D11 QRhi 导出链路具备可行基础。

## 目标架构

引入共享的渲染设备策略层，实时预览和导出 worker 都消费同一份策略。近期目标不是先做设置页，而是让默认策略足够稳定：Windows 上优先 D3D11 高性能 adapter，失败时自动回退，并且每一步都有日志。

第一阶段内部策略建议值：

- `auto_high_performance`：默认。Windows 上优先 `IDXGIFactory6::EnumAdapterByGpuPreference(HIGH_PERFORMANCE)` 或等价高性能 adapter。
- `platform_default`：回退。让 Qt/OS 按平台默认创建 RHI device。
- `adapter_luid`：工程验证入口。用于 A/B 指定某个 DXGI adapter，不先做用户 UI。
- `software`：诊断/兼容回退。

策略消费方：

- GUI 进程启动：决定 Qt Quick RHI backend、GPU preference、真实 adapter 日志。
- QuickShell root window：尽可能在 scene graph 初始化前绑定 `QQuickGraphicsDevice`。
- QuickShell preview composite surface：继续支持 `QQuickGraphicsDevice` 绑定，但改为复用统一策略。
- Timeline / preview quick scene：记录实际 RHI API 和 adapter。
- Export worker：snapshot 或 worker args 带入同一策略，D3D11 QRhi export session 用同一 adapter LUID 创建 device / texture / staging readback。

建议先只承诺 Windows D3D11 的高性能默认链路。OpenGL、D3D12、Vulkan 可以保留为手动 RHI / 诊断路径，但不作为第一阶段目标。用户可见的核显/独显选择在链路稳定后再补。

## 实施阶段

### P0：日志与文档校准

目标：先让 bug report 能回答“用户实际跑的是谁、走了哪条路、日志是否足够判断”。

工作项：

- 新增 `startup/process_identity`：
  - pid、ppid、真实 exe path、launcher/real app 判断、argv、cwd、package root、app dir。
- 修正用户文档中“desktop app forces OpenGL”的过期表述，改成：
  - GUI 默认平台 RHI / 可配置。
  - CLI export / worker 当前强制 OpenGL。
- 明确 Windows 图形设置应配置 `app\MiaCode.exe`，包根 `MiaCode.exe` 只是 launcher。
- 将现有 `media_backend adapter` 字段改名或补充说明为启发式/枚举结果，避免被误读为 Qt RHI 实际 adapter。

验收：

- 普通 GUI debug log 能直接看到真实 exe 路径和 backend source。
- 支持人员不需要再通过包结构猜用户配置了哪个 exe。

### P1：实际 adapter 可见

目标：debug 日志能判断 GUI/导出实际 GPU。

工作项：

- GUI D3D11 actual adapter 日志：
  - 在 Quick scene graph 初始化后，通过 `QSGRendererInterface::DeviceResource` 取 `ID3D11Device`。
  - `QueryInterface(IDXGIDevice)` -> `GetAdapter()` -> `GetDesc()`。
  - 记录 adapter name、vendor id、device id、subsys id、revision、LUID、dedicated/shared/system memory。
  - 可复用 `TimelineQuickItem.cpp` 中查询 D3D11 device / DXGI memory 的模式，但不要只记录 memory。
- GUI OpenGL actual renderer 日志：
  - 记录 `GL_VENDOR`、`GL_RENDERER`、`GL_VERSION`。
- Export OpenGL actual renderer 日志：
  - `PreviewQuickExportSession::initialize()` context current 后写 `GL_VENDOR` / `GL_RENDERER` / `GL_VERSION`。
- Export summary 日志增加：
  - `render_backend=opengl_qquick_rendercontrol`
  - `rhi_api=OpenGL`
  - `adapter_or_renderer=...`
  - `pbo_requested/pbo_enabled/readback_mode`
- QuickShell 拓扑日志：
  - 记录 `frontend=quickshell`、`hidden_mainwindow=1`、`stage_media_route=QuickShellStageHost`、root/top-level window 数量。

验收：

- 用户贴一份 `--debug` 日志即可判断 GUI path、导出 path、D3D11 adapter 或 OpenGL renderer。
- 如果是 OpenGL 且无法确定 adapter，日志明确显示“renderer string only / adapter not guaranteed”。

### P2：默认高性能策略

目标：默认更容易跑在独显，同时不牺牲用户覆盖能力。

工作项：

- 在真实 app 和 launcher 的 Windows target 中导出：
  - `NvOptimusEnablement = 0x00000001`
  - `AmdPowerXpressRequestHighPerformance = 1`
- 增加启动日志确认符号策略版本，例如 `gpu_hint=nvidia_optimus,amd_powerxpress exported=1`。
- 发布文档说明：
  - 默认倾向高性能 GPU。
  - 用户仍可在 Windows 图形设置里覆盖。
  - release 包中要配置 `app\MiaCode.exe`。
- 保留 `--rhi=software` / 软件路径作为故障回退。

风险：

- 这些 hint 是进程级偏好，不是强制 adapter 绑定。
- 对 OpenGL 导出尤其只能增加被选中独显的概率，不能提供精确 LUID 绑定。
- 双显卡笔记本可能被 Windows 电源策略、厂商控制面板或外接显示器拓扑覆盖。

验收：

- `dumpbin /exports` 或等价工具能在 launcher 和 real app 上看到符号。
- 双显卡测试机默认启动日志显示更可能选中高性能 adapter；若未选中，日志仍能解释当前实际 adapter。

### P3：内部高性能设备策略骨架

目标：不先做用户 UI，先把“默认高性能 adapter 选择、隐藏诊断覆盖、GUI/worker 策略传递、日志落点”做成稳定骨架，供 P4/P5 复用。

工作项：

- 新增 Windows GPU adapter 枚举与排序：
  - 优先 `IDXGIFactory6::EnumAdapterByGpuPreference`。
  - 默认选 `HIGH_PERFORMANCE` 返回的第一个硬件 adapter。
  - 跳过 software adapter，记录被跳过原因。
  - 记录 name、vendor id、device id、LUID、dedicated/shared memory、software flag、gpu_preference。
- 新增内部策略解析：
  - 默认 `auto_high_performance`。
  - fallback `platform_default`。
  - hidden override `adapter_luid`。
  - hidden override `software`。
- 增加 CLI/env 覆盖，仅用于开发和支持：
  - `--gpu-policy=auto_high_performance|platform_default|software`
  - `--gpu-adapter-luid=<high>:<low>`
  - `MIACODE_GPU_POLICY=...`
  - `MIACODE_GPU_ADAPTER_LUID=...`
  - 这些入口先不出现在用户设置页。
- 将 resolved policy 写入 GUI startup log：
  - requested policy。
  - resolved adapter LUID。
  - fallback reason。
  - process role：GUI / CLI export / export worker。
- 将 resolved policy 或 raw request 传给 export worker：
  - 优先 worker argv/env，不立即扩大 `VideoExportSnapshot` 的长期格式。
  - 如果后续需要让导出任务跨进程/跨机器复现，再把策略固化到 snapshot。

风险：

- `EnumAdapterByGpuPreference` 只表达偏好，不保证厂商控制面板或系统策略一定照做；实际命中仍以后续 D3D11 device 日志为准。
- hidden override 容易被误当成正式能力；日志和文档要标明“diagnostic / unsupported UI”。
- worker 继承策略时要避免污染其它子进程，例如 ffmpeg。

验收：

- 双显卡开发机上，默认策略能解析出一个 high-performance adapter LUID。
- GUI 与 export worker 能打印同一份 policy request / resolved 结果。
- 设置非法 LUID 时，进程不崩溃，日志写明 fallback 到 `platform_default`。

### P4：GUI 默认高性能 D3D11 链路

目标：在 Windows GUI 默认路径上建立一条稳定可用的 D3D11 高性能渲染链路。用户 UI 后置；本阶段只要求默认行为、隐藏诊断开关、日志和 fallback 可靠。

#### P4.1：D3D11 device provider

- 从 `PreviewSharedD3D11Device` 中抽出更通用的 provider，避免只服务 preview video composite。
- 输入：P3 resolved policy。
- 输出：
  - `QQuickGraphicsDevice`，供 Qt Quick window 在 scene graph 初始化前绑定。
  - 原始 `ID3D11Device` / `ID3D11DeviceContext`，供需要共享 device 的路径使用。
  - adapter desc / LUID / feature level / creation flags。
- 创建参数：
  - `D3D11_CREATE_DEVICE_BGRA_SUPPORT` 必须开启。
  - `D3D11_CREATE_DEVICE_VIDEO_SUPPORT` 仅在需要与 D3D11VA decode 共享时开启；如果默认开启风险低，也可以保持当前 `PreviewSharedD3D11Device` 行为。
  - debug layer 仍只由现有诊断开关控制。
  - 创建失败必须 fallback 到 `platform_default`，不能阻塞 GUI 启动。
- 线程保护：
  - 如果 device 会同时给 Qt Quick render thread 和 decode/copy thread 使用，继续启用 `ID3D10Multithread::SetMultithreadProtected(TRUE)`。
  - 如果只交给 Qt Quick root window，可先不共享 decode，降低第一版风险。

#### P4.2：QuickShell root window 绑定 spike

- 先验证 `QQmlApplicationEngine` 创建 root `QQuickWindow` 后、scene graph 初始化前调用 `setGraphicsDevice(...)` 是否稳定。
- 优先尝试 `QQuickGraphicsDevice::fromAdapter(luidLow, luidHigh)`：
  - 让 Qt 自己创建 D3D11 device，生命周期简单。
  - 日志用 actual adapter 验证是否命中。
- 如果 `fromAdapter(...)` 无法满足视频共享或命中不稳定，再尝试 `fromDeviceAndContext(...)`。
- 如果 root window 已经太晚无法安全绑定：
  - 不阻塞 P4。
  - 日志明确 `root_window_device=qt_default`。
  - 第一版高性能链路先覆盖 preview/export 的主要负载。

#### P4.3：Preview composite / timeline 接入

- QuickShell preview composite `QQuickView` 从统一 provider 获取 device，替代局部调用 `sharedPreviewQuickGraphicsDevice()`。
- 保留现有硬解软件回退、completion wait、drop corrupt 等诊断开关。
- Timeline quick scene 和 preview quick scene 不一定要主动绑定 device，但必须记录 actual adapter，用来判断 root/composite/timeline 是否一致。
- 如果多个 Quick surface adapter 不一致，先作为日志风险暴露，不在第一版强行合并所有 surface。

#### P4.4：默认启用与 fallback

- 默认路径：
  - Windows + Qt Quick D3D11 可用：尝试 high-performance D3D11。
  - provider 创建失败：回退 Qt platform default。
  - 用户/诊断显式 `--rhi=opengl|software`：尊重显式 RHI，不强行 D3D11。
- 日志必须写：
  - requested policy。
  - provider result。
  - 每个关键 Quick surface 的 actual RHI API + adapter LUID / renderer。
  - fallback reason。
- 暂不新增用户设置页；仅保留 CLI/env 方便 A/B。

风险：

- D3D11 device 与 QtAVPlayer/FFmpeg D3D11VA 解码共享时，线程保护、texture bridge、device removed 都是高风险点。
- 当前已有 iGPU 硬解绿屏/garble 相关诊断和软件解码偏好；设备策略不能把这些回退路径移除。
- 如果 root QuickShell window、preview composite surface、export worker 使用不同 device，第一版先通过日志暴露，等 P5 验证后再决定是否强制统一。
- 过早引入用户 UI 会放大承诺范围；本阶段只承诺默认链路和诊断开关。

验收：

- 默认启动不回归：QuickShell 能打开、预览能显示、timeline 能显示。
- 双显卡测试机上至少 preview 主负载或 root window 命中 high-performance adapter，并在日志中可见。
- 强制 provider 失败或非法 LUID 时，GUI 仍能 fallback 启动。

### P5：D3D11 QRhi 导出链路

目标：新增一条与实时谱面渲染策略一致的 D3D11/QRhi 导出链路。第一版只要求 Windows D3D11 稳定可用、日志清晰、OpenGL 可回退；不追求跨 RHI 通用抽象。

#### P5.1：独立 D3D11 render-control spike

- 新增 `PreviewQuickD3D11ExportSession`，先与当前 `PreviewQuickExportSession` 并行，不替换旧类。
- 初始化路径：
  - 复用 P4 provider 选择 adapter / 创建 device。
  - `QQuickWindow(renderControl)` 绑定 `QQuickGraphicsDevice::fromDeviceAndContext(...)`。
  - 创建 `ID3D11Texture2D` render target。
  - 用 `QQuickRenderTarget::fromD3D11Texture(...)` 设置离屏目标。
  - 创建 depth/stencil 需求需要 spike：优先让 Qt 处理；如果渲染缺失，再显式提供 depth texture 或调整 target flags。
- 最小 render：
  - 先渲染固定尺寸单帧。
  - 再渲染真实 `PreviewQuickSceneRoot`。
  - 最后接入逐帧 playhead。

#### P5.2：D3D11 readback MVP

- 先使用同步 readback：
  - render target texture -> staging texture：`CopyResource` 或 `CopySubresourceRegion`。
  - `Map(D3D11_MAP_READ)`。
  - 处理 row pitch。
  - 转成现有 ffmpeg raw pipe 需要的 RGBA image/buffer。
- 明确验证：
  - BGRA/RGBA 通道顺序。
  - premultiply / unpremultiply。
  - 垂直方向是否需要 mirror。
  - alpha flatten 是否与 OpenGL 路径一致。
- 第一版不做异步 staging ring，不做多帧 pipeline；先保证正确性。

#### P5.3：接入现有导出 backend

- `VideoExportQuickRenderBackend` 增加内部 backend 选择：
  - `opengl_qquick_rendercontrol`：当前稳定路径。
  - `d3d11_qrhi_rendercontrol`：新路径。
  - `auto`：先仅通过 hidden env/CLI 开启；验证后再默认 D3D11。
- 建议隐藏开关：
  - `MIACODE_EXPORT_RENDER_BACKEND=opengl|d3d11_qrhi|auto`
  - 默认初期仍为 `opengl`。
- 复用现有：
  - `PreviewSceneAssetRepository`
  - `PreviewFrameState`
  - layer flags
  - ffmpeg raw pipe
  - progress / cancel / failure propagation
- 新增日志：
  - selected backend。
  - requested/resolved adapter LUID。
  - render target format。
  - readback format / row pitch。
  - fallback reason。

#### P5.4：fallback 与默认切换

- D3D11 session 初始化失败：
  - 自动 fallback OpenGL。
  - export log 写 `fallback_from=d3d11_qrhi fallback_to=opengl reason=...`。
- D3D11 单帧 render/readback 失败：
  - 第一版可以 fail export 并提示使用 OpenGL；生产化后再自动重启 OpenGL worker。
- CrashExit：
  - 现有 PBO crash retry 是 OpenGL PBO 专项。
  - D3D11 crash retry 先不复用 PBO 逻辑，避免误判；验证后再设计一次 `d3d11_qrhi -> opengl` worker retry。
- 默认切换条件：
  - 双显卡测试机和单 iGPU 测试机都能稳定导出。
  - 至少一组复杂谱面在 1080p60 导出完成。
  - 日志能证明 GUI 与导出 worker 使用同一 high-performance policy 或解释 fallback。

#### P5.5：生产化优化

预计工作量：

- D3D11 QRhi MVP：1-2 周。
- 可替代当前默认导出的生产化版本：3-5 周。
- 跨 D3D11/OpenGL/Vulkan/D3D12 的完整 QRhi 抽象导出：6 周以上，不建议作为第一阶段目标。

生产化补齐项：

- staging texture ring，降低 `Map` 等待。
- device removed / resize / teardown / fallback 日志。
- D3D11 debug layer 诊断入口。
- 与导出质量档、worker retry、summary log 接轨。
- 与 OpenGL 路径保留固定样例对照，避免后续视觉漂移。

风险：

- Qt Quick render control + D3D11 render target 的生命周期与当前 OpenGL FBO/PBO 不同，不能机械迁移。
- 当前导出代码约 1400 行，深度依赖 GL context current、FBO、PBO、fence、readback ring。
- D3D11 readback 默认输出可能是 BGRA，需要验证转换成本和像素一致性。
- 如果 D3D11 QRhi 在某些机器失败，必须自动 fallback 到 OpenGL，并把失败原因写入 export log。

验收：

- hidden 开关下 D3D11 QRhi 能成功导出一段固定样例视频。
- 日志能看到 `d3d11_qrhi_rendercontrol`、adapter LUID、readback format。
- 关掉 hidden 开关后 OpenGL 导出仍保持原行为。

## QuickShell / MainWindow 并存专项验证

该专项与 GPU 策略并行推进，防止新增 device policy 放大现有混合宿主风险。

需要验证：

- QuickShell 默认启动时，隐藏 `MainWindow` 不会成为用户可见主窗口。
- `QApplication::topLevelWidgets()` / `QGuiApplication::topLevelWindows()` 日志能区分：
  - QML root window。
  - hidden backend MainWindow。
  - bridge tool surfaces。
  - preview composite/fullscreen windows。
- `lastWindowClosed`、`aboutToQuit`、`accepted_close_shutdown_*`、`accepted_close_destroy_*` 仍按现有顺序触发。
- 导出页嵌入预览、普通预览、全屏预览不会同时激活两套 chart renderer。
- Timeline quick route 在 QuickShell 下保持 widgetless，不为了旧 MainWindow 强行创建旧 `TimelineView`。
- Dialog parent/focus 在隐藏 `MainWindow` 后端存在时仍不会跑到屏幕外或挂在不可见窗口下面。

建议新增日志：

- `quick_shell/topology`
  - `root_window=... hidden_mainwindow=... bridge_surfaces=... preview_surface=...`
- `quick_shell/device`
  - 每个 Quick surface 的 RHI API、adapter LUID 或 renderer string。
- `preview/render_route`
  - `frontend=quickshell stage_media=QuickShellStageHost dcomp=... inline=... separate_surface=...`

风险等级：

- 启动/关闭/focus：中风险。已有专项日志和修复，但新增设备绑定可能改变窗口初始化时机。
- GPU adapter 一致性：高风险。现有多个 Quick surface 不天然共享同一 device。
- 用户可见双窗口：低到中风险。代码已有 `WA_DontShowOnScreen`、`hide()`、bridge rehost，但需要回归。

## 简化验收

第一阶段只验收默认高性能链路，不验收用户 UI：

- GUI：默认启动稳定，debug log 能看到 process identity、RHI API、actual adapter。
- 导出：hidden 开关下 D3D11 QRhi 能导出固定样例；默认 OpenGL 导出不回归。
- 双显卡：至少一台 iGPU + dGPU 设备上，日志能证明 high-performance adapter 命中或清晰 fallback。
- 回退：非法 LUID / provider 创建失败 / hidden backend 关闭时，GUI 和导出都能回到原有路径。
- QuickShell：隐藏 `MainWindow` 不变成可见主窗口，关闭后无明显进程残留。

## 建议落地顺序

1. P0 + P1：先补日志和文档，让后续用户反馈可诊断。
2. P2：加高性能 GPU hint，低成本改善默认行为。
3. P3：加内部高性能策略骨架、adapter 枚举、hidden override、worker 策略传递。
4. P4.1-P4.2：做 D3D11 provider 与 QuickShell root window 绑定 spike。
5. P4.3-P4.4：接入 preview composite / timeline actual adapter 日志，形成 GUI 默认高性能链路。
6. P5.1-P5.3：并行实现 D3D11 QRhi 导出 session，hidden 开关 A/B。
7. P5.4-P5.5：通过固定样例与双显卡验证后，再把 Windows 导出默认切到 `auto -> d3d11_qrhi -> opengl fallback`。
8. 用户 UI：链路稳定后再补设置页、adapter 列表、重启提示和高级覆盖。

## 未决问题

- `QQmlApplicationEngine` 创建的 root `QQuickWindow` 是否能在所有平台/主题启动路径中稳定地赶在 scene graph 初始化前 `setGraphicsDevice(...)`。
- 是否采用 `QQuickGraphicsDevice::fromAdapter(...)` 让 Qt 创建 device，还是应用创建 `ID3D11Device` 后用 `fromDeviceAndContext(...)` 导入；前者生命周期简单，后者更利于与视频解码共享。
- D3D11 QRhi 导出的 readback 格式、premultiply、vertical mirror 是否能做到与现有 OpenGL 导出完全一致。
- 用户配置是否应分为“应用 GUI GPU”和“导出 GPU”，还是默认同一策略、仅高级设置允许拆分；该问题后置到稳定链路验证之后。
- D3D12/Vulkan 是否需要列为未来项；第一阶段不建议承诺。
