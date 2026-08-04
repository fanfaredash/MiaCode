# 切换谱面资源释放专题核查

- 日期：2026-08-05
- 基准：`codex/windows-idle-freeze-diagnostics` `09422116`（本文改动落在 `1.1.0-beta.7-test.3`）
  - 审查本身在 `dev` `8fd63a6f` 上完成；两个分支在本文涉及的 6 个源文件上逐字节相同，
    `src/common/ProcessDiagnostics.{h,cpp}` 在本分支只增不删（`leak_gauge` API 未变），
    故结论对两条分支同等成立。
- 触发假设（用户）：**切换谱面后旧谱面资源未正确释放，导致堆积**
- 方法：静态审查（未运行程序、未实测）。所有结论都给出代码锚点；所有"数量级"都标注为**待实测**，实测手段见 §7。
- 关联文档：`WINDOWS_IDLE_FREEZE_AUDIT_REVIEW_ZH.md`（其中的 beta7 leak gauge 是本文的验证工具）

---

## 0. 结论摘要

用户的假设**部分成立，但落点和直觉相反**。

绝大多数"看起来最可疑"的资源（BASS 音频流、视频解码器、QSG 预览纹理、异步 worker、定时器、撤销栈、波形缓存）在切谱路径上**都有明确且正确的释放**——这些代码经过多轮审计，防护相当完整。

真正未释放的是**时间轴（timeline）那一侧的纹理缓存**，它是全仓库唯一**完全没有上限、也完全没有切谱失效钩子**的资源池，而且它的宿主对象在整个进程生命周期内常驻。

| # | 结论 | 严重度 | 归属 |
|---|---|---|---|
| **F-1** | `TimelineQuickTextureCache` 三个缓存无上限、无切谱失效 → 跨谱面单调累积 | **高** | `src/timeline/quick/` |
| **F-2** | `PreviewQuickSceneRoot::invalidateTextureCache()` 是死代码（零调用方）→ 预览纹理缓存只能靠撞 96 MB 上限自清 | 中 | `src/preview/quick_scene/` |
| **F-3** | `ignoredHeaderIssueTypesByFile_` 按文件路径累积，永不清理 | 低 | `src/app/mainwindow/` |
| **F-4** | `clearTimelineAndPreview()` 清了 `muriAnalysisReport_` 但没清 `muriStaticReferences_` | 低（正确性，非泄漏） | `src/app/mainwindow/` |
| — | 音频 / 视频 / 异步 / 定时器 / 撤销栈 / 波形 / 校验缓存 | **未发现问题** | 见 §5 |

---

## 1. 切谱路径先厘清：到底有哪两条

审查前必须区分两条路径，它们的释放语义不同：

| 路径 | 入口 | 是否换文件 | 是否走 `clearTimelineAndPreview()` |
|---|---|---|---|
| **换难度**（同文件） | `DocumentSection::switchToDifficultyField()`<br>`MainWindow.DocumentUi.cpp:1204` | 否 | **是**（`:1294`） |
| **换文件** | `DocumentSection::loadDocument()`<br>`MainWindow.DocumentUi.cpp:1388` | 是 | **是**（经 `activateInitialField()` → `switchToDifficultyField()`） |

换文件比换难度多做的清理，集中在 `TimelineSection::setCurrentFilePath()`（`MainWindow.PreviewTimelineFlow.cpp:627`）的 `pathChanged` 分支：校验缓存、校验装饰、波形缓存、延迟沙盒退出。

`clearTimelineAndPreview()`（`MainWindow.DocumentUi.cpp:1431-1493`）本身是一份**相当完整**的拆解清单：timeline 模型、待处理刷新请求、解析结果、Muri 报告、预览统计、SFX timeline、state bridge、`previewCanvas_->reset()`、stage media route，共 60 余项。

> **关键观察：这份清单里没有任何一项触及 timeline 的纹理缓存。** 这正是 F-1 的成因。

