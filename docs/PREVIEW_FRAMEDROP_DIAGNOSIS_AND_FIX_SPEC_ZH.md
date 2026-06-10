# 预览掉帧（"改多了就掉帧，必须重启"）诊断与修复方案

> 状态：**已结案（beta8 = 修复版）**。根因 = 预览中央显示 HUD 每判定一个音符组泄漏一张
> 整视口 QSGTexture（§8，logs_34/35 实锤，beta8 已修复）。§1–§7 保留为取证过程记录；
> 注意 §1 对 center HUD 的"已排除"与 §7.5 第 2 步的 prepared-cache 分支判断**已被 §8 推翻**。

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
  **【center HUD 的排除已被 §8 推翻】**——当年只核了子节点删除平衡，漏看了纹理所有权。
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

## 7. beta5 —— 确认根因 + 落地修复（gauge 实锤后）

### 7.1 gauge 取证结论（logs_30 / logs_31）
- **是内存泄漏**：`private_mb` 随暂停次数单调爬升（logs_30 ~76MB/周期 → 5.5GB；logs_31 用"狂点 play/pause" 1900 次 → 8GB），`qobject_descendants` 平、`gdi/user` 仅轻微涨。
- **是播放/渲染路径，不是编辑**：logs_31 是**纯 play↔pause toggle、零编辑**，仍 ~4MB/toggle（8000÷1900）。
- **两路独立深挖**：app 层无累积容器（渲染线程按指针读 `frameState_`、无快照/队列；各 push 都 replace；prepared cache clear-before-rebuild；texture repo 有上限；transport 0 connect/0 map）。泄漏在 **Qt RHI/场景图资源层**（几何/材质/staging 缓冲），渲染线程**惰性释放**，在核显（显存=系统内存）+ 快速 toggle/换页拖慢渲染线程时释放跟不上 → `private_mb` 累积、不重启不回收。

### 7.2 根因（git 实锤）
commit `d8ce8d7`（0.5.0 的 "prepared note windows" QSG 重写）给
[`PreviewRuntime::setMuriAnalysisReport`](../src/preview/runtime/PreviewRuntime.cpp) 加了**无条件 `sceneContentRevision += 1`**。
每次 play 都经 `applyLatestTimelinePreviewStateToPausedPreview → applyAlignedMuriAnalysisReportToViews`
无条件重推（常常一模一样的）Muri 报告 → 撞 revision → `PreviewPreparedSceneCache::sync` 返回 true →
**11 层全部 reset cursor、整棵 QSG 几何子树推倒重建**（日志里每次 play 都有 `preview_prepared_scene_hs count=925`）。
编辑也漏，是因为编辑真的改内容、同样撞 revision、同样全量重建。

### 7.3 Part A —— 已落地修复（beta5）
给 Muri 报告加**单调内容 revision**，让"未变则跳过 revision++/重建"：
- [`MuriTypes.h`](../src/common/MuriTypes.h)：`MuriAnalysisReport` 加 `quint64 revision`。
- [`MainWindowMemberStorage.inc`](../src/app/mainwindow/MainWindowMemberStorage.inc)：加 `muriAnalysisReportRevisionCounter_`。
- [`PreviewTimelineFlow.cpp:1109`](../src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp) / [`DocumentUi.cpp:976`](../src/app/mainwindow/sections/document/MainWindow.DocumentUi.cpp)：每次**存入新报告（分析产出 / reset）**时 `++counter` 并戳进 `report.revision`。
- [`PreviewRuntime::setMuriAnalysisReport`](../src/preview/runtime/PreviewRuntime.cpp)：`if (report.revision == frameState_.muriAnalysisReport.revision) return;`。
- **为何正确**：revision 在**每次新分析**都 ++（无论是 marker 还是 render options/threshold 变化导致），所以同一份报告重推 = 同 revision = 真 no-op；任何真实变化 = 新 revision = 必推。比 `sourceSignature`（只含 markers，漏 render options）健壮。**前提**：`muriAnalysisReport_` 只被整体替换、不被原地 mutate（已核对，仅 2 处赋值）。

