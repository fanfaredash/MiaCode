# Timeline 图层栈与滑动条堆叠顺序规格

本文档记录 beta21 期间确定的两条相关运行时不变量，期间经历了多轮调试才稳定：

1. **Timeline 渲染图层顺序** —— 在 DComp（默认）和 QSG 回退两条渲染路径中，
   各层之间的绘制顺序。
2. **Video Settings 中的 "Slide Stacking Order" 选项** —— 它真正控制什么、
   绝对不能影响什么。

这两个区域反复产生过视觉 bug（线被波形遮挡；切换选项时滑动条头星顺序乱跳）。
当你修改以下任一处时，请把本文档当作权威参考：

- `src/sources/timeline/Timeline*Source.{h,cpp}` —— DComp pipeline
- `src/timeline/quick/TimelineQuick*Layer.{h,cpp}` —— QSG pipeline
- `src/core/scene/PreviewPreparedSceneCache.{h,cpp}` —— 预制 draw rank
- `src/core/scene/Preview*LayerState.cpp` —— 各层排序逻辑

## 1. 两条并行的 Timeline 渲染路径

Timeline 可以走两个后端中的一条，由 `MIACODE_TIMELINE_USE_DCOMP` 环境变量控制
（默认：DComp 预览启用时为 **true**，自 beta20 起为默认开启状态）。

| 环境变量状态 | 活跃路径 | 实现根入口 |
|---|---|---|
| `MIACODE_TIMELINE_USE_DCOMP=1`（默认） | **DComp** —— D3D11 子 HWND, IPreviewSource pipeline | `TimelineRenderView` 位于 `src/render/backend_d3d11/` |
| `MIACODE_TIMELINE_USE_DCOMP=0` | **QSG** —— Qt Quick Scene Graph 图层 | `TimelineQuickItem` 位于 `src/timeline/quick/` |

两条路径都消费同一份 `TimelineSceneState`（由 `TimelineSceneStateBuilder`
构建）和同一份调色板（`UiTheme.cpp`）；它们仅在 *如何* 把图层合成到屏幕上有差别。
**下文所有图层顺序与颜色路由规则在两条路径中并行适用。** 如果某次修复只覆盖
一条路径，那这次修复就是不完整的。

## 2. Timeline z-stack —— DComp 源 pipeline

`TimelineRenderView` 在
`src/render/backend_d3d11/TimelineRenderView.cpp::ensureCompositorInitialized`
中注册一系列 `IPreviewSource`。每个 source 通过固定的 `zOrder()` 决定渲染顺序：

| zOrder | Source | 头文件 | 它输出什么 |
|---|---|---|---|
| **0** | `TimelineGridSource` | `src/sources/timeline/TimelineGridSource.h` | `baseBackgroundRects`（timeline / sidebar / header 填充）, `frameRects`, `frameLines` |
| **1** | `TimelineWaveformSource` | `TimelineWaveformSource.h` | `waveformBars` —— 不透明的音频振幅矩形 |
| **2** | `TimelineLaneOverlaySource` | `TimelineLaneOverlaySource.h` | `laneOverlayRects` —— 半透明的行条带填充（α=190 浅 / α=210 深），用来减弱波形 |
| **3** | `TimelineGridLinesSource` | `TimelineGridLinesSource.h` | `gridLines` —— bar 线（每小节）和每逗号 note 刻度 |
| **4** | `TimelineNotesSource` | `TimelineNotesSource.h` | `fireworkBands`、轨道精灵、hold span、touch-hold 线、muri 点、note 精灵 |
| **5** | `TimelineHeaderSource` | `TimelineHeaderSource.h` | sidebar mask、frame line 重新发射、header markers（小节序号三角形 + 行号标签） |
| **6** | `TimelineOverlaySource` | `TimelineOverlaySource.h` | playhead、cursor、drag-center 十字线、lane labels |

需要记住两条结构性规则：

1. **Lane overlay 必须坐在 waveform 与 grid lines 之间。** 它的半透明填充
   是用来减弱波形，**不**是用来减弱 bar 线。如果你需要新增另一种半透明装饰，
   且它不应该影响 bar 线，请把它放在 z ≤ 2 的位置。
2. **Grid lines 必须位于 waveform 之上、lane overlay 之上。**
   `c.timelineGridMajor`（不透明）+ `c.timelineGridMinor`（半透明）这两个调色板
   入口都是按对 lane fill 渲染设计的，而不是对 waveform。

### 历史 —— 为什么 zOrder 长这样

- beta21 之前：`TimelineGridSource` 自己在 z=0 输出 `gridLines`。
  结果是：bar/note 线在所有其他元素之前绘制，于是 waveform、lane overlay 和
  notes 全都画在它们之上。`c.timelineGridMajor` 只能作为残留色透过 lane overlay
  的 α=190/210 显出来。用户视觉感受是「线太淡 / 线在波形之下 / 在繁忙波形段
  完全看不见」。
