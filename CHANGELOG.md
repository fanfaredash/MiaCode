# 更新日志

本文根据仓库提交历史整理历史发布记录。早期版本与内部预发布版本中有不少本地验证用的 beta/dev 编号，因此相邻、面向同一条用户可见发布线的内部版本会合并记录。

## 1.0.0

首个 1.0.0 公共版本，基于 0.5.2 beta 线整理发布，补齐公开致谢、Net 工具入口和编辑器布局持久化等收尾工作。

### 发布整理

- 将应用版本从 `0.5.2-beta5` 更新为 `1.0.0`，移除预发布标签。
- 更新 README / README_EN 鸣谢，补充 Majdata Net、MaiViewer、TangScend、MiaCode logo 绘制和内部测试协助者。
- 删除根目录临时启动脚本，清理样例说明中对应的过期入口。

### 工具与工作流

- Net 批量下载会记住上次使用的输出目录，便于重复下载到同一文件夹。
- 将 Net 批量下载放入顶部 `工具` 菜单最后一项，并用分隔线与前面的导出工具隔开，同时保留工具箱入口。
- 修复编辑器补全和下载器搜索相关问题。

### 界面与布局

- 持久化编辑器视觉布局偏好，包括预览区域宽度、outline 折叠状态和展开宽度。
- 保留并整合 0.5.2 beta 线的封面导出修复：谱面帧内圈背景、封面导出菜单入口、导出成功打开文件夹和内圈背景下拉控件。

## 0.5.2-beta5

继续保留在 0.5.2 beta 线，重点打磨封面导出工作流与谱面帧内圈背景控制。

### 封面导出

- 将应用版本回撤为最近的 beta 版本 `0.5.2-beta5`。
- 修复导出页通过工具菜单打开封面导出时的难度选择回退问题。
- 调整谱面帧内圈背景合成顺序，让图层不透明度只影响谱面画面，不影响内圈背景背板。
- 将谱面帧内圈背景改为 `曲绘 / 透明` 两种模式；曲绘模式保留亮度，透明模式使用透明度控制黑色背板。
- 将内圈背景模式控件改为与画板背景一致的下拉菜单样式。
- 封面导出成功弹窗对齐视频导出成功弹窗，支持直接打开导出文件夹。

## 0.5.2-beta3

首个公开预发布候选版本，重点是仓库公开与 Windows 包可复现构建。

### 公开发布准备

- 为非商业公开预发布整理仓库，明确许可证边界、第三方说明、发布检查清单和公开审计记录。
- 为开源目录重组公开文档和脚本。
- 移除废弃的 GitHub Actions，发布打包改为本地脚本流程。
- 记录历史清理、分支清理和可复现依赖准备说明。
- 增加 README 顶部头像、社群二维码资源，并调整鸣谢措辞。

### 构建与打包

- 更新 Windows 构建文档和脚本，覆盖固定版本 FFmpeg 准备与 QtAVPlayer FFmpeg dev SDK 设置。
- 修复固定 Windows FFmpeg 下载链接。
- 发布打包继续以本地 Windows 预发布产物为中心。
- 从干净 clone 验证 `MiaCode-v0.5.2-beta3-win64.zip` 构建与打包，SHA256 为 `50E6564E96866AF2B0D8C29EACCED1E9FC2684E8AC21F48104FD0EAD33B4BC57`。

### 修复

- 修复空数值元数据序列化问题，包括裸 `&first=`。
- 修复欢迎页/元数据页重排，以及中文输入法设置说明措辞。
- 移除误入仓库的 skinSTD SlideOK fast-gd 精灵图。
- 调整预发布分支上的预览传输控件布局。

## 0.5.2-beta

进入公开预发布前的一条 beta 线，重点包括预览、导出页、谱面整理和大型重构。

### 重点

- 为 QtAVPlayer 预览后端加入硬件/软件视频解码偏好，并支持运行时切换。
- 加入硬件解码稳定性工作、NV12 诊断，以及 seek 后绿帧完成顺序修复。
- 加入地雷音符（`m`）和负 HS（`<HS*-N>`）反向流动支持。
- 加入 Net 公开谱面批量下载器。
- 加入 Xiaolai Mono CJK HUD 字体子集及其子集化工具链。
- 加入片头开关框、欢迎页中文输入法设置和偏好 schema v4。

### 导出与片头

