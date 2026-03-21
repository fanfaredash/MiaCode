# MiaCode 无理检测与 MaiMuriDX 风格渲染集成方案

本文件用于规划将 `MaiMuriDX` 的“动作仿真 + slide/wifi 动态擦除 + 提前整条擦除判定”能力原生集成到 `MiaCode`。

当前约束与目标如下：

- 渲染、导出、SFX 以当前 `MiaCode` 仓库为准。
- `MaiMuriDX` 的阈值类参数需要保留，但首阶段先写死在代码中，不做 UI 对接。
- 需要尽可能完整复现 `MaiMuriDX` 的特色功能。
- 需要保留现有渲染开关，并将 `MaiMuriDX` 风格渲染作为可选的实时预览 / 视频导出模式。
- 需要新增“无理检查”能力，定位方式接近语法检查，但展示位置优先放在 timeline 标签页。
- 当前仓库缺失“slide 提前被判定后整条提前擦除”的视觉表现，该效果需要明确纳入本次实现范围。

## 1. 已确认的现有基础

### 1.1 解析与数据承载

当前仓库已经具备 slide / wifi 区域几何与阈值数据，不需要从零重建：

- `TimelineNoteMarker` 已包含 `slideTrackAreaPoints`、`slideTrackAreaThresholds`、`slideTrackAreaCheckpoints`、`wifiTrackAreaPoints`、`wifiTrackAreaThresholds` 等字段。
- 位置：`src/timeline/TimelineView.h`
- slide / wifi 原生数据来自 `assets/generated/slide_native_data.json`
- 读取与填充位置：
  - `src/simai/parser/SimaiNativeParser.cpp`
  - `src/simai/parser/SimaiNativeParser.Slide.cpp`

### 1.2 预览与导出链路

当前仓库的实时预览和视频导出共享同一套 `PreviewCanvas` 渲染路径：

- 实时预览主路径：
  - `src/preview/video/PreviewCanvas.h`
  - `src/preview/video/PreviewCanvas.cpp`
  - `src/preview/video/PreviewCanvas.Runtime.cpp`
  - `src/preview/video/PreviewCanvas.Objects.cpp`
- 视频导出入口：
  - `src/tools/video_export/VideoExportController.cpp`

这意味着只要 `PreviewCanvas` 能消费一套统一的“无理分析结果 + 渲染选项”，实时预览和导出都可以直接复用。

### 1.3 现有 UI 与开关

当前仓库已存在以下预览开关：

- `showSlideTracks`
- `showJudgeMarkers`
- `showTouchTrail`

状态与入口位于：

- `src/app/mainwindow/MainWindow.h`
- `src/app/mainwindow/sections/preview/MainWindow.PreviewSessionFlow.cpp`
- `src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp`

本次新增 `MaiMuriDX` 风格渲染模式时，应保留这些开关，并让它们继续作用于新模式。

### 1.4 比例与逻辑空间

当前仓库已经定义了以 `540` 为基准的逻辑画布：

- `src/common/PreviewGameplayConfig.h`

迁移 `MaiMuriDX` 阈值时，所有半径、距离、区域判定相关常量都应落在该逻辑空间中，不能直接按窗口像素或 `pygame` 屏幕尺寸照搬。

## 2. 目录与职责划分

### 2.1 `src/common`

用于放置多方共用、无 UI 依赖的常量、类型与接口。

建议新增文件：

- `src/common/MuriConfig.h`
- `src/common/MuriTypes.h`
- `src/common/MuriRenderOptions.h`

职责：

- `MuriConfig.h`
  - 保存 `MaiMuriDX` 默认阈值
  - 保存 tick、时间基准、比例换算说明
  - 将所有空间参数统一映射到 `540` 逻辑空间
- `MuriTypes.h`
  - 定义 `MuriKind`
  - 定义 `AreaJudgeCause`
  - 定义 `SlideAreaEraseRecord`
  - 定义 `WifiAreaEraseRecord`
  - 定义 `MarkerMuriState`
  - 定义 `MuriDiagnostic`
  - 定义 `MuriAnalysisReport`
- `MuriRenderOptions.h`
  - 定义 `RenderMode`
  - 定义 `showSlideTracks`
  - 定义 `showJudgeMarkers`
  - 定义 `showTouchTrail`

说明：

- `src/common` 只放共享定义，不放仿真算法实现。
- `PreviewCanvas`、`VideoExportController`、`MainWindow` 都应只依赖这里提供的统一接口类型。

### 2.2 `src/tools/muri`

用于放置无理检测核心逻辑，不依赖 QWidget，不包含渲染代码。

建议新增文件：