- beta21-fix7：把 `gridLines` 拆分到 `TimelineGridLinesSource` 放在 z=2，
  Notes/Header/Overlay 的 z 各往后移一位。
- beta21-fix12：把 `laneOverlayRects` 从 `TimelineNotesSource` 中拆出来，
  作为 `TimelineLaneOverlaySource` 放在 z=2；`TimelineGridLinesSource` 推到 z=3。
  这是最终的结构性修复 —— 之前 lane overlay *也* 在画在 bar 线之上，因为它
  寄居在 z=3 的 NotesSource 里，比当时位于 z=2 的 grid lines 更晚绘制。

## 3. Timeline z-stack —— QSG 图层 pipeline（回退）

`TimelineQuickItem::updatePaintNode` 遍历一个固定 slot 列表。Slot 顺序与 DComp
z-stack 一致：

| Slot | Layer | 头文件 |
|---|---|---|
| 0 | `gridLayer`（basebackgrounds） | `TimelineQuickGridLayer.h` |
| 1 | `waveformLayer` | `TimelineQuickWaveformLayer.h` |
| 2 | `headerLayer`（lane overlays + 小节序号三角形 + 标签 —— **不含 grid lines**） | `TimelineQuickHeaderLayer.h` |
| 3 | `gridLinesLayer`（bar 线 + note 线，独立 slot） | `TimelineQuickGridLinesLayer.h` |
| 4 | `notesLayer` | `TimelineQuickNotesLayer.h` |
| 5 | `overlayLayer` | `TimelineQuickOverlayLayer.h` |

Slot 数量为 `kTimelineLayerSlotCount = 6`，定义于
`src/timeline/quick/TimelineQuickItem.cpp`。

### QSG 特有陷阱 —— α=255 线的 `Blending` flag

Qt 6 RHI batched renderer 允许在不同 slot 之间为 *不透明*（`Blending=false`）
的 draw 重排序，以实现 early-Z 优化。通过 `QSGFlatColorMaterial` 用 α=255
颜色绘制的 bar 线默认会落入 opaque batch —— 而在 D3D11 上，这个 batch 可能
会比 waveform 的 opaque batch *更早* 提交，即使 bar 线的 scene-graph slot
位置更高。

`TimelineQuickFlatColorBatchBuilder::flush()`（位于
`src/timeline/quick/TimelineQuickLayerUtils.cpp`）**强制对每个 emit 的 material
调用 `material->setFlag(QSGMaterial::Blending, true)`**。强行走 translucent path
能保证绘制顺序严格跟随 scene-graph 顺序。**不要移除这一行** —— 一旦移除，
opaque-batch 重排会让 QSG 路径下「线在波形之下」的症状重现。

## 4. 颜色路由 —— 哪个调色板入口对应哪条线

两条渲染路径读同一份调色板。截至 beta21：

| 屏幕上的元素 | 调色板入口 | 定义于 |
|---|---|---|
| sidebar 边界线（timeline 内容区左边缘） | `c.timelineAxis` | `UiTheme.cpp` |
| **bar 线（每小节）** | **`c.timelineGridMajor`** | `UiTheme.cpp` |
| **note 线（每逗号）** | **`c.timelineGridMinor`** | `UiTheme.cpp` |
| lane 行条带 | `c.timelineLaneEven` / `c.timelineLaneOdd` | `UiTheme.cpp` |
| lane 序号 / 行号标签 | `c.timelineLabel` | `UiTheme.cpp` |
| Frame border lines | `c.timelineBorder` | `UiTheme.cpp` |

布线写在两个文件里；它们必须保持一致：

- DComp 路径：`src/common/TimelineThemeConfig.h::timelineThemeColors()`
  把 `theme.gridMajor` 设成 `c.timelineGridMajor`，把 `theme.gridMinor` 设成
  `c.timelineGridMinor`。`TimelineSceneStateBuilder` 再把它们写入
  `state.gridLines`。
- QSG 回退：`src/timeline/TimelineView.Paint.cpp::paintEvent()`
  从 `c.timelineGridMajor` 构造 `majorBeatPen`，从 `c.timelineGridMinor`
  构造 `minorBeatPen`。

要重新调整 bar/note 线的颜色，**只改** `UiTheme.cpp` 中
`timelineGridMajor` / `timelineGridMinor` 的调色板值。两条路径会自动接收新值。

## 5. "Slide Stacking Order" Video Settings 选项

UI：Video Settings 对话框 → Gameplay 分组 → "Slide Stack Order"，两个选项
"DX Style" 和 "FiNALE Style"。在 JSON 里持久化为
`slide_earlier_second_and_text_on_top`（bool），通过
`PreviewFrameState::render::slideEarlierSecondAndTextOnTop` 传播。

### 作用域（这个选项 *会* 控制什么）

该选项决定 **slide 轨道层族 ** 内部的堆叠顺序，仅此而已。具体来说：