- 在导出页加入可拖动的负时间片头预览区域。
- 加入可播放的导出页预览接线、动态片头画面/声音和接近编辑器的预览控制。
- 加入 `clock_count` 倒计时控制、预览音频接线和导出开关约束。
- 加入导出范围轨道、范围时长说明和导出侧栏忙碌旋转反馈。
- 重设计导出 hub/page 布局，加入嵌入式视频面板和固定帧表面行为。
- 修复片头 seek、导出页试听恢复时的 BGM/传输同步。
- 修复过期片头负时间区域和 `clock_count` 重复写入。

### 谱面、时间轴与工具

- 将批量变换操作直接放入编辑器右键菜单。
- 为升 subdivision 半步但网格无法继续二分的情况加入 x3 回退。
- 允许 `&key=` 元数据前有前导空白，并对元数据值右侧去空白。
- 修复延迟检测的 BPM 默认值发现逻辑，并将谱面参数合并进延迟工作流。
- 加入同轨道 `v` 形 slide 支持（`1v1` 到 `8v8`）。
- 改进多窗口并发时的崩溃恢复 session marker。

### 重构与打磨

- 将 main-window、preview、export、logging、timeline、audio、chart、app 中的大文件拆成更小的翻译单元。
- 将 `slide_data` 拆入独立 Qt resource，并裁剪发布包内容。
- 加入 skinDX 地雷音符素材，并将 slide 参考数据迁移到 `assets/reference`。
- 改进日志记录格式、轮转行为、诊断拆分和 debug flag 缓存。
- 修复预览烟花预热、清空元素、查找栏、MP3 选择器、全屏速度和导出页布局稳定性问题。

## 0.5.1-beta

围绕封面导出、片头、欢迎页、时间轴、解析器和恢复流程的 beta 线。

- 加入首次启动设置对话框，用于选择预览侧和主题。
- 加入 WYSIWYG 封面编辑器、谱面框图层编辑、布局保存/导入和封面导出传输控制打磨。
- 加入谱面框内环背景和进程内取帧器支持。
- 加入暂停显示按键重绑定。
- 加入 `clock_count` 元数据和检测 UI 基础。
- 加入导出效果预览，并修复导出预览滑块 scrub。
- 加入缺少逗号再写 directive 的解析器警告，以及更严格的 headed slide 分支校验。
- 加入 3.0x 时间轴缩放预设和分级网格线高度。
- 修复从谱面 0 开始的范围导出、预览 debug HUD 行为、资源计量、偏好页尺寸和恢复提示。
- 集中原生窗口主题逻辑，并让崩溃恢复与备份恢复流程对齐。

## 0.5.0-beta

围绕片头、导出对话框、预览视频、HUD 和打包的一条 beta 线。

- 加入 maimai 风格 track-start 片头 overlay、导出 banner 数据、动态卡片揭示、背景纹理和 CLI 片头工具。
- 将 track-start 片头与开场 SFX 合成进谱面导出。
- 为片头/卡片渲染加入 Resource Han Rounded 字体，并在打包中附带 OFL 许可证。
- 加入难度 banner 静态图片导出，以及后端无关的离屏封面渲染。
- 加入 QtAVPlayer/FFmpeg 预览视频后端和仅解码用 FFmpeg trim 工作。
- 将视频导出对话框重组为 Output/Video/Gameplay/Font/Range 风格标签页，之后又将 HUD 控制合并进 Visuals。
- 加入导出质量切换基础和片头背景淡入淡出。
- 加入预览 HUD 中央显示和分类型音符总数。
- 将 slide 形状环拆到判定文字下方，并加入 scale-pop 与 break-flash 判定效果。
- 修复解析器对悬空 each 分隔符和分段 slide 时值的处理。
- 修复片头视觉接缝、卡片框裁剪、曲绘框像素对齐、导出 readback 崩溃和片头淡入后 alpha 展平。

## 0.4.0-beta 系列

大型内部 beta 系列，将 MiaCode 从旧预览/导出栈推进到现代 Quick Shell、BASS 音频、DComp/HWND 实验、Windows 运行时打包、导出页工具，以及第一版 maimai 风格片头管线。

### beta1-beta7：Quick Shell、预览与早期打包