---

## 2. F-1（高）：时间轴纹理缓存跨谱面单调累积

### 2.1 缓存本体

`src/timeline/quick/TimelineQuickTextureCache.h:96-104`：

```cpp
QQuickWindow* window_ = nullptr;
miacode::timeline::TimelineNoteAssetSet noteAssets_;
QString skinDirectory_;
QHash<QString, QSGTexture*> textures_;          // GPU 纹理
QHash<QString, QPixmap>     transformedPixmaps_; // CPU 侧同内容副本
QHash<QString, HoldPixmapPartsCacheEntry> holdPixmapParts_;
quint64 textureCreateCount_ = 0;
```

头文件里 `debugCacheStats` 的注释自己写明了性质（`:63`）：

> `beta7 leak gauge (probe 3.1) — cheap read of the three uncapped caches ...`

即**开发者已知这三个缓存无上限**，只是当时的探针目标是"编辑→播放→暂停"循环，得出的结论是"在单个谱面内会饱和，所以不是泄漏源"。**该结论对单谱面成立，对切谱不成立**——切谱恰恰是唯一会引入全新 key 的动作。

### 2.2 全部失效触发点（穷举）

| 触发 | 代码锚点 | 影响范围 |
|---|---|---|
| `QQuickWindow` 变更 | `TimelineQuickTextureCache.cpp:25 setWindow()` | 全清 |
| 皮肤目录变更 | `TimelineQuickTextureCache.cpp:68 setSkinDirectory()` | 全清 |
| DPR 变更 | `TimelineQuickItem.cpp:1548 invalidateDprDependent()` | 全清 |
| 主题变更 | `TimelineQuickItem.cpp:1552 invalidateThemeDependent()` | **仅清非 `note|` / `hold_` 前缀**；`transformedPixmaps_` / `holdPixmapParts_` 完全不动 |
| **切换谱面 / 难度 / 关闭文件** | — | **无** |

`requiresReset()`（`:63`）只比较 `window_` 与 `skinDirectory_`，两者在切谱时都不变。

### 2.3 宿主生命周期：确认常驻

`TimelineQuickItem` 持有 `std::unique_ptr<TimelineQuickTextureCache> textures_`（`TimelineQuickItem.h:208`）。
`TimelineTabSurface` 在 `BottomTabsQuickHost.qml:301` 是**直接实例化**的，不在 `Loader` 里，切页只改 `visible`（`:303`）：

```qml
TimelineTabSurface {
    visible: controller && controller.bottomTabsCurrentTabId === "timeline"
```

⇒ 缓存对象与进程同寿。切一次谱、切一百次谱，它只增不减。

### 2.4 key 空间：为什么会持续增长

**(a) 文本纹理**（`textTexture()`，`:137`）
key = `text|<文本>|<QFont::toString()>|<颜色>|dpr=N`
文本来源是**谱面文本的行号**（`TimelineSceneStateBuilder.cpp:926-927`：`QString::number(label.lineNumber)`），且字体随位数变化（`timelineHeaderLabelScale(font, labelText.size())`）。
→ 上界≈"历史打开过的所有谱面里最长的那一份的行数"。跨谱面部分重叠（行号复用），增长**次线性**。

**(b) 音符纹理 / 变换后 pixmap**（`noteTexture()` `:225`、`transformedNotePixmap()` `:193`）
key = `note|<类型>|<w>|<h>|<旋转0.1度>|<镜像>|dpr=N`

旋转来自 slide 轨道箭头，是**连续量**（`TimelineSceneStateBuilder.cpp:1240`）：

```cpp
? qRadiansToDegrees(qAtan2(dy, dx))
```