- `src/tools/muri/MuriActions.h`
- `src/tools/muri/MuriActions.cpp`
- `src/tools/muri/MuriActionBuilder.h`
- `src/tools/muri/MuriActionBuilder.cpp`
- `src/tools/muri/MuriAreaJudge.h`
- `src/tools/muri/MuriAreaJudge.cpp`
- `src/tools/muri/MuriJudgeEngine.h`
- `src/tools/muri/MuriJudgeEngine.cpp`
- `src/tools/muri/MuriAnalyzer.h`
- `src/tools/muri/MuriAnalyzer.cpp`

职责：

- `MuriActions`
  - 对应 `MaiMuriDX` 中的 `ActionPress / ActionSlide / ActionExtraPadDown`
  - 提供基于时间的触点位置、半径、方向采样
- `MuriActionBuilder`
  - 从 `TimelineNoteMarker` 生成动作序列
  - 处理 slide head、tail on slide head、touch on slide、wifi 三路动作
- `MuriAreaJudge`
  - 负责 slide / wifi 区域推进
  - 处理 checkpoint、skip、不可跳区规则
  - 输出每个区域何时、因什么被擦掉
- `MuriJudgeEngine`
  - 提供整张谱面的 tick 主循环
  - 维护 pad down / up 状态
  - 驱动动作判定与区域推进
  - 产出 `SlideTooFast` 等结果
- `MuriAnalyzer`
  - 提供外部统一入口
  - 将 `TimelineNoteMarker` 转为分析结果
  - 输出 `MuriAnalysisReport`

说明：

- `src/tools/muri` 是本次功能的核心。
- 这里的输出必须同时满足三个消费方：
  - timeline 的“无理检查”
  - 实时预览
  - 视频导出

### 2.3 `src/preview/video`

用于把无理分析结果渲染出来。

主要修改文件：

- `src/preview/video/PreviewCanvas.h`
- `src/preview/video/PreviewCanvas.cpp`
- `src/preview/video/PreviewCanvas.Runtime.cpp`
- `src/preview/video/PreviewCanvas.Objects.cpp`

职责：

- 接收 `MuriAnalysisReport`
- 接收 `MuriRenderOptions`
- 在 `Native` 与 `MaimuriDxStyle` 两种模式之间切换
- 在 `MaimuriDxStyle` 模式下按分析结果动态裁切 slide / wifi 轨道
- 在新模式下保留 `showJudgeMarkers` 与 `showTouchTrail`

### 2.4 `src/tools/video_export`

用于复用实时预览的渲染结果进行导出。

主要修改文件：

- `src/tools/video_export/VideoExportController.cpp`

职责：

- 将同一份 `MuriAnalysisReport` 和 `MuriRenderOptions` 下发给导出用 `PreviewCanvas`
- 确保实时预览与导出帧结果一致

## 3. 统一接口设计

本次接入的关键点不是“在哪里算”，而是“算出来后如何被多方共用”。

### 3.1 分析结果接口

建议在 `src/common/MuriTypes.h` 中统一定义：

- `MarkerAnalysisKey`
- `SlideAreaEraseRecord`
- `WifiAreaEraseRecord`
- `MarkerMuriState`
- `MuriDiagnostic`
- `MuriAnalysisReport`

建议 `MuriAnalysisReport` 至少包含：

- `QHash<QString, MarkerMuriState> markerStates`
- `QVector<MuriDiagnostic> diagnostics`
- `QString sourceSignature`

说明：

- `markerStates` 使用稳定 key 回挂到 `TimelineNoteMarker`
- 避免直接把大量运行态字段塞入 `TimelineNoteMarker`

### 3.2 渲染选项接口

建议在 `src/common/MuriRenderOptions.h` 中定义：

- `enum class RenderMode { Native, MaimuriDxStyle };`
- `bool showSlideTracks`
- `bool showJudgeMarkers`
- `bool showTouchTrail`

说明：

- 预览和导出都只使用这一个渲染选项对象
- 避免一边用布尔值、一边用字符串，导致状态不一致

### 3.3 marker 稳定标识

需要提供稳定的 marker key 构造函数，用于把分析结果与解析结果关联起来。

建议组合字段：

- `type`
- `second`
- `lane`
- `endLane`
- `sourceLine`
- `sourceCol`
- `slideTrackKey`

建议封装为：

- `QString makeMarkerAnalysisKey(const TimelineNoteMarker& marker);`

## 4. 需要修改的现有文件

### 4.1 `src/simai/parser/SimaiNativeParser.Driver.cpp`

用途：

- 补齐与 `MaiMuriDX` 更一致的后处理规则

重点：