- 在 `0.3.11` Quick Shell 稳定工作后准备第一条 `0.4.0-beta` 分支。
- 将 Windows 打包路径锚定到仓库根目录，使脚本可从仓库外启动。
- 加入 quick-shell 预览 host 诊断、更高精度的预览 tick 调度和更暗的全屏传输 overlay。
- 加入全局和分通道预览静音控制。
- 改进暂停预览跟随、错误标记音频文件的 seek 行为和 slide 运动角度插值。
- 加入基于选择区的谱面格式化，并改进 inline terminal 附近的时间轴 segment anchor。
- 加入缓存列波形渲染，以及早期负时间预览/Muri anchor 处理。
- 将预览跟随做成应用偏好，并保留 zip 里的包根目录。
- 移除高压缩导出预设，并改进预览 FPS 诊断/日志。

### beta9-beta19：导出运行时、BASS 音频与保留式预览状态

- 改进导出运行时策略、worker 管线、filter 处理、静态背景预处理和预览/导出比例与 HUD 排版。
- 统一预览播放时序权威，并抽出共享预览 SFX 播放调度。
- 加入 BASS 预览后端、BASS 导出音频管线、共享时序设置和 Windows 包内 BASS runtime 部署。
- 加入保留式 BASS 预览传输和跨时间轴/导出流程的预览播放状态保留。
- 加入 tap/touch 流速拆分、break-slide 尾音过滤、独立 break-slide 音频桶行为和 hold 尾部 judge SFX。
- 加入 1.5x 实时预览速度和播放速率 toast overlay。
- 加入 Quick Shell 关闭/启动可见性修复、停止/暂停处理修复和预览 stage media 恢复。
- 加入谱面格式化 384 snap 偏好，并在 release tail 中保留合并 slide 结构。
- 修复 Muri 与时间轴中的 sub-tick wifi touch、头星处理、碰撞目标、tooltip 命中测试、合成 slide 头和静态面板去重。
- 开始 present-driven frame pacing、异步日志、MMCSS 注册、QSG timing 捕获和 DComp smoke test 实验。

### beta20-beta29：DComp/HWND 预览、渲染器重构与操作日志

- 合并大型 v2 refactor 分支，并在 beta20 线中将 DComp 预览路径作为默认实验。
- 将 preview/video 代码重组为 compositor/source 模型，加入谱面侧和 HUD 侧 preview source。
- 加入 D3D11/DXGI/DComp 渲染线程工作、snapshot queue、固定名义 playhead 时钟、sprite/HUD/background 管线和设备移除/可见性恢复实验。
- 加入 timeline DComp/HWND 实验、时间轴几何 GPU 管线、label 缓存、sprite asset 缓存和几何/生命周期诊断。
- 加入崩溃时 autosave、恢复提示和包依赖审计。
- 移除已死亡的 libmpv 包内容，并统一预发布版本变量。
- 加入视频导出音频码率下拉框和多处导出布局改进。
- 通过将小节线/音符线提升到专用 z slot 和 palette entry，修复时间轴网格可见性。
- 将语法诊断与宽松解析解耦，并加入 misplaced modifier 与 slide-chain syntax 的 strict-only 检查。
- 加入 `.miacode` 下 autosave 存储、紧凑 x264 调参、worker 线程 readback 转换、zero-copy frame handoff 基础和 CPU 构建 mip-chain 实验。
- 加入预览音频锚定和 DComp/音频同步修复。
- 加入 pad 距离、slide-OK alpha、judge-text alpha、touch border 堆叠和 judge-effect 开关的场景对齐改进。
- 加入独立时间轴跟随控制、cursor-follow highlight 行为和 timeline/quick tick/header 修复。
- 在 document I/O、parser/transform、audio、render submission、preview QML bootstrap、IPC、worker process 和 export controller 路径上加入 operation breadcrumb logging。
- 加入跨进程崩溃日志发现、硬崩溃 shadow buffer，以及带进程内 fallback 的进程隔离预览 worker 实验。

### beta31-beta41：用户工作流、打包 wrapper 与编辑器工具