`dx`/`dy` 是**屏幕像素**位移 —— 取决于 slide 时长、缩放、lane 高度。key 里量化到 0.1°（`TimelineNoteAssets.cpp:324-332`，`tenths %= 3600`），即**单个 (类型,尺寸,镜像) 元组最多 3600 个旋转档位**。
→ 这是主要增长轴：**每个谱面的 slide 几何各不相同 → 每次切谱都注入一批全新 key，且旧 key 永不退场**。
→ 且 `transformedPixmaps_` 与 `textures_` 各存一份同内容数据（CPU + GPU 双份）。

**(c) Hold 三段**（`holdTextureParts()` `:248`）
key = `<类型>|<scale 千分位>|dpr=N`，scale 由缩放档位驱动，**实际有界**（几十条），风险低。

### 2.5 影响面

- **GPU 显存**：`textures_` 里的 `QSGTexture` 永不 `delete`。
- **进程私有内存**：`transformedPixmaps_` 里的 `QPixmap` 同步永驻。
- **每帧成本**：`QHash` 命中本身是 O(1)，但缓存膨胀会恶化局部性；`debugCacheStats` 里的 `tex_rot` 统计要遍历全表（仅 `--debug` 下每次暂停一次，可忽略）。

> **未实测。** 单条目量级估算：sprite 目标盒 ≈ `basePixelSize × scale`（`TimelineNoteAssets.cpp:381-401`），叠加 DPR 后每条目 GPU+CPU 合计约 8–16 KB。真实数量必须用 §7 的探针读，不要用估算值做决策。

### 2.6 修复方向（建议，非本次改动）

优先级从高到低：

1. **给三个缓存加容量/字节上限 + 代际冲刷**，直接复用仓库内已验证的同款策略
   `src/preview/quick_scene/PreviewTextureGenerationPolicy.h::previewTextureGenerationResetRequired()`
   （预览侧用的是 96 条目 / 96 MB / 8192 fast-key）。
   这是**治本**：既解决切谱累积，也解决单谱面内缩放+slide 编辑导致的 key 爆炸。
2. 附加：在切谱路径上主动失效一次。但注意**不要全清**——音符底图与谱面无关，全清会造成切谱后一次可见的重建卡顿；应只清 `text|` 与带旋转（`rotTenths != 0`）的 key，`removeTextureKeysMatching()`（`:176`）已经提供了这个能力。
3. 附加：`transformedPixmaps_` 是 `textures_` 的 CPU 侧副本，二者生命周期应绑定；目前 `invalidateThemeDependent()` 只清前者不清后者，本身就是不对称的。

---

## 3. F-2（中）：预览侧纹理缓存的显式失效入口是死代码

`PreviewQuickSceneRoot::invalidateTextureCache()`（`PreviewQuickSceneRoot.cpp:629-646`）会置位 `textureResetRequested_`，`updatePaintNode` 在 `:901` 消费它并触发 `resetTextureGeneration()`。

但全仓库搜索：

```
src/preview/quick_scene/PreviewQuickSceneRoot.cpp:629   （定义）
src/preview/quick_scene/PreviewQuickSceneRoot.h:64      （声明）
```

**零调用方。** 它不是 slot、不是 `Q_INVOKABLE`，QML 也无法触达。

后果：`PreviewTextureRepository` 只能靠 `resetRequiredBeforeFrame()` 撞上限自清（`PreviewTextureRepository.cpp:13-18`，96 条目 / 96 MB / 8192）。
⇒ 切谱、换皮肤之后，**旧谱面/旧皮肤的纹理最多可常驻 96 MB 直到被挤掉**。

**与 F-1 的区别：这是"有界的常驻地板"，不是无界泄漏。** 严重度因此低一档，但它确实符合用户描述的"切完谱内存下不去"的观感。

---

## 4. F-3 / F-4（低）