### 7.4 Part B Step 1 —— 详细 gauge（beta5）
`preview/resource_gauge` 暂停时新增分项：
- **进程（Win32）**：`kernel_handles`、`peak_working_set_mb`、`commit_mb`、`paged_pool_kb`、`nonpaged_pool_kb`、**`page_faults`**（换页实锤）。
- **预览渲染（[`PreviewRuntime::resourceGaugePayload`](../src/preview/runtime/PreviewRuntime.cpp)）**：`scene_revision`（重建计数——**验证 Part A：play 不应再让它涨**）、`cached_tex` / `cached_tex_kb`（当前 RHI 纹理占用）、`transient_tex`、`cached_tex_creates` / `transient_tex_creates`（累计 churn）、`sprite_max`、`present_total`。

### 7.5 beta5 验证
1. `Start_MiaCode_Debug.bat` 跑 logs_31 同样的"狂点 play/pause"：
   - **`scene_revision` 不再随 `txn` 涨**（Part A 生效，play 不再触发重建）。
   - **`private_mb` / `commit_mb` / `page_faults` 不再单调爬升**（泄漏止住）。
2. 再跑"编辑→播放→暂停"：看 `scene_revision` 只在**真编辑**时涨；`private_mb` 是否仍残留爬升。
   - 若仍爬 → Part B Step 2：看 `cached_tex_kb` 是否同步涨（纹理）；若纹理平而 `private` 涨 → 确认是 Qt RHI 几何/材质惰性释放，转向"让 prepared-cache sync 增量化、不每次全量重建"（§4.2 源头方向）。
   - **【已被 §8 推翻】**：beta7 取证证明残余泄漏既不是 RHI 惰性释放也不是 prepared-cache，
     而是中央显示 HUD 的纹理所有权缺失。本分支判断到此作废。

## 8. 结案（beta8）—— 根因实锤 + 修复

### 8.1 beta7 取证结论（logs_34 / logs_35，2026-06-10）

beta7 量规（`d_play_kb` / `presents_in_play` / `gpu_kb` / `geom_create` / `leak_gauge`）+
音频日志 `bass_sfx_drain` 交叉回归，得到唯一自洽解释：

- **泄漏与"判定事件数"严格成正比，每个 `bass_sfx_drain`（音符判定组）泄漏 ≈ 4.6 MB**：
  两场会话全部 65 个测量播放窗口 KB/事件 = 4690±30（s34）/ 4278~4609（s35），
  无论渲染率 140Hz 还是塌到 16-38Hz（KB/present 会膨胀 8 倍，KB/事件不变）。
- **无音符段落泄漏归零**：s34 txn170-172（共 33 秒、4748 presents、每帧 ~85 次几何重建照跑、
  144Hz GUI 定时器照跑）drain 数 1/0/15 → 泄漏 4.5/-0.1/55.7 MB。
  ⇒ 同时否决"每 present 泄漏"（header churn 假说）与"每 GUI tick 泄漏"假说。
- **算术闭环**：预览画布 `render_size=512x512`、`dpr=1.50` ⇒ `QImage` 768×768 ARGB32 = 2304 KB；
  每张泄漏纹理钉住 GPU 一份 + `QSGPlainTexture` 内部 QImage CPU 一份 = **4608 KB ≈ 实测 4690**（98%）。
- **卡顿/闪退因果链**：30-44 MB/播放秒（随谱面密度 6.7~10 drains/s 浮动）→ private commit
  16 / 32.5 GB → OS 裁剪工作集（21.4→8.5→13 GB 锯齿）→ ~1.2 万缺页/秒 → present 率阶梯塌陷 = 掉帧；
  s34 终局 32.5 GB 提交下 ntdll 堆 AV（`0xc0000005` tid=4104 读 `0xFFFFFFFFFFFFFFFF`）= 闪退。