- 在 beta31 线中默认使用 HWND 时间轴路径，同时保留 QML 路径作为 fallback。
- 修复 preview worker teardown、暂停 worker 预览窗口跟踪、break-slide 音量、worker Muri slide erasure 和局部导出 overlay preload timing。
- 加入第一版 QML 片头视频原型和全范围导出的 `&clock_count` 倒计时 SFX。
- 加入预览 canvas/时间轴帧率偏好，并在导出 lead-in 中渲染游玩区/HUD 倒计时。
- 加入所选 subdivision 调整动作、自定义皮肤/outline 导入、启动文件夹拖放打开和跨难度视图状态保留。
- 重做延迟检测，使 offset 检测先弹出结果而不是立即自动写入。
- 合并延迟检测传输/SFX/缩放控制，并将 follow/view-lock 选项迁移到 gear menu 工作流。
- 加入编辑器 overwrite mode、块状光标、内联 Follow Code 开关、备份恢复菜单，并重命名 Preview 菜单中的音频/视频入口。
- 加入 live preview 和导出后端的 BGM peak-normalization 扫描。
- 将 runtime DLL 隐藏到 `app/` 下，并通过 wrapper 可执行文件启动，同时给 wrapper 嵌入应用图标。
- 加入 toolbox 音视频媒体工具、可编辑快捷键、书签管理、从 `track.mp3` ID3 tag 读取元数据，以及预览速度/subdivision 修复。
- 修复 `<HS*N>` 高速倍率解析/渲染，以及带匹配 bracket group 的 chained slide rotation。
- 恢复编辑器文本拖放，并加固速率恢复和暂停 view-lock 场景下的预览/scrub 行为。

### beta42-beta50：启动诊断与预览音频重构

- 调查 Windows 10 22H2 无声启动崩溃，并加入启动崩溃诊断。
- 加入 QtPluginDiag launcher，用于平台插件启动 race 的排查。
- 将 VC++ runtime app-locally 打包，并加入 DLL 版本探测。
- 加入缺失 DLL 的双语 launcher preflight 诊断和简体中文 MessageBox 文案。
- 重做 BASS 错误报告和预览音频公共入口的 operation scope。
- 围绕 sample-flag pause/resume、TEMPO write-once、master-always-on routing 和 pause-modify-resume 速率变化重建预览音频同步。
- 在音频重构后移除过期调度面，并恢复关键 validation/status 日志。
- 加入 beta50 后续修复：touch-hold 暂停、stop/entry seek、BGM 游标锚定和 SFX event cursor 重新锚定。
- 在测试 table/cache 方案后，简化谱面 normalization snap 行为。

### beta51-beta59：元数据、导出 Lead-In、延迟页与编辑器打磨

- 将裸 touch-hold 解析为零时长 hold，并让零时长 touch-hold 不被渲染层裁剪。
- 加入局部范围导出的 1.5 秒冻结 chart/HUD/audio preload 行为。
- 加入“所有难度共用同一谱师”项目偏好，包含暗色模式安全对话框、checkmark 字形、seed 行为和默认关闭加载行为。
- 改进谱面 normalization 对 slide-segment break 分支和尾随 `{N}` 输出的处理。
- 在速率变化时重建预览 media player，并加入 sync beacon trail 用于诊断。
- 加入 Muri 问题提示项目开关、启动时恢复保存的预览皮肤，以及带 video watchdog 加固的 square-fit 背景缩放。
- 加入 SEH/terminate/signal 的异常与寄存器 dump，并用 `glFenceSync` 修复导出 PBO tearing。
- 让全范围导出 lead-in 通过负谱面时间渲染，并重做静态背景 target rectangle 与 square-fit filter chain。
- 加入时间轴游标三角标记、左上角谱面信息 HUD、原生多文件夹批量选择和编号谱面列表。
- 将 BPM/延迟工具重构为左侧栏页面，加入 sandbox audition 和更好的主题刷新。
- 加入渲染设置标签页、问题图标跳转、编辑器自动配对、底部标签高度持久化、跨行 bracket highlight 和 IME 提交 bracket 配对。
- 修复预览/时间轴 hold 时长不一致、BPM 页试听复用主预览传输、touch/firework 对齐、slide-track break flash 时机和 simultaneous-touch Muri gating。
- 加入 bracket-completion dropdown、延迟页 Audio/Video Processing 按钮、连续 touch-hold SFX latest-wins 所有权、按住 undo/redo、导出预览 PV/BG 可见性和导出 ZIP 打包。

### beta62-beta63：片头、QtAVPlayer 预览与导出对话框重设计