**F-3** `MainWindowMemberStorage.inc:614`
`QHash<QString, QSet<QString>> ignoredHeaderIssueTypesByFile_` 按文件 scope key 累积（写入点 `MainWindow.ValidationListUi.cpp:232`）。只在用户取消最后一个忽略项时按 key 移除（`:238`），**换文件/关文件都不清**。
内容仅为字符串集合，量级极小；但它是一次会话内唯一按"打开过的文件数"线性增长的容器。按会话作用域保留可能是有意设计（回到旧文件时忽略项还在），若确认是有意的，值得补一行注释说明，否则未来审计会反复把它当泄漏。

**F-4** `MainWindow.DocumentUi.cpp:1449-1451`
`clearTimelineAndPreview()` 重置了 `muriAnalysisReport_`，但**没有**重置 `muriStaticReferences_`。后者要等异步分析 worker 回填才被整体替换（`MainWindow.TimelineAnalysisFlow.cpp:199`）。
这不是泄漏（永远是整体赋值，不累积），但在切难度到新分析落地之间存在一个窗口，旧难度的 static reference 仍会参与渲染（`MainWindow.ValidationRender.cpp:80`）。**建议单独确认是否有意**，不要和本文的泄漏项混在一起改。

---

## 5. 已排查且**未发现问题**的部分（含证据，避免下次重复审）

| 领域 | 结论 | 关键证据 |
|---|---|---|
| **BASS 音频样本** | 正确释放 | `Sample::create()` 首行即 `free()`（`BassPreviewAudioBackendSample.h:181`）；`resetAssets()` 逐个 `free()` 并置空（`_Assets.cpp:40-79`）；`samplesByKind_` 是裸指针视图，随之 `clear()`。tempo 流用 `BASS_FX_FREESOURCE`（`Impl.h:41`），解码流由 tempo 流代管 |
| **BASS 引擎/混音器** | 正确释放 | `BassPreviewAudioBackend.cpp:49,61` `BASS_StreamFree(masterMixer_)` + `BASS_Free()` |
| **miniaudio 后端** | 正确释放 | `resetBanks()` / `resetBackgroundTrack()` / `resetStretchedBackgroundTrack()` 全部 `ma_sound_uninit` + `ma_decoder_uninit` + `delete`（`QtPreviewSfxRuntime.Assets.cpp:18-80`，经 `#define` 重命名并入 `MiniaudioPreviewAudioBackend.cpp:346-350` 的 include-split TU） |
| **SFX timeline** | 正确释放 | `clearTimeline()` → `clearPreparedTimeline()` + `stopAll()`（`BassPreviewAudioBackend_Assets.cpp:308,380`） |
| **视频/背景解码器** | 正确释放，且防护充分 | `clearMedia()`（`PreviewStageMediaHost_Media.cpp:223-305`）显式向内外两个 `videoSink_` 推空帧以释放 `QAVFormatContext`→`avformat_close_input`；`setChartPath()` 用 (路径, `&video=`, 内容戳) 三元组判重，切谱必经 `clearMedia()`（`:188`） |
| **播放器对象** | 无重复创建 | `initializeBackendObjects()` 首行 `if (player_ != nullptr) return;`（`_Backend.cpp:181`），且 `new QAVPlayer(this)` 有父对象 |
| **预览 QSG 纹理** | 有界（但见 F-2） | 96 条目 / 96 MB 硬上限 + 代际冲刷（`PreviewTextureRepository.cpp:13-18, 159-162`） |
| **场景/皮肤资源** | 正确替换 | `PreviewSceneAssetRepository` 用 `loadGeneration_` 守卫异步结果，`applyLoadResult` 整体替换（`.cpp:66,103,126`） |
| **预览运行时状态** | 正确重置 | `PreviewRuntime::reset()`（`.cpp:977-1018`）清 noteMarkers / progressStats / muri / media |
| **波形缓存** | 有清理 | 换文件或换音轨即 `waveformCacheService_->clear()`（`MainWindow.PreviewTimelineFlow.cpp:647-649`）；音频设置对话框也清（`MainWindow.Dialogs.cpp:100`） |
| **校验缓存** | 有界 | `validationCacheByDifficulty_` 按难度 ID 键控（最多 7 条），换文件全清（`MainWindow.ValidationRuntime.cpp:617`） |
| **异步 worker** | 无堆积 | timeline slow / analysis 均单飞（`timelineAnalysisWorkerRunning_` 闸门）、`QPointer<MainWindow>` 守卫、revision + difficultyId + chartText 三重比对后才落地（`MainWindow.TimelineAnalysisFlow.cpp:124-168`） |
| **线程池** | 一次性创建 | `previewWarmupPool_` / `timelineSlowRefreshPool_` / `timelineAnalysisPool_` 均在 `MainWindow.FrameBootstrap.cpp:121-132` 以 `this` 为父创建 |
| **定时器** | 一次性创建 | 全部在 `FrameBootstrapFinalize.cpp:159-354` 建立；`exportIntroLeadInTimer_`（`PreviewIntroRegion.cpp:203`）有 `== nullptr` 守卫 |
| **信号连接** | 无重复连接 | 切谱路径上无 `connect()`；stage media host 的 5 处 `connect` 在 `ensurePreviewStageMediaHostInitialized()` 的早退守卫之后（`PreviewStageMediaRoute.cpp:360-366`） |
| **编辑器撤销栈** | 每次切谱重置 | `setEditorText()` 调用 `clearUndoRedoStacks()`（`MainWindow.DocumentEditorState.cpp:432`） |
| **导出页嵌入面板** | 离页即销毁 | `onPageLeft()` → `syncEmbeddedVideoPanel()` → `destroyEmbeddedVideoExportPanel()`（`ExportLauncherPage.cpp:307-313, 438-480`） |
| **扩展贡献物** | 有清理入口 | `extensions/clearRuntimeContributions` 清空全部 widget/action/shortcut 向量（`MainWindow.ExtensionHostRequests.cpp:1511-1521`）。**注意**：是否在切谱时被调用取决于扩展自身实现，核心侧无强制 |
| **日志缓冲** | 有界 | `DebugLog.cpp:554` `kMaxQueueSize = 4096` |
| **`muriStaticReferences_`** | 整体替换，不累积 | 见 F-4（那是正确性问题，不是泄漏） |