- slide *track*（滑动箭头轨迹）—— `PreviewTrackLayerState.cpp`
- slide *motion* sprites（动画箭头头部）——
  `PreviewSlideMotionLayerState.cpp`
- 在 `PreviewPreparedSceneCache::rebuild` 中给 `slideLikeLayer_` 窗口缓存的
  prepared draw rank

当 `earlierOnTop = true`（DX Style）时，时间靠前的 slide 轨道画在靠后的之上；
FiNALE 反过来。在单一 layer 内的多个重叠 slide 之间，这是纯粹的 layer 内
堆叠决策。

### 越界（这个选项 *绝不能* 控制什么）

- **Slide head 头星 / head layer。** Heads 必须是确定性的、与选项无关。
  存在两个执行点：
  1. `PreviewHeadLayerState.cpp` 第 310–322 行强制
     `kHeadStarAlwaysEarlierOnTop = true`，作用于 fallback（未走缓存）的
     head layer 排序路径。
  2. `PreviewPreparedSceneCache.cpp::rebuild` 在为 `headLayer_` 计算 prepared
     draw rank 时强制 `kHeadLayerAlwaysEarlierOnTop = true`。这是缓存模式
     下对应第 1 点的镜像；两者必须同时成立，否则切换选项就会让某颗 head 星
     的渲染顺序乱跳。
- 任何非 track 的 layer：touch、judge effect、judge firework、hold body、
  chart-review markers、muri overlay、header markers。它们**都不应**读
  `slideEarlierSecondAndTextOnTop`。

审计确认（截至 beta21-fix13），仅以下调用点读
`state.render.slideEarlierSecondAndTextOnTop`：

- `PreviewTrackLayerState.cpp:98`
- `PreviewSlideMotionLayerState.cpp:94`
- `PreviewPreparedSceneCache.cpp:382`（slideLikeLayer 重建）
- `PreviewPreparedSceneCache.cpp:129`（cache key —— 用于失效，不用于排序）

如果将来出现第五个调用点，请仔细审查 —— 默认假设是任何新加入的 layer
**都不应**消费这个 flag。

### 缓存失效行为

该 flag 是 `PreviewPreparedSceneCacheKey` 的一部分，所以切换 Video Settings
里的选项会重新生成缓存键并强制完全重建。这是有意为之 —— slide 轨道精灵
需要新的 rank。但缓存重建时，head layer 用的是它自己*固定*的 DX 规则，
所以切换前后 head sprite 会得到完全相同的 rank。从用户视角看：slide stack
顺序变了；head 星顺序保持不变。

## 6. 当 bar/note 线表现不符合预期时的诊断清单

如果 bar 线或每逗号 note 线渲染出意外的颜色、位置或堆叠，按以下顺序排查：

1. **确认当前活跃路径。** `MIACODE_TIMELINE_USE_DCOMP` 环境变量或默认开启
   （DComp）。DComp 路径用 IPreviewSource z-stack；QSG 路径用 slot 列表。
   仅修复 QSG 在 DComp 路径上不会生效，反之亦然。
2. **确认 grid lines 真的被发射了。** 在活跃路径里搜
   `gridLines` 的消费者。截至 beta21，DComp 仅有一个消费者：
   `TimelineGridLinesSource`（z=3）；QSG 仅有：`TimelineQuickGridLinesLayer`
   （slot 3）。出现别的就是有人重复发射了。
3. **确认 Blending 是开的（仅 QSG）。** 执行
   `git grep "Blending, true" src/timeline/quick/TimelineQuickLayerUtils.cpp`
   仍应在 `TimelineQuickFlatColorBatchBuilder::flush()` 里命中。移除它会让
   QSG 路径的顺序回退。
4. **确认调色板布线。** bar 线 → `c.timelineGridMajor`，note 线 →
   `c.timelineGridMinor`。`git grep "c\.timelineGrid"` 应仅命中
   `TimelineThemeConfig.h` 和 `TimelineView.Paint.cpp`。

## 7. 当 slide head 星在切换选项时乱跳的诊断清单

1. 确认 `PreviewHeadLayerState.cpp:322` 仍是
   `constexpr bool kHeadStarAlwaysEarlierOnTop = true;`。
2. 确认 `PreviewPreparedSceneCache.cpp:rebuild` 在为 `headLayer_` 调用
   `rebuildPreviewPreparedMarkerDrawOrder` 时仍传
   `kHeadLayerAlwaysEarlierOnTop = true`（**不是**
   `state.render.slideEarlierSecondAndTextOnTop`）。
3. 重新审计 `slideEarlierSecondAndTextOnTop` 的所有消费者：
   ```
   git grep -n "slideEarlierSecondAndTextOnTop" src/core/ src/preview/
   ```
   预期命中：PreviewFrameState.h（声明）、PreviewRuntime.cpp（外部 setter）、
   PreviewPreparedSceneCache 的 cache key + slideLikeLayer 重建、
   PreviewTrackLayerState、PreviewSlideMotionLayerState。其他都是回归。