- 补强 `beforeSlide / afterSlide`
- 校正 `slideHead / tailOnSlideHead / onSlide / slideEach`
- 处理当前注释中提到的复杂 embedded-track merge checks 尚未移植的问题

说明：

- 这是后续无理分析正确性的前置条件
- 如果这一步不补齐，后面的仿真结果会在边界情况与 `MaiMuriDX` 偏离

### 4.2 `src/app/mainwindow/MainWindow.h`

建议新增状态：

- `MuriAnalysisReport muriAnalysisReport_;`
- `MuriRenderOptions muriRenderOptions_;`

作用：

- 让 preview、timeline、export 共享同一份状态

### 4.3 `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`

用途：

- 作为总调度入口

建议改动：

- 在 `refreshTimelineMetadata()` 成功解析后调用 `MuriAnalyzer`
- 将分析结果缓存到主窗口状态
- 将分析结果同时分发给：
  - timeline
  - `PreviewCanvas`
  - 视频导出

说明：

- 这是最适合挂载“无理检查”的统一入口

### 4.4 `src/preview/video/PreviewCanvas.h`

建议新增接口：

- `void setMuriAnalysisReport(const MuriAnalysisReport& report);`
- `void setMuriRenderOptions(const MuriRenderOptions& options);`

### 4.5 `src/preview/video/PreviewCanvas.cpp`

建议修改：

- 将当前固定的 slide 轨道裁切模式从“编译期常量”收敛为“实例运行时策略”
- 保留现有 `Native` 行为
- 增加 `MaimuriDxStyle` 分支

### 4.6 `src/preview/video/PreviewCanvas.Runtime.cpp`

建议修改：

- 保证设置 note markers 后，可以同步设置无理分析结果
- 切谱、切难度、重载预览时清理过期分析结果

### 4.7 `src/preview/video/PreviewCanvas.Objects.cpp`

用途：

- 这里是 slide / wifi 轨道绘制与效果叠加的主战场

建议改动：

- `drawSlideTrack()`：
  - `Native` 模式下保持现有逻辑
  - `MaimuriDxStyle` 模式下根据 `SlideAreaEraseRecord` 逐区裁切
- `drawWifiTrack()`：
  - 同样根据分析结果逐区裁切
- 将 `showJudgeMarkers` 与 `showTouchTrail` 接入新模式

### 4.8 `src/tools/video_export/VideoExportController.cpp`

建议改动：

- 读取主窗口或导出参数中的 `MuriRenderOptions`
- 将 `MuriAnalysisReport` 与 `MuriRenderOptions` 一起下发给导出使用的 `PreviewCanvas`

说明：

- 目标是“预览看到什么，导出就是什么”

### 4.9 `src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp`

建议改动：

- 增加渲染模式选择入口：
  - `Native`
  - `MaimuriDxStyle`
- 保留并继续使用：
  - `showSlideTracks`
  - `showJudgeMarkers`
  - `showTouchTrail`

### 4.10 `src/app/mainwindow/MainWindow.cpp`

建议改动：

- 持久化 `MuriRenderOptions`
- 渲染模式切换后刷新 preview 与 timeline
- 如果导出界面存在单独参数面板，也应同步该模式

### 4.11 timeline 相关文件

建议修改：

- `src/timeline/TimelineView.h`
- `src/timeline/TimelineView.Paint.cpp`
- `src/timeline/TimelineView.Interaction.cpp`

用途：

- 给 timeline 上的 marker 增加轻量无理标记
- 点击或定位时与“无理检查”列表联动

说明：

- timeline 侧只做轻量提示，不建议首版就把复杂判定区域可视化塞进去

## 5. “slide 提前被判定”视觉效果的专项安排

这是当前仓库明确缺失、且本次必须补齐的内容。

### 5.1 现状

当前仓库已有 slide / wifi 轨道显示与一般裁切逻辑，但缺少以下效果：

- slide 头部尚未正常到达终点时，因仿真判定导致整条轨道提前被擦除的视觉表现
- 提前整条擦除时的“被判定瞬间”反馈
- 对应的 judge marker / touch trail 与轨道消失之间的同步表现

### 5.2 首版建议

首版不依赖额外外部素材，优先用当前仓库已有渲染能力做出可读性强的视觉反馈：

- 轨道逐区擦除：
  - 根据 `SlideAreaEraseRecord` 实时缩短轨道
- 提前整条擦除瞬间反馈：
  - 当命中 `SlideTooFast` 时，对整条轨道做一次短时高亮闪烁或 alpha 衰减
- 被擦除区域残影：
  - 可选做 80ms 到 160ms 的轻微残影，帮助玩家看清是“判定导致消失”，而不是普通时间裁切

建议实现位置：