### 5.1 非默认路径（按项目约定视为不存在）

- `TimelineView`（widget 版时间轴）的 `transformedIconCache_` / `holdPixmapPartsCache_`（`TimelineView.h:232-233`）同样只在缩放/皮肤变更时清理。
  但 `timelineWidgetlessQuickRoute_ = quickShellBootstrapMode_`（`FrameBootstrap.cpp:93`），而 `QuickShellBootstrap.cpp:227` 恒为 `make_unique<MainWindow>(true)`
  ⇒ **默认路径下 `TimelineView` 根本不会被创建**（`FrameBootstrap.cpp:1596`），不计入本次结论。
- `src/render/` + `src/sources/`（DComp/D3D11）默认关闭，未纳入审查。

---

## 6. 为什么"感觉在堆积"但大部分模块都是干净的

三个叠加效应，容易被误读成"到处都在泄漏"：

1. **F-1 真实存在且只增不减** —— 这是唯一的无界项。
2. **F-2 造成 96 MB 量级的常驻地板** —— 切谱后内存"降不回去"，但它有上限。
3. **Qt RHI 的延迟释放** —— `QSGTexture` 被 delete 后，底层 D3D11/Metal 资源由 RHI 在后续帧回收，进程私有字节的回落**滞后于**我们的 delete。beta7 探针的注释已经点明这一点（`TimelineQuickItem.cpp:1630-1631`：`nodes/tex flat, private_mb climbs` 即 Qt 内部延迟释放）。

⇒ 判定标准必须是 **`tex` / `tex_pix` / `tex_rot` 计数**，而不是进程内存曲线。计数持平 = 我们没泄漏；计数单调爬升 = 我们在泄漏。

---

## 7. 现有日志对 F-1 / F-2 的覆盖度评估

