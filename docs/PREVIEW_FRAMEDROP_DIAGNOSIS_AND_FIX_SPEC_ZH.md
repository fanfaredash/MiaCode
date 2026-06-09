# 预览掉帧（"改多了就掉帧，必须重启"）诊断与修复方案

> 状态：beta4 = **诊断版**。本文档同时是 beta4 已应用诊断改动的**补丁说明**，以及
> 真正泄漏修复的**方案（未应用，待取证确认后再做）**。

## 0. 摘要

- **现象**：0.5.0 起，反复「编辑→播放→暂停→编辑」很多次后，预览出现巨大掉帧，必须重启才恢复。
  **复现时不带 PV（背景视频）。**
- **矛盾点**：用户怀疑 QtAVPlayer（0.5.0 引入），但不带 PV 时 QtAVPlayer 路径基本不参与
  （`loadVideoMedia` 永不调用，`QAVPlayer` 至多构造一个空闲无源实例，幂等不累积）。QtAVPlayer 与
  「离屏 worker 删除 + main/bootstrap 重写 + PreviewStageMediaHost +840 行」是**同一提交 `d76f584`**
  一起进来的——它只是 0.5.0 预览大改里最显眼的乘客，不是无 PV 掉帧的直接原因。
- **本次 beta4 做了两件诊断事**（详见 §3）：
  1. 关掉 `--debug` 下**默认强开的预览调试 HUD**（去掉每帧开销，并消除对掉帧判断的干扰）；
  2. 暂停时**低频**采样进程资源计量（句柄/内存/QObject），用于定位泄漏**类别**。
- **真正的泄漏修复**（撤销栈封顶 / 时间轴纹理缓存封顶 等）以方案形式列在 §4，**未应用**，
  待 beta4 取回 `preview/resource_gauge` 数据、确认是哪一类资源在爬升后再精确实施。

## 1. 已排除（强证据，避免重复排查）

对 cross-chain §1 整条「编辑→解析→时间轴→预览→播放→暂停」链路做了多路审计，下列子系统在应用层
逻辑上均为有界（替换而非追加 / 单飞合并 / 有上限并淘汰 / 复用而非重建 / 先断开再连接 / 日志门控）：

- 异步慢刷新 / 分析 worker（单飞门控 + 合并）。
- 预览音符缓存（`PreviewTextureRepository` 封顶 96/96MB/8192）、`PreviewPreparedSceneCache`（单条替换）。
- 预览 QSG 节点（固定 18 槽，多余子节点 `removeChildNode`+`delete`）。
- 底部时间轴 QSG（重建前 `clearChildren`；model `clear()` 后重填；bridge 整体替换 + 先断开再连接）。
- 音频 / SFX（BASS）（`configureTimeline`/`rebuildPreparedTimeline` 零分配；sample 固定 `unique_ptr`；无 HSYNC）。
- 视频媒体宿主（QtAVPlayer，无 PV 时不参与；player 全程一个）。
- 编辑器装饰 / 校验下划线（每轮重建新列表 + signature 门控 + 整体 `setExtraSelections`）、括号高亮（只建一次）。
- 播放传输 / 逐帧 tick（复用的成员定时器；`TimelinePlayback.cpp` 内 0 个 `connect`、0 个 `new`；
  `applyQtPreviewPosition` 播放中刻意跳过重活）。
- 各效果层 firework / judge scale-pop·break-flash / center HUD（每帧删除多余子节点；firework
  `ensureJudgeFireworkRoot` 是专用槽，create/destroy 平衡，非泄漏）。
- 生产侧 `frameStateChanged` 连接（`setRuntime` 同对象早返回 + 先断开再连接）。

## 2. 已确认的唯一「按编辑次数单调增长、只重启清空」累积点

**编辑器撤销栈无上限。** 全 `src` 内**没有** `setUndoLimit` / `setMaximumBlockCount` /
`setUndoRedoEnabled`；`QTextDocument` 撤销栈默认无限，仅在**加载/保存**时
`clearUndoRedoStacks()`（[MainWindow.DocumentEditorState.cpp:399](../src/app/mainwindow/sections/document/MainWindow.DocumentEditorState.cpp)）。
「编辑-播放-暂停」循环从不保存 → 撤销栈只增不减。**契合「改多了」「必须重启」**；但普通打字单步很小，
单独多半不足以造成「巨大掉帧」，应视为**必修项 + 可能的协同因素**，而非唯一主因。

## 3. 本次 beta4 已应用的诊断改动（补丁说明）

### 3.1 关闭 `--debug` 默认强开预览调试 HUD（已应用）

- **文件**：[MainWindow.FrameBootstrapFinalize.cpp:525](../src/app/mainwindow/sections/frame/MainWindow.FrameBootstrapFinalize.cpp)
  删除 `if (runtimeDebugOutputEnabled_) { previewShowDebugInfo_ = true; }`。
- **原因**：诊断版通过 `--debug` 启动（`Start_MiaCode_Debug.bat`）→ 旧逻辑据此**强开**预览调试 HUD
  （`PreviewRuntime::setShowDebugInfo` → `frameState_.render.showDebugInfo`），每帧多一层调试覆盖层绘制
  + 统计采集。这既加重掉帧，又让「掉帧到底有多少来自 HUD」无法判断。
- **改后**：HUD 仅由渲染设置里「显示预览调试信息」开关控制（`previewShowDebugInfo_`，默认 `false`），
  与 `--debug` 日志解耦。诊断版仍出日志，但不再背 HUD 每帧开销。

### 3.2 暂停时低频资源计量（已应用）