- 同步排除（对抗验证确认）：音频/BASS、撤销栈（s35 零编辑全速复现）、TimelineQuickTextureCache
  （按谱面饱和）、日志/句柄/QObject、Qt RHI 延迟释放积压（28 分钟空闲提交不降、与塌帧期速率恒定矛盾）。

### 8.2 根因

[`PreviewQuickSceneRoot.cpp` `updateCenterDisplaySlot`](../src/preview/quick_scene/PreviewQuickSceneRoot.cpp)：
中央显示（连击/达成率/DX分，`CenterDisplayMode`，默认 Off、用户开启后持久化）每次数值变化
（= 每个判定组）画一张**整视口** `renderSize×dpr` 的 QImage → `createTextureFromImage` →
`node->setTexture(texture)`，但 `QSGSimpleTextureNode` **从未 `setOwnsTexture(true)`** ——
Qt 语义：不拥有则 `setTexture` 替换时**不释放旧纹理**。手动 `delete ...->texture()` 只存在于
"模式关闭/节点类型不符"两条冷路径，最热的"数值更新"路径每次替换都泄漏一张。
§1 当年把 center HUD 列为已排除，只核了"子节点删除平衡"，漏看了纹理所有权——以此为戒：
**QSGSimpleTextureNode + 临时 createTextureFromImage 必须显式声明所有权**。
（注：视频导出 `VideoExportQuickRenderBackend` 复用同一 SceneRoot，开中央显示导出同样泄漏，本修复一并覆盖。）

### 8.3 beta8 修复（已落地）

- `node->setOwnsTexture(true)`（节点创建处）：`setTexture()` 替换时自动 delete 旧纹理，
  节点析构时释放最后一张。
- 两条冷路径的手动 `delete static_cast<QSGSimpleTextureNode*>(old)->texture()` 移除，
  改为纯 `removeChildNode + delete old`（节点已拥有纹理，再手动删会**双重释放**；
  顺带消除 old 非 QSGSimpleTextureNode 时 static_cast 的潜在 UB）。
- CMakeLists 版本 beta7→beta8。**本版未做**"只画文字包围盒"的尺寸优化（见 §8.5）。

### 8.4 beta8 验证步骤

1. `--debug` 启动，**开启中央显示**（达成率/DX分任一非 Off 模式），播放密集段 ≥60 秒后暂停。
2. 过滤 `preview/resource_gauge`：**`d_play_kb` 应 < ~10MB/次播放**（beta7 同段为 10 万~26 万 KB）；
   长试谱 `private_mb`/`commit_mb`/`page_faults` 不再单调爬升，`gpu_kb` 不再每播放上百 MB 增长。
3. 对照组：关闭中央显示重复一次，两组 `d_play_kb` 应同样平（beta7 中关闭=平、开启=爆，是
   用户侧最快的根因 A/B 复核）。
4. 视觉回归：中央显示数字正常刷新、暂停/拖动/切模式/关闭模式无残影无崩溃。

### 8.5 后续卫生项（结案后另行排期，非本泄漏）

- 中央显示只画文字包围盒（现在每次数值变化画+上传 768×768 整视口，~25 倍浪费——修掉泄漏后
  只剩性能问题）；重建判据由裸 double 比较改为"显示字符串变化"。
- §4.1 撤销栈封顶（真实存在的无上限累积，量级 MB 级）。
- header 层每 present 全量重建（~87 节点+标签纹理，纯 churn 浪费）：静/动子树拆分、
  标签纹理 raster 移到缓存命中之后、加 atlas 标志。
- `timelineThemeSignatureHash` 对可见标签数敏感 → 偶发全缓存 evict/recreate 突发（+886 次纹理重建）。
- `kTimelineMaxUiUpdateFps=3600` 实际放飞 ~144Hz GUI tick 链（churn 馈源，考虑按消费帧门控
  `bumpOverlayDynamicRevision`）。
