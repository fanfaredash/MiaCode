# MiaCode 0.1.0 开发日志（详细）

本文件保留研发与迭代细节，用于后续维护与回归。

## 1. Simai 编辑能力

- 完成 simai `&key=value` 结构化解析，支持多行字段内容。
- 新建文件默认写入并维护固定元字段：
  - `&title=`
  - `&artist=`
  - `&first=`
  - `&des=`
- 打开旧文件时自动补齐缺失固定元字段。
- 增加难度字段管理：
  - `&lv_x`
  - `&des_x`
  - `&inote_x`
- 实现难度添加、排序与删除流程。
- 难度删除后自动选择最近难度；无难度时回到谱面信息页。
- 空文件场景下删除难度不触发额外保存。
- 默认打开逻辑：
  - 新文件默认进入谱面信息设置页。
  - 已有谱面按优先级进入难度：`5 > 6 > 4 > 7 > 3 > 2 > 1`。
- 元数据页布局调整：
  - 偏移字段位置调整。
  - `Other &xx Fields` 区域对齐优化。
- 编辑器交互补齐：
  - Ctrl+左键文本定位预览时间。
  - 批量操作后保持当前选区。
- 批量变换功能落地：
  - 左右镜像 `Ctrl+J`
  - 上下镜像 `Ctrl+K`
  - 旋转180 `Ctrl+L`
  - 逆时针45 `Ctrl+;`
  - 顺时针45 `Ctrl+'`
- 批量变换规则覆盖 tap/hold/touch/slide，并修正多段 slide 场景映射。
- 修复 0 长度 hold 展示和逻辑：
  - `4h[1:0]` 可显示并按 hold 处理。
  - `4h` 等价于 `4h[1:0]`。

## 2. 渲染与预览

- 预览区改为播放器式控制：
  - 停止
  - 播放/暂停
  - 可拖动时间条
  - 倍速菜单
- 弃用旧入口 `Play` / `Play@Cursor`，统一到底部控制条。
- 时间显示改为 `分:秒:毫秒`。
- 时间轴与预览联动逻辑调整，支持暂停后拖动预览定格。
- 统计面板落地并校准：
  - Tap / Hold / Slide / Touch / Break / Total
- Southern Cross 口径修正：
  - Tap = 1103
  - Total = 1194
- 渲染链路优化重点：
  - 规避高频 CPU fallback。
  - 视频帧处理从 `toImage` 路径迁移到更轻量路径。
  - 降低上传开销与抖动。
- 增加分阶段性能统计（CPU 准备、上传、GPU 绘制、present 近似值等）。
- 启动阶段增加分段计时日志，定位卡顿来源。
- 预览控制与媒体控制器做惰性初始化，缩短首屏等待。
- 启动窗口最大化流程调整，减少先小后大的视觉跳变。
- 右侧预览页布局规则收敛为静态预计算：
  - 先按主窗口分辨率计算右侧预览页目标宽度 `W`。
  - 再根据右侧区域总高度 `H` 与底部播放栏/统计栏最低占用 `H'` 计算预览可用区 `W x (H-H')`。
  - 若 `W < H-H'`，预览窗口取 `W x W`，多余高度分配给统计栏。
  - 若 `W >= H-H'`，预览窗口取 `(H-H') x (H-H')`，多余宽度回收给左侧文本区。
  - 整套计算在窗口绘制前完成，避免预览区先加载再缩放的闪帧。
- 右侧预览页容器调整：
  - 预览窗口强制保持正方形。
  - 背景图片尺寸始终与预览窗口一致，避免出现黑边。
  - 预览内时间/调试 HUD 字号随预览窗口尺寸动态缩放。

## 3. UI/交互与工程化

- 难度色块与选中态绘制逻辑拆分，避免色块被高亮染色。
- 左侧菜单与预览区、统计区样式多轮微调：
  - 对齐
  - 间距
  - 字体
  - 圆角
  - 图标
- 右键菜单、滚动条、时间轴表头与行号样式修正。
- `About` 页面加入版本号与平台信息展示。
- 中文文本适配和术语统一：
  - “谱面信息设置”
  - “添加难度”
  - 菜单与工具项本地化
- 项目名统一为 `MiaCode`。
- 版本升级为 `0.1.0`。
- 打包流程清理：
  - portable 包主程序放根目录
  - 移除不需要的辅助可执行文件
