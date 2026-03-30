<!-- translation-source: .codex/skills/miacode-dev-guide/references/cross-chain-linkage.md -->
<!-- translation-source-hash: 3535629d72d5fd2358db645c64edca6f371779a37ff85e45c92ac7da19528997 -->
<!-- 说明：这是中文镜像，不作为 Codex skill 入口加载。 -->

# 跨链路联动

在修改会跨 parser、preview、audio、export 或工具链传播的行为前，先看这份文件。

## 1. 编辑 -> 解析 -> 时间轴 -> 预览

主链路：

1. 编辑器 `contentsChange`，或 `scheduleTimelineRefresh` 这样的显式全量刷新入口
2. `MainWindow::applyTimelineQuickChange` / `MainWindow::refreshTimelineQuickModelFromCurrentText`
3. `TimelineQuickModel::applyContentsChange` 或 `TimelineQuickModel::rebuildFromText`
4. `TimelineView::setTimelineData`
5. `MainWindow::requestTimelineSlowRefresh`
6. `buildTimelineSlowRefreshResult`
7. 位移后的 beat/note markers、validation report，以及预览统计输入
8. `QtPreviewSfxRuntime::configureTimeline`
9. `MainWindow::scheduleDeferredMuriRefresh`
10. `buildTimelineMuriRefreshResult`
11. `PreviewCanvas::setNoteMarkers`

含义：

- parser 改动通常不是 parser-only。
- 一个新的 note 属性或时序规则，通常还要联查 timeline、preview、audio、export 和 Muri。
- `TimelineQuickModel` 现在负责“只认逗号”的 `C` 锚点寻址，覆盖编辑器光标同步、Timeline 头部 / `Ctrl+点 Timeline` 的 `R -> C` 跳转，以及播放中的 Follow 绑定。
- 当预览正在播放时，slow refresh 产生的 note-marker 更新仍会继续驱动 validation 和 Timeline 侧诊断，但 preview audio / canvas / 物件统计会继续使用点击播放时冻结的快照，直到本次播放停止。

## 2. `&first` 与时间偏移链

当前约定：

- `SimaiDocument` 存储原始 `first`。
- `MainWindow::parsedFirstSeconds` 是主 getter。
- `TimelineQuickModel` 会在每次快路径全量重建或增量编辑应用时接收 `first`。
- `buildTimelineSlowRefreshResult` 会按 `first` 平移 parser 产出的 beat/note markers。
- `MainWindow::applyLatencyDetectorOffset` 会把原始 `first` 写回文档。
- `LatencyDetectorDialog` 读写原始值，不维护一个反相的影子值。
- 导出任务重建时，会再次使用解析后的文档文本和位移后的 markers。

如果你要改 `&first` 语义，至少要联查：

- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
- `src/tools/latency/LatencyDetectorDialog.Analysis.cpp`
- `src/tools/video_export/VideoExportSnapshot.cpp`
- `DEVELOPMENT_PLAN.md` 第 11 节

## 3. 实时 SFX 与导出 SFX 必须同步

当前同步对：

- 实时：`src/preview/audio/QtPreviewSfxRuntime.Timeline.cpp`，`QtPreviewSfxRuntime::configureTimeline`
- 导出：`src/tools/video_export/VideoExportController.cpp`，`buildSfxTimeline`

共同关心的点：

- 各 note 类型会发出哪些 `answer`、`judge`、`break`、`ex`、`touch`、`touchhold`、`firework`
- slide/wifi 头星行为
- `sameHeadSlide` 行为
- `trackBreak` 与 `headBreak` 的区别
- touchhold 持续段语义
- firework 的触发时刻偏移

改一边就要同 patch 检查另一边。

## 4. 背景媒体解析逻辑有两份

当前重复实现：

- 预览侧：`PreviewMediaController::resolveMediaPath`
- 导出侧：`VideoExportController.cpp` 里的 `resolveBackgroundMediaPath`

当前文件名约定：

- `bg.mp4`
- `pv.mp4`
- `bg.jpg`
- `bg.png`
- `bg.jpeg`

如果新增或移除支持的媒体命名，预览和导出都要同步。

## 5. 音轨路径解析分布在多个位置

当前归属点：

- `MainWindow::resolveDefaultTrackPath`
- `MainWindow::resolveLatencyDetectorTrackPath`
- `QtPreviewSfxRuntime::resolveTrackPath`

当前约定：

- 谱面目录同级的 `track.mp3`
- 主窗口导出路径里还允许通过 `MIACODE_TRACK_PATH` 做环境变量覆盖

如果你要支持新的音轨文件名或新的查找规则，要把相关归属点和 `assets-and-tools.md` 一起更新。

## 6. 皮肤与资源查找会流到预览和导出两边

资源根：

- `miacode::assets::findAssetRoot`
- `miacode::assets::assetPath`

预览侧消费者：

- `MainWindow::resolvePreviewSkinDir`
- `PreviewCanvas::setSkinDirectory`
- `miacode::preview_sfx::resolveSfxDirectory`

导出侧消费者：