结论先行：**字段侧两条都够用，触发侧两条都不覆盖切谱；F-2 另有一个会输出"假数据"的静默失真陷阱。**

### 7.1 覆盖度矩阵

| | F-1 时间轴缓存 | F-2 预览仓库常驻 |
|---|---|---|
| 有对应字段？ | ✅ 完全对应 | ✅ 完全对应 |
| 切谱时会打点？ | ❌ | ❌ |
| 有静默失真风险？ | ⚠️ 丢样（可检测） | ❌ **陈旧数据（不可见）** |
| 需要改代码才能测？ | 否（但流程别扭） | 否（但必须确认门控） |

### 7.2 现有探针清单

| 日志行 | 打点位置 | 门控 | 关键字段 |
|---|---|---|---|
| `preview/resource_gauge` | `MainWindow.PreviewPlaybackState.cpp:368` | `runtimeDebugOutputEnabled()` | `qobject_descendants` / `d_play_kb` / `inflight`<br>+ 尾部拼入 `PreviewRuntime::resourceGaugePayload()` |
| `timeline/leak_gauge` | `TimelineQuickItem.cpp:1659` | 同上（经 arm 传递） | **`tex` / `tex_pix` / `tex_hold` / `tex_rot` / `tex_create`** / `nodes` / `gpu_kb` |
| `preview/quick_scene` `action=texture_generation_reset` | `PreviewQuickSceneRoot.cpp:908` | Runtime 通道 | `cached=` / `cached_bytes=` / `transient=` / `retained=`（冲刷**前**快照） |

### 7.3 F-1：字段够，触发点错位

**字段侧完全够用。** `debugCacheStats()`（`TimelineQuickTextureCache.cpp:94-130`）一次性吐出三个缓存的当前条目数 + 旋转键计数 + 累计创建总数，正好是判定 F-1 所需的全集：

- `tex` / `tex_pix` / `tex_hold` = 三个缓存的**当前占用**
- `tex_rot` = 带旋转的 note 键数量，直接隔离 §2.4(b) 那条主增长轴
- `tex_create` = **累计**创建数（单调），与 `tex` 对比即可区分"创建后被清掉"和"创建后一直留着"

**触发侧不覆盖。** `armRenderSample()` 全仓库只有一个调用方——暂停处理器（`MainWindow.PreviewPlaybackState.cpp:383`）。
⇒ **纯切谱不会产生任何一行日志。** 必须每轮都 play→pause 才能取到样本。

消费侧还有两个会静默跳过的条件（`TimelineQuickItem::updatePaintNode`）：

| 条件 | 后果 |
|---|---|
| 时间轴 tab 不可见（`BottomTabsQuickHost.qml:303` 的 `visible` 为假） | QSG 不调用 `updatePaintNode` ⇒ arm 悬空 |
| `!canBecomeReady()` 早退（`:1506`）位于 gauge 块（`:1632`）**之前** | 同样跳过 |

且 arm 是**单槽全局变量**，`armRenderSample()` 无条件覆写（`ProcessDiagnostics.cpp:116-121`），未消费的样本静默丢失。

> **好消息：丢样是可检测的。** 两条日志行都打印 `txn`。若某个 `preview/resource_gauge` 的 `txn` 在 `timeline/leak_gauge` 里找不到配对，就是那一轮丢样，不会被误读成"计数没涨"。核对 txn 是本方案的必要步骤，不是可选步骤。

### 7.4 F-2：字段够，但存在静默失真

`preview/resource_gauge` 尾部拼接的 `PreviewRuntime::resourceGaugePayload()`（`PreviewRuntime.cpp:1052-1065`）已经包含 F-2 需要的全部量：

```
scene_revision=… cached_tex=… cached_tex_kb=… transient_tex=…
cached_tex_creates=… transient_tex_creates=… sprite_max=… present_total=…
```