- 构建产物策略调整：
  - 默认发布构建不再产出 `simai_native_dump` 与 `soundtouch_probe`。
  - 仅在显式 Debug 构建时启用开发辅助工具。
  - 主应用构建目标名统一为 `MiaCode`。
- GitHub Actions 构建流程调整：
  - `main` 分支保留稳定的手动 workflow 入口。
  - workflow 通过 `workflow_dispatch.inputs.ref` 接收目标分支或 tag。
  - workflow 先 checkout 指定 `ref`，再执行该分支内的构建脚本。
  - macOS 构建逻辑下沉到 `scripts/build-macos.sh`。
  - Windows 构建逻辑下沉到 `scripts/build-win.ps1`。
- macOS CI 兼容性修正：
  - 不再依赖 Homebrew 的 `qt` 发布链路，改为脚本内固定安装 Qt `6.8.3`。
  - 打包阶段显式设置 `CMAKE_OSX_DEPLOYMENT_TARGET=13.0`。
  - `package-mac.sh` 在 `macdeployqt` 后校验主程序和 `QtCore.framework` 的最低系统版本。
- 文档清理：
  - 去除绝对路径
  - 中英文 README 互链

## 4. 待办

- 烟花效果
- 物件判定效果
- 实时演算级无理检测
- 导出谱面预览视频

## 5. Recent Updates

- Preferences/state persistence unified into `.miacode_preferences.json`; legacy `.miacode_state.json` write path removed.
- Preferences schema normalized to stable `miacode_preferences_v3` with `ui` and `app.preview` sections.
- Default UI language remains `system`; legacy preference data is normalized on load.
- Welcome screen English copy completed, including the empty-state hint.
- Welcome header now uses final sizing on first paint to avoid later height jumps.
- `Lv` field keeps centered input text while drawing the placeholder `&lv_n=` left-aligned.

# MiaCode 0.1.2 开发日志

## 1. BPM 与偏移检测算法（当前实现）

实现位置：`src/tools/LatencyDetectorDialog.cpp`。

### 1.1 音频预处理

- 使用 `miniaudio` 将 `track.mp3` 解码为 `24000 Hz` 单声道浮点 PCM。
- 构建两条包络并归一化到 `[0, 1]`：
  - `onsetEnvelope`（用于 BPM）：`window=1024`、`hop=512`，按帧计算 0.6x 均方差 + 0.4x 帧均值。用于描述音乐强度曲线。
  - `offsetEnvelope`（用于偏移）：`window=512`、`hop=128`，按帧计算与前一帧的差分均值。用于描述音频变化剧烈程度。

### 1.2 BPM 检测流程

1. 粗扫阶段：
- 在 `50..300 BPM` 范围内以 `0.25 BPM` 步长扫描。
- 评分函数为滞后相关：
  - `rate = corr(lag) + 0.20*corr(2*lag) + 0.10*corr(lag/2)`。
  - 其中 `corr(lag) = mean( envelope[i] * envelope[i-lag] )`

1. 候选生成（基线）：
- 先以 `4/4` 对齐模型做基线候选得到 `coarse`。
- 以 `coarse * {0.5, 1.0, 1.5, 2.0, 3.0}` 作为中心做局部搜索。
- 局部搜索由 `bestTempoAlignmentNear()` 完成，网格 `0.05 BPM`，搜索半径按倍频分支在 `4~8 BPM` 调整。

1. 拍号感知候选增强：
- 对 `3/4`、`6/8` 追加拍号专用候选。
- 条件启用三倍频分支（`coarse * 3`）；其中 `6/8` 仅在 `coarse < 80` 时启用该分支。

1. 倍频/半频别名修正：
- 先做全局低速别名提升，再按拍号应用启发式算法，将 BPM 翻倍恢复成原始值。

1. 最终对齐：
- 以选中的 BPM 在对应拍号附近再次对齐。
- 自动检测模式会在全部已知拍号中择优取分数最高者。

### 1.3 偏移检测流程

- 输入：BPM + `onsetEnvelope` + `offsetEnvelope`。
- 在一个拍周期内做两阶段相位搜索：
  - 粗扫步长 `1 ms`，细扫步长 `0.25 ms`。