- `src/preview/video/PreviewCanvas.Objects.cpp`

### 5.3 后续增强版

如果首版视觉效果仍不足以表达“提前判定”，再考虑在当前仓库内新增少量本地资产，而不是直接复用 `MaiMuriDX` 的 `pygame` 图：

建议可选新增资产目录：

- `assets/effects/muri/slide_erase_flash.png`
- `assets/effects/muri/slide_erase_ring.png`

建议接入文件：

- `src/preview/video/PreviewCanvas.SkinAndAtlas.cpp`
- `src/preview/video/PreviewCanvas.Objects.cpp`

说明：

- 这些资产应作为 `MiaCode` 自己的效果资源管理
- 不建议把 `MaiMuriDX` 的图像文件直接搬进来

## 6. “无理检查”功能落地方式

### 6.1 定位

“无理检查”语义上接近语法检查，但不应与语法错误混为一类。

建议：

- 解析 / 语法错误继续放原校验区域
- “无理检查”作为 timeline 标签页中的独立列表或分组面板

### 6.2 数据来源

统一来自：

- `src/tools/muri/MuriAnalyzer.*`

产物：

- `MuriDiagnostic`

每条诊断至少包含：

- 类型
- 时间
- 对应 note 的 `line / col`
- 摘要文本
- 可选扩展信息

### 6.3 UI 行为

建议行为：

- 点击诊断，跳转到文本行列
- 点击诊断，同时将 timeline 居中到对应时间
- 对有问题的 marker 显示轻量警示色或图标

## 7. 分阶段实施顺序

### 第一阶段：纯逻辑对齐

目标：

- 不改 UI
- 不改导出
- 先得到可信的 `MuriAnalysisReport`

执行文件：

- `src/common/MuriConfig.h`
- `src/common/MuriTypes.h`
- `src/common/MuriRenderOptions.h`
- `src/tools/muri/*`
- `src/simai/parser/SimaiNativeParser.Driver.cpp`

验收标准：

- 能对典型谱面输出 slide / wifi 区域擦除记录
- 能输出 `SlideTooFast`
- 能生成可定位到 note 的 `MuriDiagnostic`

### 第二阶段：实时预览接入

目标：

- 增加 `MaimuriDxStyle` 渲染模式
- 在实时预览中正确表现 slide / wifi 动态擦除与提前整条擦除

执行文件：

- `src/preview/video/PreviewCanvas.h`
- `src/preview/video/PreviewCanvas.cpp`
- `src/preview/video/PreviewCanvas.Runtime.cpp`
- `src/preview/video/PreviewCanvas.Objects.cpp`

验收标准：

- 模式切换生效
- `showSlideTracks`、`showJudgeMarkers`、`showTouchTrail` 在新模式中仍可控
- 能看见“提前整条擦除”的视觉表现

### 第三阶段：视频导出接入

目标：

- 导出与实时预览保持一致

执行文件：

- `src/tools/video_export/VideoExportController.cpp`

验收标准：

- 同一时间点截图，预览与导出帧一致

### 第四阶段：timeline 无理检查接入

目标：

- 在 timeline 标签页落地“无理检查”

执行文件：

- `src/app/mainwindow/MainWindow.h`
- `src/app/mainwindow/MainWindow.cpp`
- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- `src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp`
- `src/timeline/TimelineView.h`
- `src/timeline/TimelineView.Paint.cpp`
- `src/timeline/TimelineView.Interaction.cpp`

验收标准：

- 能看到无理检查列表
- 点击可跳转
- timeline 上有轻量提示

## 8. 测试与回归建议

建议新增：

- `src/tools/simai_parser/MuriJudgeSpec.cpp`
- `tests/charts/` 或等价测试数据目录

测试内容至少覆盖：

- slide 提前整条擦除
- wifi 三路擦除
- touch on slide
- tail on slide head
- beforeSlide / afterSlide
- 同时起始的多条 slide

## 9. 非目标

以下内容不在本次首阶段范围：

- 阈值参数的 UI 配置
- 迁移 `MaiMuriDX` 的 `pygame` 资产
- 迁移 `MaiMuriDX` 的 SFX 目录与音频混音逻辑
- 保留对外部 Python preview session 的依赖作为主路径

## 10. 当前结论

本次功能集成的正确方向是：

- `src/common` 放共享参数与接口
- `src/tools/muri` 放完整的无理仿真判定逻辑
- `src/preview/video` 与 `src/tools/video_export` 只消费统一结果
- timeline 标签页承载“无理检查”
- `MaimuriDxStyle` 作为实时预览 / 导出的可选模式
- “slide 提前被判定后整条提前擦除”的视觉效果作为明确交付项，不再视为后续可选项