`cached_tex` / `cached_tex_kb` 就是 `PreviewTextureRepository` 的**当前常驻**（对应 96 条目 / 96 MB 上限）。

> ⚠️ **陷阱：这些字段有独立的第二道门控，且失效时不报错。**
> `PreviewRuntime::notePresentedTextureStats()` 在 `previewProfileOutputEnabled()` 为假时**第一行就 return**（`PreviewRuntime.cpp:1069`）。此时 `latestCachedTextureCount_` / `latestCachedTextureBytes_` 保持上一次的值（或初始 0），而 `resourceGaugePayload()` 照常拼串输出。
> ⇒ **日志行看起来完全正常，数字却是陈旧的。** 这比缺数据更危险。
> 数据源侧同理：`enqueueTextureStatsForPresentation()` 的三处调用点（`PreviewQuickSceneRoot.cpp:967/1029/1341`）全部包在 `if (previewProfileOutputEnabled())` 里。
>
> `--debug` 默认同时开启 runtime 与 preview-profile 两个通道，所以正常情况没问题；但只要环境里设了 `MIACODE_DISABLE_PREVIEW_PROFILE_OUTPUT`，F-2 的读数就静默失真。**测前必须确认这个变量未设置。**

**补充手段（不受上述门控影响）**：`texture_generation_reset` 是边沿触发的，每次撞上限冲刷前打印一次占用快照。它给不了常驻曲线，但给得了**冲刷频率**——而冲刷频率本身就是 F-2 的直接证据：如果切谱后很快触发一次 `reason=cache_limit` 的 reset，就说明旧谱面纹理确实堆在里面把上限顶穿了。

### 7.5 共同缺口

1. **切谱是"零日志事件"** —— 两条 gauge 线都不在 `switchToDifficultyField()` / `loadDocument()` 上打点。
2. **日志轮转会吃掉基线** —— runtime 通道 4 MB × 3 段（`DebugLog.cpp:372` + `kMaxLogSegments`）。`8fd63a6f` 之后约 1.5 MB/h，短测无忧；长时间跑会把最早的基线冲走，务必在测完后**立刻**取日志。

### 7.6 最小验证步骤（无需改代码，但流程别扭）

以 `--debug` 启动，**确认 `MIACODE_DISABLE_PREVIEW_PROFILE_OUTPUT` 未设置**，全程**停留在时间轴 tab**（否则丢样），然后：

1. 打开谱面 A → 播放几秒 → **暂停** → 记 `tex` / `tex_pix` / `tex_rot` / `tex_create` / `cached_tex` / `cached_tex_kb`
2. 切到谱面 B（换文件，slide 密度越高越好）→ 播放 → **暂停** → 再记
3. **切回谱面 A** → 播放 → **暂停** → 再记
4. A→B→A 循环 5 轮
5. **核对 txn 配对**，剔除丢样轮次后再看斜率

判读：

| 观察 | 结论 |
|---|---|
| 回到 A 之后 `tex` / `tex_pix` **仍继续上升** | F-1 证实（回到同一谱面不该产生新 key） |
| `tex_rot` 涨幅占大头 | 主增长轴是 slide 旋转，修复须优先覆盖旋转键 |
| `tex` 持平但 `priv_present_mb` 上涨 | Qt RHI 延迟释放，不是本文的问题 |
| `cached_tex_kb` 长期贴近 96 MB + 频繁 `reason=cache_limit` reset | F-2 证实（旧纹理把上限顶穿） |
| `cached_tex` 恒为 0 或恒定不变 | **不是"没泄漏"，是 profile 门控关着** —— 先查 §7.4 的陷阱 |

```bash
rg "timeline/leak_gauge|preview/resource_gauge|texture_generation_reset" .miacode/logs/miacode_runtime_debug.log
```

### 7.7 已补的诊断打点（`1.1.0-beta.7-test.3` 起）

