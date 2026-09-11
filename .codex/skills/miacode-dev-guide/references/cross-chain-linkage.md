# 跨模块同步

按行为选择同步面，先读当前实现与对应 Spec，不按旧函数名机械改动。

| 改动 | 必须一起检查的消费者 |
| --- | --- |
| note 类型、修饰符、时间语义 | `src/core/chart/parser/` ↔ `src/timeline/` ↔ `src/core/scene/` ↔ `src/preview/` ↔ `src/audio/` ↔ `src/tools/video_export/` ↔ `src/tools/muri/`；变换与规范化还需保证语法 round-trip |
| BPM、拍号、`&first`、offset | `src/core/chart/document/` 的 SimaiTimingMetadata、parser、TimelineQuickModel、播放时钟、导出媒体时间线、延迟检测；`&first` 空值为零，非数值或非有限值无效，导出重建必须失败；明确 chart time 与媒体时间的换算 |
| 文档替换、编辑与导航 | ChartWorkspace → revision → AnalysisService / EditorSyncController → runtime 与 QML 投影；完整文档保存点、difficulty 和过期结果丢弃一起验证 |
| 播放/seek/设备切换 | `src/app/runtime/playback/` ↔ `src/app/runtime/preview/` ↔ `src/app/runtime/timeline/` ↔ `src/audio/`；保持单一音频时钟、代次校验和 worker barrier |
| 图层、皮肤、绘制顺序 | `src/core/scene/` ↔ `src/preview/quick_scene/` ↔ `src/timeline/quick/` ↔ `src/tools/video_export/`；导出使用显式帧时间 |
| SFX 事件与混音 | `src/common/PreviewSfxTimeline.h`、`src/common/PreviewSfxTiming.h` ↔ `src/audio/` ↔ `src/tools/video_export/VideoExportAudioRenderPlan.cpp` |
| 导出选项与素材 | QML session / preferences → runtime export snapshot → `src/tools/video_export/VideoExportSnapshot.cpp` 序列化 → worker task；预览、视频及封面检查适用的同一设置 |
| 文件、媒体与资源解析 | `src/common/AssetPaths.h`、`src/common/ChartAssetPaths.h`、ChartMediaService ↔ 预览/导出/导入/打包 |

谱面信息保存由 `DocumentModel::publishWorkspaceCommit` 通过单次事件定时器合并字段修改，
调用 `ChartWorkspaceFileService::save(ChartWorkspace::MetadataSection)`。全局字段、难度等级、七个谱师
槽位与难度删除进入自动保存范围；难度正文保留独立保存状态。元数据页支持其他 `&` 字段编辑、音频标签读取、
封面提取、背景媒体导入和 PV 移除。离开文档前提交输入框内容，写入失败保留修改并报告错误。

## 不可丢失的边界

- QML model 是投影/命令入口，不重新计算文档 dirty 或制造第二个 workspace。
- 异步分析、播放与导航结果必须匹配各自的文档 revision / session generation / sequence；换谱后拒绝旧结果。
- 设备切换的即时音频停止与 GUI 状态更新不是同一线程动作；不要用 GUI 回调替代音频屏障。
- 预览与导出共享纯时间/场景/SFX 语义；headless 帧不能依赖 QML Timer 推动动画。
- 窗口、engine、音频资源和导出任务按 owner 生命周期释放；合并 Spec 不得削弱编译、链接或进程隔离。

具体产品语义查 `docs/INDEX.md`；测试入口查 `docs/tests/SPEC_CATALOG.md`。