- 加入 maimai 风格 track-start 片头 overlay 资源和导出 banner 数据。
- 将 track-start 片头与开场 SFX 合成进谱面导出。
- 加入 QtAVPlayer/FFmpeg 预览视频后端和仅解码 FFmpeg trim 工具。
- 将导出对话框重构为 Output/Visuals/HUD 标签布局，随后改为 Output/Video/Gameplay/Font/Range 布局。
- 加入导出质量切换基础、PBO readback 选项和片头背景淡入淡出。
- 修复解析器对悬空 each 分隔符和等价总 slide timing 的处理。
- 修复媒体工具操作时预览 `pv.mp4` 句柄释放。
- 加入可取消/按大小 gate 的 FFmpeg 媒体工具操作和延迟 playhead 对齐。
- 加入两级底部标签缩放、分难度 offset/谱师对话框和更稳健的 Windows FFmpeg dev 下载。
- 对齐 track-start pre-roll timing，修复转场视觉，并加入 CLI 片头迭代支持。
- 加入分部 staggered 片头卡片揭示、动态背景纹理、难度 banner 静态导出、用于片头迭代的 preview-seconds 导出上限，以及中央显示/分类型音符总数。

## 0.3.11

Quick Shell 稳定化版本。

- 将 Quick Shell 作为活跃预览 shell 路径发布。
- 打磨 quick-shell 启动、布局、全屏行为、快捷键转发和 header summary 行为。
- 加入暂停预览时聚焦游玩区的行为。
- 打磨 autosave anchor 和行为。
- 将 hold 与 slide 时值规范化到 384 网格模型。
- 加入多押分析的 touch exclusion 选项。
- 对相同头部的 slide 预览头去重，并修复过期/误归属的 Muri overlap anchor。
- 改进语法问题显示和预览 parser marker 匹配。

## 0.3.10-dev 系列

Hybrid Quick 前端和 platform-host 迁移工作。

- 加入 quick-shell beta host mode 和 hybrid debug 打包。
- 按 host route 拆分预览背景媒体。
- 将 GUI runtime 向 platform-backend 抽象迁移。
- 打磨时间轴 anchor、行号缩放、UI cadence、强制 labeled outline 策略和生成的 labeled outline 资源。
- 改进 Quick 预览渲染/启动恢复、背景 profiling、resize throttling 和对象统计 throttling。
- 加入 autosave/UI 变更基础和 DX 皮肤变体支持。

## 0.3.9

Qt Quick 实时预览与导出迁移版本。

- 将预览 runtime host 迁移到 Qt Quick scene graph。
- 将 guide、touch、touch-hold、head 和早期预览批次迁移到 Qt Quick。
- 将预览 runtime 和导出迁移到 Qt Quick。
- 修复 Qt Quick 导出 overlay 合成。
- 为发布打包 Qt Quick 导出路径。
- 恢复 Quick 预览中的 firework fade 语义。
- 为 Quick 预览层加入 prepared note window。
- 修复离屏 readback unpremultiply 和时间轴 each 渲染。

## 0.3.8-dev 系列

预览 runtime、Muri、时序和导出预设开发线。

- 加入 slide delay 和 head material 语义，并补充文档/测试。
- 加固 GL media fallback 和预览 runtime 诊断。
- 改进时间轴和滑块聚焦后的预览播放/暂停行为。
- 打磨长 slide Muri 语义、受保护 slide 碰撞严重级别，以及 Muri 输出中的完整 chained-slide 标签。
- 改进预览 seek 控制、导出对话框偏好和时间轴渲染。
- 统一 timing metadata，并加入谱面格式化工具。
- 加入导出预设和预览对比诊断。

## 0.3.7-dev 系列

时间轴、变换、Muri 和预览性能开发线。

- 加入 parse-level 谱面变换回归规格。
- 拆分快速和慢速时间轴刷新路径。
- 修复 strict 检查和编辑器高亮中的 parser/comment 处理。
- 在变换中保留 brace 前的 timing prefix 和未匹配的选择区边缘。
- 修正 slide each 着色、重复 hold endcap 和 each guide 分组。
- 加入延迟工具中的 offset 实时编辑/恢复行为。
- 优先使用背景媒体而不是 track 内嵌封面。
- 延后预览恢复和分析 UI 工作以减少卡顿。
- 重构 preview/audio SFX 同步和 prepared SFX state。

## 0.3.5-dev 系列

导出、音频、校验和 UI 开发线。

- 加入批量导出 worker 流程，并改进批量导出持久化。
- 加入默认预览音频音量持久化。
- 加入 hold 与 break shine 音符效果。
- 加入时间轴 Alt+滚轮缩放和固定缩放档位。
- 将全角语法错误与无效音符拆成独立诊断。
- 改进导出 worker 和 FFmpeg 失败诊断。
- 加入 Wi-Fi 渲染选项持久化，并将该选项接入 Muri 分析。
- 加入侧栏交换实验和 themed 预览面板复用。