上文的缺口已经补上：`MainWindow::TimelineSection::emitChartSwitchResourceGauge()`
（`MainWindow.PreviewPlaybackState.cpp`），由 `DocumentSection::switchToDifficultyField()` 末尾调用
（`MainWindow.DocumentUi.cpp`）。

**一个调用点覆盖两条路径**：`loadDocument()` 抵达谱面的唯一途径是
`activateInitialField()` → `switchToDifficultyField()`，所以换难度与换文件都被覆盖。

产出的日志行：

```
preview/resource_gauge reason=chart_switch txn=… switch_seq=… difficulty=…
  qobject_descendants=… inflight=… inflight_peak=…
  <processResourceGaugePayload> <PreviewRuntime::resourceGaugePayload>
```

并 arm 一次渲染线程采样 ⇒ 下一次 present 补出配对的 `timeline/leak_gauge txn=…`。

三个设计点：

| 决策 | 原因 |
|---|---|
| txn 偏移到 `1ULL << 32` 以上的独立区间 | 与播放事务号（从 1 开始的小计数）不相交，`timeline/leak_gauge txn=` 因此能唯一定位是哪条 resource_gauge armed 的，不会在两个来源之间串号 |
| 用独立的 `chartSwitchGaugeTxnCounter_`，**不复用** `previewPlaybackTransactionCounter_` | 后者用于与音频 / stage-media 后端对账真实事务，诊断代码不该消耗它的号段 |
| 同样受 `runtimeDebugOutputEnabled()` 门控，首行早退 | 非 `--debug` 下零开销（尤其是 `findChildren<QObject*>()` 那次遍历） |

**语义（读日志前必须理解）**：被 arm 的计数在**下一次时间轴 present** 时读取，因此一行报的是"进入这个谱面时进程所处的状态"——它携带的是**此前所有切换**累积下来的量，而不是本次切换自己新建的纹理。
这恰好是检测累积所需要的：在 A→B→A→B… 循环里，"进入 A"这些行的 `tex` / `tex_pix` **应当持平**；单调爬升即 F-1 的泄漏特征；计数持平而私有字节上涨则是 Qt RHI 延迟释放，与本文无关。

⇒ §7.6 里"每轮 play→pause"和"全程停在时间轴 tab"两条约束现在都不再必需（`visible` 仍决定 present 何时发生，但 arm 不会因为切页而丢——它会留到时间轴下次可见时被消费）。

---

## 8. 建议的处置顺序

0. **先补切谱打点**（§7.7）：几行诊断代码，让切谱本身触发采样。不做也能测（§7.6），但流程依赖操作纪律且容易丢样。
1. **先量后改**：执行 §7.6，用 `tex_rot` 的斜率确认 F-1 的真实量级。静态审查只能证明"没有上限、没有失效钩子"，证明不了"实际涨多少"。
2. **F-1 修复**：加上限 + 代际冲刷（复用 `PreviewTextureGenerationPolicy.h` 的策略函数），而非只在切谱时清空。理由见 §2.6。
3. **F-2 修复**：要么给 `invalidateTextureCache()` 接上调用方（切谱 / 换皮肤），要么删掉它并在注释里写明"预览侧只靠容量上限自清"。**保留一个零调用方的公开方法本身就是审计噪声。**
4. **F-3 / F-4**：确认是否有意；若有意，补注释即可。

---

## 9. 本次审查的边界（必须声明）

- **未运行程序、未做任何内存/显存实测**。所有"累积"结论均来自代码路径分析：能证明*不存在释放路径*，不能证明*实际增长速率*。
- 未审查：`src/render/`、`src/sources/`（DComp，默认关闭）、导出子进程、批量导出。
- 未审查扩展（extension）自身实现是否在切谱时重复注册贡献物——核心侧提供了 `clearRuntimeContributions`，但不强制调用。
- QML 侧未发现动态对象创建（全仓库 `.qml` 无 `createObject` / `createComponent`），故未展开。