- 相位评分为逐拍平均：
  - `0.75 * transientSample + 0.25 * onsetSample`。
- 为抑制“等价但过大偏移”，引入近零惩罚项：
  - `abs(phase) * 0.18`。
- 候选吸附集合包括：原始最优相位、`0`、以及 `n/30 s` 的离散偏移点。
  - 仅保留 `score >= 0.73 * bestScore` 的候选，再选绝对归一化相位最小者（同分时更靠近原始最优相位者优先）。
- 最终偏移归一化到 `[-beat/2, +beat/2]`。

### 1.4 已知限制

- 部分曲目仍可能出现半速/倍速歧义（尤其节奏结构重复性高的曲风）。
- 对显式含有非零 `&first` 的谱面，检测结果建议人工复核。

## 2. 启动卡顿优化原理（预览区）

目标是把预览侧重负载从 UI 主线程剥离，避免窗口刚启动时菜单与交互被阻塞。

1. 皮肤异步加载与主线程回填：
- `PreviewCanvas::setSkinDirectory()` 将皮肤解析与资源准备投递到 `QThreadPool`。
- Worker 完成后通过主线程队列回填，并使用 generation 校验丢弃过期结果，避免竞态覆盖。

2. 纹理分帧预热：
- 纹理预热不再一次性执行，改为队列 + `QTimer(16ms)` 分批处理（每 tick 处理少量项）。
- 通过摊平 CPU/GPU 峰值，降低启动瞬间卡顿感。

3. 预览子系统空闲预热：
- `MainWindow` 在启动流程中调度预览子系统预热（媒体控制器与效果音运行时）。
- Worker 线程完成后在主线程做轻量 apply，避免首次点击预览时集中初始化。
- 若预热尚未完成，播放路径仍保留兜底 `ensurePreviewSfxRuntimePrepared()`，保证功能正确性。

4. 启动分段计时与定位：
- 关键阶段记录到 `%TEMP%/miacode_startup_timing.log`，包含 worker 预热、主线程 apply、纹理预热等阶段耗时。
- 用于持续定位“启动卡顿”主因并验证优化是否生效。

## 6. Tap/Hold Effect 渲染复刻说明（2026-03-10）

### 6.1 TapEffect（judge_effect_tap / judge_effect_tap_break）

- 触发时机：tap 判定、slide/wifi 头星判定、hold 结束判定、touch-hold 结束判定。
- 基准像素换算：
  - `tapBasePixels = tap.png宽度 * kSkinAssetScale * canvasScale`（无贴图时回退到 `96*canvasScale`）。
  - `effectBasePixels = tapBasePixels * kJudgeEffectBaseRelativeToTap`。
  - `effectOffsetPixels = tapBasePixels * kJudgeEffectOffsetRelativeToTap`。
- 动画计算方法：
  - 统一 clip 时间：`clipTime = clamp(elapsed * playbackSpeed, 0, clipDuration)`。
  - 根节点缩放：`sampleScalarCurve(kJudgeEffectRootScaleKeys, clipTime)`。
  - 透明度：`sampleScalarHermiteCurve(kJudgeEffectAlphaKeys, clipTime)` 再经尾部 gamma。
  - 旋转：`sampleScalarCurve(kJudgeEffectRotationKeys, clipTime)`。
  - 子六边形/星形位置、缩放：由位置/缩放曲线按 `clipTime` 线性采样。
- 发光实现：同一张贴图先绘制一层轻微放大+低 alpha 的 glow，再绘制本体。

## 7. 导出编码与尺寸量化（2026-03-12）

- GPU 导出编码已启用“硬件优先”策略（Windows）：`h264_nvenc -> h264_qsv -> h264_amf -> libx264 -> libopenh264 -> mpeg4`。  
  代码定位：`src/tools/video_export/VideoExportController.cpp::chooseVideoEncoder()`。
- 尺寸量化已关闭（返回原始像素尺寸，不再按 step 桶化）。  
  代码定位：`src/preview/video/PreviewCanvas.cpp::quantizeDimension(int value, int)`。
- 历史量化参数保留用于缓存预热循环步进：  
  - `kGuideTransformSizeStep = 2`  
  - `kSpriteTransformSizeStep = 4`  
  参数定义位置：`src/preview/video/PreviewCanvas.cpp`。