## 0.3.3

校验、导出与预览发布线。

- 加入打开/编辑时自动校验，并恢复 auto-check 行为。
- 将 Muri 问题纳入 header summary，并打磨校验列表优先级。
- 加入隔离的视频导出进程、改进的进度 UI、120 FPS 导出选项、性能 profile 和更快取消。
- 加入删除难度后的 undo 支持。
- 加入带四分之一步进吸附的可编辑流速。
- 加入旋转 slide 头星。
- 加入可中断 firework 效果，以及时间轴交互时暂停。
- 加入导出设置持久化和随导出分辨率缩放 HUD。
- 修复批量变换后的预览刷新、Unicode BGM 路径、touch-hold each 视觉、parser hold 变体和暂停 BGM pre-seek。

## 0.3.1

预览/导出打磨与工作流稳定线。

- 恢复预览中的媒体同步和光标跟随行为。
- 打磨预览工作流、设置、布局、尺寸、底部标签行为和任务栏图标行为。
- 统一预览和导出 SFX 资源查找。
- 回迁导出渲染/音频支持，并默认启用 PBO readback。
- 加入隔离式视频导出设计工作，并改进导出默认值/输出命名。
- 加入语法校验 summary 打磨和 offset 符号对齐。
- 修复导出对话框后的黑屏、错误 dirty 状态、保存/编辑时预览音频突发，以及所选预览 FPS 保留。

## 0.3.0

本 changelog 中曾记录为 `3.0`；历史版本 bump 实际是 `0.3.0`。

- 改进导出稳定性并加固 FFmpeg 校验。
- 加入 CLI 导出入口和导出跟踪更新。
- 加入运行时 encoder probe 和更安全的软件线程策略。
- 从发布产物中移除 `ffprobe`，并固定 FFmpeg 二进制及 archive/version 校验。
- 加入宽屏导出布局、实时预览比例控制和导出亮度控制。
- 默认使用 H.264 导出，并提供 PBO fallback。
- 修复多行粘贴空格和 slide-touch each 分组语义。
- 加入时间轴 header 点击跳转并同步编辑器。
- 加入恢复上次打开文件和对应用户偏好。
- 加入 break touch 解析/预览支持、touch border overlap 渲染、暗色主题支持和编辑器/菜单打磨。

## 0.2.2-dev 系列

`0.2.1` 与 `0.3.0` 之间的开发线。

- 打磨预览/导出渲染，并集中共享常量。
- 加入导出对话框预览状态同步，并打磨预览布局/设置工作流。
- 重新初始化 BGM track 时保留预览位置。
- 加固导出默认值和 break-touch 统计。
- 移除旧 bridge 文档和未使用的导出临时目录 helper。

## 0.2.1

- 修复 FFmpeg 合成 cadence，消除导出时周期性重复帧卡顿。
- 修复时间轴跟随预览行为，使编辑器光标只在播放时由预览控制。

## 0.2.0

### Simai 文本编辑器

- 加入 Ctrl+F 查找与替换。
- 加入可调字体大小和行距。
- 加入 simai 语法高亮。

### BPM 与 Offset 检测

- 加入自动 BPM 和 delay 检测。
- 减少谱师手动测 BPM 和 delay 的需要。

### 谱面视频导出

- 加入完整谱面视频导出。
- 加入局部片段导出。
- 更方便分享短谱面预览。

### 语法校验

- 加入谱面语法错误检测。
- 加入潜在兼容性问题警告。
- 帮助避免在非 Maj 平台上的解析问题。

### 功能补全

- 加入多种音符类型的判定效果动画。
- 加入烟花渲染效果。

## 0.1.1

- UI 打磨。
- 加入应用图标。

## 0.1.0

### Simai 编辑

- 加入 simai 解析和编辑工作流支持。
- 加入多难度字段管理，包括新增、删除和自动切换。
- 加入批量镜像和旋转操作。
- 加入难度页和谱面元数据设置。

### 渲染与预览

- 将预览控制升级为播放器式工作流，包含时间轴和速度选项。
- 加入编辑器、时间轴和预览播放同步。

### 其他

- 加入中文和英文 UI 支持。