- `MainWindow::buildVideoExportSnapshot`
- `VideoExportSnapshot::buildVideoExportTaskFromSnapshot`
- `VideoExportController::exportPreparedTask`

改皮肤或 SFX 查找规则时，要同时看预览与导出。

## 7. 导出 Snapshot 边界本身就是契约

导出 worker 边界：

1. `MainWindow::buildVideoExportSnapshot`
2. `VideoExportSnapshot::toJson`
3. `runCliVideoExportWorker`
4. `VideoExportSnapshot::fromJson`
5. `buildVideoExportTaskFromSnapshot`
6. `VideoExportController::exportPreparedTask`

含义：

- 新的导出设置必须同时加到序列化和反序列化两侧。
- worker 协议一旦变化，`main.cpp` 和 MainWindow 的 worker 事件处理都要改。

## 8. 共享渲染状态会贯穿预览与导出

Wifi 补充说明：

- `RenderMode::MaimuriDxStyle` 下的 wifi 轨道擦除不是由静态 `wifiTrackAreaCheckpoints` 驱动。
- `MuriAnalyzer` 会把运行时三轨进度写入 `MarkerMuriState::wifiLaneProgressSeconds`，把已判定的三轨 area 镜像到 `MarkerMuriState::wifiLaneAreas`，并把实际的 `C` 抬手时刻写入 `MarkerMuriState::wifiPadCSecond`。
- `PreviewCanvas::drawWifiTrack` 必须按三轨里最慢的一轨来裁切共用的中线轨道，优先使用 `wifiLaneProgressSeconds`，在进度数组不可用时回退到 `wifiLaneAreas`。
- 在 `RenderMode::MaimuriDxStyle` 下，wifi 轨道一旦按运行时状态完成擦除，就不应再叠加整条轨道的 flash 回补；如果 `wifiNeedC` 开启，则最后一个 area 仍要一直保留到 `wifiPadCSecond`。

共享渲染设置包括：

- 背景亮度 outer / inner
- layout square scale
- smooth brightness
- background scale mode
- note flow speed
- 谱面确认判定 overlay 的 slide/wifi 类与 tap/hold 类开关
- timestamp / object-stats HUD 开关
- Muri render options

归属点：

- 持久化：`MainWindow::loadProjectRenderState`、`saveProjectRenderState`
- 预览应用：`PreviewCanvas` 的 setter 和 `PreviewMediaController`
- 导出应用：`MainWindow::buildVideoExportSnapshot`、`buildVideoExportTaskFromSnapshot`、`VideoExportController`

如果新增一个渲染设置，要把预览持久化和导出重建一起打通。

## 9. Parser 输出会同时喂给预览和导出侧的 Muri

当前流向：

- 实时预览路径：`requestTimelineSlowRefresh` -> `buildTimelineSlowRefreshResult` -> `scheduleDeferredMuriRefresh` -> `buildTimelineMuriRefreshResult`
- 导出路径：`buildVideoExportTaskFromSnapshot` -> `MuriAnalyzer::analyze`

含义：

- marker 字段一旦变化，会同时影响实时诊断和导出 overlay 行为。

## 10. 常见“改这里，查那里”对照

- 改 `SimaiNativeParser`：
  - 查 `MainWindow.ValidationFlow.cpp`
  - 查 `MainWindow.PreviewTimelineFlow.cpp`
  - 查 `VideoExportSnapshot.cpp`
  - 查 `MuriAnalyzer.cpp`
- 改预览 SFX 映射：
  - 查 `VideoExportController.cpp::buildSfxTimeline`
  - 查 `DEVELOPMENT_PLAN.md` 第 12 节
- 改导出媒体规则：
  - 查 `PreviewMediaController.cpp`
  - 如果格式支持变化，还要检查打包或 ffmpeg 假设
- 改预览时序常量：
  - 查 `PreviewGameplayConfig.h`
  - 查 `PreviewCanvas.cpp`
  - 查 `VideoExportController.cpp` 里的诊断与时间轴假设
- 改 Muri 静态阈值：
  - 查 `MuriStaticChecker.h`
  - 查所有把该阈值暴露给 UI 或设置项的地方

## 在这些情况下更新本文件

- 某个行为开始或停止在两条代码路径中镜像实现。
- 新增了导出序列化字段。
- 某条重复的查找规则被集中化，或反过来被拆散。
- 某条时序规则开始影响以前不依赖它的另一个子系统。
## 11. 2026-03 导出补充同步说明

- 当导出请求不是“完整导出”时，导出会增加 `1.5s` 预加载，但导出的物件集合会先按 `marker.second` 是否位于 `[L, R]` 内来筛选。
- 预览导出渲染、Muri overlay 与导出 SFX 都要消费同一批已筛选 marker；不要再额外做 `L-1 ~ L` 的运行时激活判定。
- slide 头部与轨迹视作同一个 `TimelineNoteMarker`，因此会一起保留或一起剔除，时间参考头部时间戳。
- `snapshot.outputPath` 在 worker 启动前就应当已经是最终输出路径；主窗口会先补齐 `.mp4` 后缀并处理重名回退（如 `name(1).mp4`）。