- **文件**：
  - [DebugLog.h](../src/common/DebugLog.h) / [DebugLog.cpp](../src/common/DebugLog.cpp)：新增
    `miacode::debug_log::processResourceGaugePayload()`——Windows 下采样 GDI/USER 句柄数 +
    工作集/私有提交（MB）。
  - [MainWindow.TimelinePlayback.cpp](../src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp)
    `pauseQtPreviewPlaybackExact()`：暂停时发一行 `preview/resource_gauge`，附带
    `qobject_descendants`（MainWindow 下 QObject 后代总数）。
- **频率**：**每次用户暂停一次**（低频，非每帧）——遵循「过于高频先不加日志，避免日志风暴」。
- **门控**：仅 `--debug`（runtime 调试输出开启）时记录；release 下连 `findChildren()` 都不走。
- **字段**：`gdi_objects` / `user_objects` / `working_set_mb` / `private_mb` / `qobject_descendants`。

> 说明：本次**有意只加暂停这一处低频计量**。`setNoteMarkers`、场景重建、纹理分配/淘汰等更高频
> 的盲点暂不加日志，避免日志风暴掩盖性能问题；待 §4.5 需要时再按需补。

## 4. 待实施的泄漏修复方案（未应用 — 取证确认后再做）

依 §5 取回的 `preview/resource_gauge` 数据，按「哪个字段单调爬升」对症下药：

### 4.1 若 `working_set_mb` / `private_mb` 单调爬升（内存类）→ 撤销栈封顶
- 根因候选：§2 的无上限撤销栈（+ QML/V4 垃圾）。
- 方案（择一）：(a) 按编辑步数/空闲周期性 `document()->clearUndoRedoStacks()`（会清掉 undo 历史，需产品权衡）；
  (b) 引入「撤销步数软上限」，超限即清栈重锚（`QTextDocument` 不支持局部裁剪，故只能整清或改用 `QUndoStack`）。
- 锚点：[MainWindow.DocumentEditorState.cpp:399](../src/app/mainwindow/sections/document/MainWindow.DocumentEditorState.cpp)
  附近（已有 `clearUndoRedoStacks` + `editorUndoSaveAnchor_`）；与 `chartSelectionTransformUndoEntries_`
  的现有 prune 协调。

### 4.2 若指向 GPU/纹理（需 §4.5 扩展计量佐证）→ 给 `TimelineQuickTextureCache` 封顶
- 现状：`TimelineQuickTextureCache` 三个哈希**无上限**，编辑期不淘汰（键基数有界但不释放；换缩放/谱面变长会抬高）。
  `PreviewTextureRepository` 已封顶。
- 方案：给 `TimelineQuickTextureCache` 加 LRU/字节上限（仿 `PreviewTextureRepository` 的 96 条/96MB），
  或在 `invalidate*` 之外按大小淘汰。
- 锚点：[TimelineQuickTextureCache.{h,cpp}](../src/timeline/quick/TimelineQuickTextureCache.h)。

### 4.3 若 `gdi_objects` / `user_objects` 单调爬升（句柄类，Windows「必须重启」经典）
- 静态审计未在 timeline 路径发现每周期 `new QWidget/QFont/QPixmap` 未释放；需按计量定位来源
  （可在 gauge 上加来源细分，或用 Process Explorer 句柄类型分布交叉确认）。

### 4.4 若 `qobject_descendants` 单调爬升（QObject 累积）
- 找每周期 `new QObject(parent=长寿命对象)` 未释放的子树；静态审计未在 timeline 路径发现，需结合计量定位。

### 4.5 扩展计量（可选下一步，便于 4.2/4.3）
- 给 `preview/resource_gauge` 增补：`PreviewTextureRepository` 条目数 + `cachedTextureBytes_`、
  `TimelineQuickTextureCache` size、`PreviewQuickSceneRoot` 子节点数。
- 需暴露 getter：[PreviewRuntime](../src/preview/runtime/PreviewRuntime.h) /
  [PreviewTextureRepository](../src/preview/quick_scene/PreviewTextureRepository.cpp) 的纹理计数、
  `TimelineQuickTextureCache::size()`。仍保持**低频**（暂停时）发出。

## 5. 取证步骤（用 beta4）

1. 用 `--debug` 启动 beta4（如 `Start_MiaCode_Debug.bat`）。打开一个**不带 PV** 的谱面。
2. 做 20~30 次「编辑→播放→暂停」，复现掉帧。
3. 打开 runtime 日志（`.miacode/logs/` 或可执行旁 `logs/` 下 `miacode_runtime_debug.log`），
   过滤 `preview/resource_gauge`。
4. 看哪个字段随**暂停次数**单调爬升：
   - `working_set_mb` / `private_mb` ↑ → **内存类** → §4.1。
   - `gdi_objects` / `user_objects` ↑ → **句柄类** → §4.3。
   - `qobject_descendants` ↑ → **QObject 累积** → §4.4。
   - 都基本持平 → 多半是 **GPU/RHI 纹理**（gauge 暂未覆盖）→ 加 §4.5 扩展计量再测 → §4.2。
5. 把爬升的那一类（连同几行样本）发回，即可精确定位并实施对应修复。

## 6. 同时验证：3.1 是否消除/减轻掉帧

beta4 关掉了默认强开的预览 HUD。请额外对比：**同样的 20~30 次循环下，掉帧是否明显减轻**。
- 若明显减轻 → 掉帧很大一部分来自 HUD 每帧开销（已由 3.1 解决），剩余再看 gauge。
- 若几乎无变化 → HUD 不是主因，全力看 §5 的 gauge 爬升项。