当前关键硬编码参数（Tap）：
- `kJudgeEffectClipDurationSeconds`：单次动画时长（源 clip）。
- `kJudgeEffectPlaybackSpeed`：播放速度倍率。
- `kJudgeEffectBaseRelativeToTap`：特效相对 tap 的基准尺寸。
- `kJudgeEffectOffsetRelativeToTap`：子图形相对 tap 的位移尺度。
- `kJudgeEffectEdgeGlowScale`：glow 放大倍率。
- `kJudgeEffectEdgeGlowAlpha`：glow 透明度倍率。
- `kJudgeEffectTapTextureAngleOffsetDegrees` / `kJudgeEffectBreakTextureAngleOffsetDegrees`：贴图朝向补偿。

## 8. 视频导出路线（方案A，M1+M2 落地记录）

### 8.1 技术路线

- 主路径采用“离线逐帧渲染 + 离线混音 + ffmpeg 编码”：
  1. 从谱面 note marker 生成导出片段事件（含 break/judge/touchhold/firework 音效触发）。
  2. 先离线混音到 `export_sfx.wav`（48kHz/双声道/16-bit PCM）。
  3. 逐帧渲染 overlay（物件层+HUD），通过 `pipe:0` 送入 ffmpeg。
  4. ffmpeg filter graph 合成背景媒体（PV/图片）、暗度遮罩、overlay、BGM+SFX 混音并编码为 MP4。
- 时间轴对齐策略：
  - 固定前置 `3.0s`（`kExportLeadInSeconds`）。
  - `timelineOrigin = segmentStart - 3.0`，导出时戳与音频/BG 轴保持一致。
  - 当 `timelineOrigin < 0` 时，BGM 通过 `adelay` 对齐；SFX 仅在片段内触发。
- 渲染后端策略：
  - 优先离屏 OpenGL（共享实时预览上下文），失败时自动回退 CPU/QPainter 路径。
  - 统一复用 `PreviewCanvas` 的绘制序列，减少“实时渲染 vs 导出渲染”差异。

### 8.2 代码定位

- 入口与 UI：
  - `src/app/mainwindow/MainWindow.BootstrapAndMenus.cpp`：顶部菜单“预览->导出视频...”与顶部栏按钮入口。
  - `src/app/mainwindow/MainWindow.cpp`：`MainWindow::onExportPreviewVideo()` 组装 `VideoExportTask`，触发导出对话框。
  - `src/tools/video_export/VideoExportDialog.cpp`：导出参数面板（输出路径/分辨率/时间戳选项/进度）。
- 控制器与管线：
  - `src/tools/video_export/VideoExportController.cpp`：
    - `VideoExportController::exportFullPreview()`：总流程（参数校验、SFX 混音、ffmpeg 启动、逐帧喂图、收尾校验）。
    - `chooseVideoEncoder()`：编码器探测与优先级选择。
    - `mixSfxTrackToWav()`：离线混音。
- 渲染复用与离屏渲染：
  - `src/preview/video/PreviewCanvas.h/.Runtime.cpp/.Render.cpp`：
    - `copyRenderStateFrom()`：复用实时预览资源与状态。
    - `initializeOffscreenRenderer()/renderOverlayFrameOffscreen()`：离屏 OpenGL 导出路径。
    - `renderCanvas(...)`：统一绘制入口（导出与实时共用）。
  - `src/preview/video/PreviewGLRenderer.cpp`：纹理采样策略（缓存纹理启用 mipmap + 三线性）。
- 打包与依赖：
  - `scripts/package-win.ps1`、`scripts/package-mac.sh`：将 ffmpeg 可执行文件打进 portable 包。
  - `third_party/ffmpeg/README.md`：多平台二进制来源、版本与校验记录。

### 8.3 关键参数与当前值

- 导出核心：
  - `kExportLeadInSeconds = 3.0`
  - `fps = 60`（当前固定）
  - 分辨率预设：`512/768/1024/1280/1536`（正方形画布）
- 编码器优先级（当前）：
  - HEVC：`hevc_nvenc -> hevc_qsv -> hevc_amf -> libx265`
  - 回退：`h264_nvenc -> h264_qsv -> h264_amf -> libx264 -> libopenh264 -> mpeg4`
- 码率估算（硬编/部分软编分支）：
  - `estimatedBitrateKbps = clamp(res^2 * fps * 0.075 / 1000, 2200, 8500)`
  - `maxRate = min(estimated*1.4, 10500)`，`bufsize = min(maxRate*2, 16000)`
- 导出进度条策略：
  - 渲染喂帧阶段上限 90%，ffmpeg finalize 阶段保持 90% 等待异步退出，避免 UI 假死观感。
- 画质与锯齿相关：
  - 物件缓存纹理 `GL_LINEAR_MIPMAP_LINEAR` + `glGenerateMipmap`。
  - 背景媒体纹理保持原路径（不启用该项），避免视频帧逐帧生成 mipmap 的额外开销。
  - 尺寸量化已关闭：`quantizeDimension()` 直接返回原尺寸。

### 8.4 运行日志与排障定位

- 导出日志默认路径：`%TEMP%/miacode_video_export.log`。
- 可通过环境变量覆写：`MIACODE_EXPORT_LOG_PATH`。
- 关键日志阶段：
  - `encoder_select`：编码器可用性与选型
  - `render_backend` / `offscreen_warmup`：离屏渲染是否生效
  - `frame_timing` / `frame_timing_summary`：逐帧耗时、重复帧、fallback 统计
  - `ffprobe_summary`：导出成品容器/流摘要

### 6.2 HoldEffect（hold sustain ring）

- Prefab 结构依据：`Hold_Effect.prefab` 只有 1 个 `ParticleSystem + ParticleSystemRenderer`，未发现独立“额外光圈底图”对象。
- 基准像素换算：
  - `sustainBasePixels = tapBasePixels * kJudgeEffectHoldSustainBaseRelativeToTap`。
  - 其中 `kJudgeEffectHoldSustainBaseRelativeToTap = (256 * 1.2) / 122`（Circle.png像素 * prefab缩放 / tap宽度）。
- 粒子时间计算：
  - `particleAge = fmod(elapsed + phase, lifetime)`。
  - `normalizedAge = particleAge / lifetime`。
- 粒子尺寸：`particleScale = sampleScalarCurve(kJudgeEffectHoldSustainSizeKeys, normalizedAge)`，再乘 `sustainBasePixels`。
- 粒子透明度：`sampleScalarCurve(kJudgeEffectHoldSustainAlphaKeys, normalizedAge)`。
- 贴图线条收紧：加载 hold 贴图后执行 `alpha' = pow(alpha, kJudgeEffectHoldSustainAlphaTightenGamma)`，用于“线条变细”而不改主色。
- 发光实现：沿用 tap 同风格的 edge glow（放大一层 + 本体一层）。

当前关键硬编码参数（Hold）：
- `kJudgeEffectHoldSustainLifetimeSeconds`：单粒子生命周期（当前实现值）。
- `kJudgeEffectHoldSustainParticleCount`：并发粒子数量。
- `kJudgeEffectHoldSustainBaseRelativeToTap`：hold 相对 tap 的基准尺寸。
- `kJudgeEffectHoldSustainAlphaTightenGamma`：贴图 alpha 收紧强度（>1 更细，=1 不处理）。
- `kJudgeEffectHoldSustainSizeKeys`：生命周期内尺寸曲线（线性插值）。
- `kJudgeEffectHoldSustainAlphaKeys`：生命周期内透明度曲线（线性插值）。
- `kJudgeEffectHoldSustainPhaseOffsets`：多粒子相位错开参数。每个粒子会先读取一个归一化相位 `phaseNorm`，再换算为 `phase = phaseNorm * lifetime`，并进入 `particleAge = fmod(elapsed + phase, lifetime)`；这样多个粒子会在同一条尺寸/透明度曲线上以不同起点循环。调大“错开程度”会让波纹更连续、减少整齐闪烁；若所有值都相同（例如全 0），粒子会几乎同相，视觉上更像一层同步明暗的“平涂圈”。

### 6.3 Alpha 曲线时间点说明（Hold）

- `kJudgeEffectHoldSustainAlphaKeys` 的时间点使用归一化时间（0~1）：
  - `0.09459098, 0.14285539, 0.50193027, 0.59652174, 0.66602579`。
- 采样方式：相邻 key 之间做线性插值，不使用样条。
- 因此“改 key 值”改变的是亮灭节奏形状；“改 lifetime”改变的是实际秒级周期长度。
