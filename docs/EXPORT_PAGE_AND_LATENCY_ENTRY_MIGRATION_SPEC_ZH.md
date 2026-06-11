# 导出页迁移 + 延迟设置入口并入谱面信息页 —— UI 迁移交接文档

- **状态**：**第一期 + 第二期均已实施**（2026-06-11；导出页 widget =
  `src/tools/export_page/ExportLauncherPage.{h,cpp}`；第二期实施记录见 §6）。
  **2026-06-12 页面布局重设计已实施**（固定头尾 + 永不横向滚动 + transport 条移除 +
  D6 推翻，见 §6.1；CTest 24/24，手工 GUI 验收待执行）
- **分支**：`test`
- **拍板记录**：2026-06-11 与产品（用户）逐项确认，全部主决策与细节决策见 §4
- **读者**：接手实施的工程师（假定不了解本次讨论上下文，但熟悉仓库
  `.claude/skills/miacode-dev-guide/` 的查阅方式）

---

## 1. 目标（一句话）

把分散在 5 处的全部「导出」功能收口为左侧菜单的一个「导出」页（替换现有侧边栏
「延迟设置」项）；「延迟设置」改为从「谱面信息」页内的入口卡进入（页面本体保留）。

分两期交付：

- **第一期**：侧边栏改造 + 延迟入口迁移（L-A）+ 导出启动器页（E-A，子项仍走现有对话框）。
- **第二期**：导出页升级为混合形态（E-C，视频导出整体内嵌页内，封面/批量保持弹窗）。

---

## 2. 现状盘点（全部经代码核实，行号为 2026-06-11 `test` 分支）

### 2.1 左侧菜单与中央页面栈

- 侧边栏 = `outlineList_`（QListWidget），由
  `src/app/mainwindow/sections/document/MainWindow.DocumentUi.cpp:485`
  `rebuildFieldSidebar()` 全量重建。项与 `Qt::UserRole` key：

  | 顺序 | 显示 | key | 点击行为 |
  |---|---|---|---|
  | 1 | 谱面信息 | `metadata` | 切 `metadataPage_` |
  | 2 | **延迟设置**（本次移除） | `latency` | 切 `latencyDetectionPage_` |
  | 3..n | 各难度 | `difficulty_chart` (+UserRole+1=id) | 切编辑器页 |
  | n+1 | 添加难度 | `add` | 行内弹出菜单 |
  | n+2 | 工具箱 | `toolbox` | 弹出 `toolboxMenu_`（不切页） |

- 点击分发：`src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp:888`
  （`itemClicked` lambda；`latency` 分支在 :902，`add` :907，`toolbox` :978）。
  注意契约：**分发先写 `activeOutlineKey_` 再调 `switchTo*Field()`**——
  各 switch 函数里的 latency teardown 因此必须无条件执行（见 §2.3）。
- 中央页面栈 `editorStack_` 共 4 页（`MainWindow.FrameBootstrap.cpp:802-806`）：
  `welcomePage_` / `metadataPage_` / `latencyDetectionPage_` / `chartPage_`。
- 页切换函数（`MainWindow.DocumentUi.cpp`）：
  `switchToLatencyField()` :700、`switchToMetadataField()` :744、
  `switchToWelcomePage()` :785、`switchToDifficultyField()` :825。
  共同骨架：`maybeSaveCurrentFieldChanges` → `latencyDetectionPage_->onPageLeft()`
  （无条件）→ `cacheWorkspaceLayoutSizes` → `stopQtPreviewPlayback(true)` → 清
  pending 播放状态 → 置 `activeDifficultyId_` / `activeOutlineKey_` → 切栈页 →
  `setChartBottomTabsMode(...)` → `rebuildFieldSidebar` → 刷新布局。

### 2.2 bottom-tab 机制（按页定义，迁移的关键约束）

- `setChartBottomTabsMode(bool)`（`MainWindow.DocumentUi.cpp:680`）控制底部
  时间轴/检查/无理三个 tab（`BottomTabsTabId`，见
  `sections/window/MainWindow.BottomTabsHost.cpp`）的整体可见性。
- 现状模式表：**编辑器页 ON、延迟设置页 ON**（试听需要在时间轴上看合成测试谱的
  taps，`switchToLatencyField` :730 显式开启）、**谱面信息页 OFF、欢迎页 OFF**。

### 2.3 延迟设置页（`src/tools/latency/LatencyDetectionPage.*`）—— 必须原样保持的契约

页面 = BPM / 偏移 / 试听 三卡。以下契约都是修过线上事故的，**本次迁移只改“到达
方式”，页面本体与契约零改动**：

1. `onPageEntered()` / `onPageLeft()` 生命周期：`onPageLeft()` 在**每条离开路径**上
   无条件调用（幂等）。历史事故：曾 gate 在 `activeOutlineKey_=="latency"` 上，而
   分发先覆写了 key，导致 teardown 永不执行 → SFX 音量泄漏进正常预览。
2. SFX 电平隔离是 `LatencySandboxController::isOnPage()` 的纯函数（经
   `MainWindow::applyPreviewAudioSettingsToRuntime()` 单入口重派发）。
3. Ctrl+S 经 app 级 `eventFilter` 路由（页面 QShortcut 与全局 Save QAction 同键会
   ambiguous），作用域 `isVisible() && isAncestorOf(target)`。
4. 试听复用主预览 transport（不是独立播放器）；进页保留 playhead
   （`switchToLatencyField` :713 的 restorePreviewSecond 逻辑）。
5. 页头标题「延迟设置」在 `MainWindow.DocumentUi.cpp:41` gate 在
   `activeOutlineKey_=="latency"` 上——页面保留，此 gating 不变。
6. 偏移值与难度页 header 的 `&first` 同源（`document_.first`）。

顶部菜单直达项：`latencyDetectorAction_`（文案「BPM && 延迟检测」）在
`sections/frame/MainWindow.BootstrapAndMenus.cpp:356` 创建（triggered →
`switchToLatencyField()`），在 `MainWindow.FrameBootstrap.cpp:174-176` 被挪进
顶部「工具」菜单。

### 2.4 导出功能：5 处入口 × 4 个子项

入口（编号对应讨论时的草图）：

1. **工具栏「导出」按钮 + 下拉**（`sections/frame/MainWindow.FrameBootstrapFinalize.cpp:125-168`）：
   按钮只开菜单（菜单项=导出 / 导出封面 / 批量导出）；:170-175 还有 250ms 悬停
   自动开菜单的 `exportVideoHoverMenuTimer_`。
2. **顶部「工具」菜单**：`exportVideoAction_`（「导出谱面」，
   `BootstrapAndMenus.cpp:549` 创建、`FrameBootstrap.cpp:182-185` 挪入工具菜单）
   + 独立「批量导出」action（`FrameBootstrap.cpp:186-187`）。
3. **文件菜单「导出为ZIP」**：`packAsZipAction_`（`BootstrapAndMenus.cpp:124-129`）。
4. **工具箱菜单**：同一 `packAsZipAction_` 复用（`FrameBootstrap.cpp:1069-1070`）。
5. **视频设置对话框的「字体」tab**（HUD 字体，与导出对话框 Font tab 共享
   `tools/video_export/HudFontSettings.*`）——属设置入口，本次**不动**。

子项明细：

| 子项 | 入口槽 | 形态 | 复杂度来源 |
|---|---|---|---|
| 导出视频 | `MainWindow::onExportPreviewVideo`（`ExportFlow.cpp:460` 一带） | `VideoExportDialog`，6 tab：输出/视频/游戏/片头/字体/导出区间（`VideoExportDialog.cpp:1268-1291`） | 片头 tab 内嵌 `IntroPreviewWidget`（QQuickWidget 实时预览）；设置持久化；导出走 worker 子进程 |
| 导出封面 | `ExportSection::onExportCover`（`ExportFlow.cpp:662`，种子任务 :679） | `ExportCoverDialog` 所见即所得合成器 | 内嵌原生 `QQuickWindow`（`createWindowContainer`）；渲染机制两条硬约束（no QQuickView / no forced OpenGL，见 feature-index §8c）；Esc/焦点特殊处理；`.miacover` 布局存取 |
| 批量导出 | `MainWindow::onBatchExportPreviewVideo` | `BatchVideoExportDialog` | 独立队列+进度 |
| 导出为ZIP | `ExportSection::onPackAsZip`（`sections/export/MainWindow.PackZip.cpp`） | 一键动作 + 进度/结果弹窗 | 最简单 |

种子任务：`ExportSection::buildVideoExportSeedTask()`
（`MainWindow.ExportFlow.cpp:344`），视频与封面导出共用；**当前隐式取活动难度**。

置灰联动：`exportVideoAction_->setEnabled(hasActiveDifficulty())`，位于
`DocumentSection::updateDifficultyScopedActionStates()`
（`MainWindow.DocumentUi.cpp:81-96`）。注意推论：在谱面信息/延迟这类
`activeDifficultyId_=0` 的栈页上，该 action 本来就是灰的——导出页同样
`activeDifficultyId_=0`，所以**导出页自身的可用性判断不能依赖
`hasActiveDifficulty()`**（见 §5.3 难度上下文）。

---

## 3. 方案选型记录（为什么是 L-A 与 E-A→E-C）

- **延迟迁移 L-A（入口跳转，页面保留）**：被选中。否决项——
  L-B（三卡整体内嵌谱面信息页折叠区块）：试听需要 bottom-tab ON + 预览沙盒，而
  谱面信息页为 OFF，需重做 tab 切换语义与进出生命周期（§2.3 的事故区），且谱面
  信息页已有占满高度的「其他 &xx 字段」编辑框，需引入滚动容器；工期≈3 倍。
  L-C（改回弹窗）：延迟页本来就是替换 `LatencyDetectorDialog` 的产物，弹窗挡住
  自己要联动的预览/时间轴，走回头路。
- **导出页 E-A→E-C 分期**：被选中。否决项——E-B（四子项全内嵌，含封面合成器）：
  封面合成器的原生窗口容器目前活在独立顶层对话框里，避开了主窗口工作区
  splitter/QuickShell rehost 的层叠体系；嵌入中央栈页后 airspace/焦点/Esc 路由
  需按 `.claude/skills/qt-ui-layout-pitfalls/` 雷区逐个排，回归风险最大。保留为
  远期演进，不进本计划。

---

## 4. 已拍板决策清单（实施时不要再开放讨论）

| # | 决策点 | 结论 |
|---|---|---|
| D1 | 延迟检测并入方式 | **L-A**：谱面信息页入口卡跳转，延迟页本体保留 + 返回条 |
| D2 | 导出页形态 | **分期**：第一期 E-A 启动器 hub，第二期升级 E-C 混合内嵌 |
| D3 | 工具栏「导出」按钮 | **改为跳转导出页**（拆掉下拉菜单与悬停定时器） |
| D4 | 导出页难度上下文 | **页顶难度选择器**，默认 = 进入前活动难度，回退 `projectLastOpenedDifficultyId_` |
| D5 | 侧边栏「导出」项位置 | **工具箱上方**（顺序：谱面信息 → 难度们 → 添加难度 → 导出 → 工具箱） |
| D6 | 工具菜单「导出谱面 / 批量导出」 | ~~保留直达对话框~~ → **2026-06-12 产品推翻**：「导出谱面」改为跳转导出页（模态形态代码保留但无 UI 入口，`MainWindow::onExportPreviewVideo` 包装已删）；「批量导出」维持直达弹窗 |
| D7 | 文件菜单 + 工具箱「导出为ZIP」 | **保留**（同一 action，零维护成本；导出页内另加一处入口） |
| D8 | 谱面信息页延迟入口卡 | **带 BPM/Offset 实时摘要** |

默认假设（实施按此执行，产品可随时推翻）：

- A1：工具菜单的「BPM && 延迟检测」直达项**保留**（键盘可达路径）。
- A2：导出页停留期间右侧预览面板**维持切页前画面**（不随难度选择器刷新）。
- A3：导出进度第一期**维持现有弹窗**，第二期才移入页内进度条。

---

## 5. 第一期实施方案（4 个工作包，顺序执行，每包独立可验收）

### WP1 —— 侧边栏与页面栈改造

1. `rebuildFieldSidebar()`（`MainWindow.DocumentUi.cpp:485`）：
   - 删除 latency 项（:502-513）。
   - 在工具箱项**之前**插入「导出 / Export」项，key `"export"`，新图标（建议参考
     `makeToolboxAccessIcon` 的自绘风格做一个下载/导出箭头），tooltip 列出
     四个子项（导出视频 / 导出封面 / 批量导出 / 打包ZIP）。
   - 选中态：`activeOutlineKey_=="latency"` 分支（:567）改为高亮 **metadata** 项
     （延迟页在信息层级上成为谱面信息的子页）；新增 `=="export"` 分支高亮导出项。
2. 点击分发（`MainWindow.FrameBootstrap.cpp:888`）：删 `"latency"` 分支（:902），
   新增 `"export"` 分支 → `activeOutlineKey_="export"; switchToExportField();`。
3. 新增 `DocumentSection::switchToExportField()`：**整体照抄
   `switchToMetadataField()`（:744）的骨架**——含无条件
   `latencyDetectionPage_->onPageLeft()`、`stopQtPreviewPlayback(true)`、
   `activeDifficultyId_=0`、`setChartBottomTabsMode(false)`、
   `refreshLayoutAfterPageSwitch` 双保险。仅栈页目标与 key 不同。
4. `exportPage_` 加入 `editorStack_`（`MainWindow.FrameBootstrap.cpp:802-806` 处
   `addWidget`）。新页构建放独立文件（建议
   `sections/export/MainWindow.ExportPage.cpp` 或 `src/tools/export_page/` 新
   widget，遵守 god-file 规约：MainWindow 只做编排）。
5. 双外壳验证：widget shell 与 `--quick-shell-beta` 两套外壳下，新栈页都按谱面
   信息页同款路径工作（谱面信息页模式已验证可行，无需新机制）。
6. 顺手清理（同一 PR）：工具箱项构建处重复的 `setText`/`setToolTip` 死代码
   （`MainWindow.DocumentUi.cpp:559-564`），以及 tooltip 残留的「BPM检测与偏移」
   文案。

### WP2 —— 延迟入口迁移（L-A）

1. 谱面信息页（构建在 `MainWindow.FrameBootstrap.cpp:580-762`）在
   `metadataCard` 之后追加「延迟与偏移校准」卡：
   - 摘要行：当前 BPM（`document_` 的 wholebpm 解析值，与延迟页同源）+ 偏移
     `document_.first`；在 `populateMetadataPage()`
     （`MainWindow.DocumentUi.cpp:583`）里随文档刷新。
   - 按钮「打开延迟设置 →」→ `switchToLatencyField()`（无需动分发器；该函数
     自带全部进页语义，含 bottom-tab ON 与 playhead 保留）。
2. 延迟页顶部加「← 返回谱面信息」条 → `switchToMetadataField()`。
   实现位置建议在 `LatencyDetectionPage::buildUi()` 顶部加一行返回控件（页面
   已持有 `owner_` MainWindow 指针）；样式随 `applyThemeStyles()` 重刷。
3. **不改**：`onPageEntered/onPageLeft`、SFX 隔离、Ctrl+S filter、
   `MainWindow.DocumentUi.cpp:41` 的页头 gating、`latencyDetectorAction_`（A1）。
4. `UiText` 新增中英文案：入口卡标题/摘要/按钮、返回条。

### WP3 —— 导出启动器页（E-A）

页面结构（自上而下）：

```
┌ 导出（标题）────────────────────────────────┐
│ 难度上下文： [● Master] [Re:Master] …（徽章单选排）│
├──────────────┬──────────────┤
│ 导出视频        弹窗 │ 导出封面        弹窗 │
│ 完整谱面确认MV…     │ 难度卡合成器…       │
│ [打开导出设置…]      │ [打开合成器…]       │
├──────────────┼──────────────┤
│ 批量导出        弹窗 │ 打包 ZIP    页内执行 │
│ 多难度/多谱面队列    │ maidata+音轨+BG/PV  │
│ [打开队列…]         │ [立即打包]          │
└──────────────┴──────────────┘
（bottom-tab：隐藏，同谱面信息页）
```

实施要点：

1. **难度选择器**（D4）：徽章复用 `makeDifficultyBadgeIcon`；进页时默认选中
   「进入前的活动难度」，无则回退 `projectLastOpenedDifficultyId_`
   （`MainWindowMemberStorage.inc:311`，持久化键 `last_opened_difficulty`），再无
   则第一个存在的难度。文档重载/难度增删时重建徽章排。
2. **种子任务参数化**：`buildVideoExportSeedTask()`
   （`MainWindow.ExportFlow.cpp:344`）增加显式难度参数（默认值=现行为，保持
   `onExportPreviewVideo`/`onExportCover` 既有调用不变），导出页按选中徽章传参。
   涉及读取活动难度的内部取值点要一并跟随参数（谱师 fallback 契约见
   feature-index §3 「Export-side fallback contract」，五处同步点不要漏）。
3. **三张弹窗卡**分别调既有入口：`onExportPreviewVideo` / `onExportCover` /
   `onBatchExportPreviewVideo`（批量导出本身多难度，难度选择器对它仅作默认值
   提示，不强约束）。**对话框内部零改动。**
4. **ZIP 卡**：调 `onPackAsZip()`（`MainWindow.PackZip.cpp`），复用现有进度/
   结果弹窗（A3）。
5. **禁用态**：不能用 `hasActiveDifficulty()`（导出页上恒 false，见 §2.4 推论）。
   判据 = 文档含 ≥1 个有谱面体的难度（视频/封面/批量卡），ZIP 卡判据 = 文档
   可保存。置灰时卡上给原因文案。`updateDifficultyScopedActionStates` 不动
   （它管的是菜单 action，语义不变）。
6. 进度第一期不进页（A3）。

### WP4 —— 入口收敛 + 文档同步

1. 工具栏「导出」按钮（`MainWindow.FrameBootstrapFinalize.cpp:125-168`）：
   - 拆掉 `exportVideoMenu_` 下拉与 :170-175 的 250ms 悬停定时器
     `exportVideoHoverMenuTimer_`（含 `eventFilter`/`setMouseTracking` 相关挂钩，
     `ExportSection::showExportToolbarMenu()` 一并删除或改跳页）。
   - 点击 → 切到导出页（同侧边栏 `"export"` 分支）。
   - 按钮 enable 不再绑 `exportVideoAction_`（那个是难度作用域的）；改为
     「有文档打开即可用」或恒可用（页内自行置灰），实施时取简单者。
2. 菜单保持现状（D6/D7/A1）：工具菜单「导出谱面 / 批量导出 / BPM && 延迟检测」、
   文件菜单与工具箱的「导出为ZIP」全部不动。
3. **同步更新 `.claude/skills/miacode-dev-guide/references/feature-index.md`**
   （仓库硬规约：结构/入口变更必须同 PR 更新 skill）：
   - §2/§3 侧边栏键表与 outline 流程（`export` 键、latency 键删除）；
   - §8 视频导出入口槽（工具栏下拉 → 导出页）；§8b ZIP 入口槽补导出页；
     §8c 封面入口槽（toolbar dropdown → 导出页卡片）；
   - §9 延迟页入口描述（侧边栏项 → 谱面信息页入口卡 + 返回条 + 工具菜单直达）。

---

## 6. 第二期方案（E-C 混合内嵌）—— **已实施（2026-06-11）**

实施形态（与原概要的差异以**粗体**标注）：

- 导出页子导航：**视频导出（页内）** / 封面导出 ↗弹窗 / 批量导出 ↗弹窗 /
  打包 ZIP（页内）。难度徽章排保留在页顶，作用于所有子页。
  **子导航最终为页顶横向分段按钮，不是左侧纵列**（左列在 quick-shell 工作区
  ~700 逻辑像素宽度下放不开 6-tab 面板，导致右缘裁切+横向滚动，2026-06-11 返工）；
  内嵌模式同时解除面板的 560px 对话框最小宽。
- **6 个 tab 没有抽成独立面板类——`VideoExportDialog` 本体增加「内嵌面板模式」**
  （`setEmbeddedPanelMode(true)`：去模态/去自身高度锁与居中/Esc-reject 与 done()
  no-op/隐藏取消键；`startExport` 改发 `exportConfirmed()` 信号，面板保持打开）。
  工具菜单「导出谱面」直达路径继续以模态对话框形态使用同一个类（D6 不变），
  设置持久化与 bake-gate 锁步天然单点。内嵌面板生命周期 = 进入「视频导出」子页
  创建（`ExportSection::createEmbeddedVideoExportPanel`，含 exportPreviewActive_
  会话），离开子页/导出页销毁（= 对话框关闭语义，持久化时机风险消解）。
- 页内底部「开始导出」（即对话框原导出键，运行中变「取消导出」）。
  **进度不做页内进度条（产品 2026-06-11 改 A3）：复用右侧预览区的播放条** ——
  面板发起的导出不再弹 `QProgressDialog`，播放条按**谱面时间**前进
  （位置 = 导出起点 + 进度 × 区间时长，量程/时间文案保持播放形态，禁用拖动；
  `TimelineSection::updatePreviewSliderPosition` 被 gate），百分比/阶段/ETA
  文案只走状态栏；quick-shell 经 `shellVideoExportProgressSeconds()` →
  `QuickShellController.videoExportProgressSeconds` →
  `QuickShellPreviewTransport.qml`（displayedSeconds 覆写）。
  菜单直达路径维持原弹窗进度。
  ⚠ 初版曾把滑条改成 0..100 百分比制 —— 已被产品否决并返工为播放条语义，
  不要回退。
- 「导出区间 ↔ 底部时间轴拖选」联动属远期增强，未实施。

### 6.1 2026-06-12 布局重设计（固定头尾 + 永不横向滚动）

初版 E-C 页面整页一个 QScrollArea，总高超出视口时「开始导出」沉到折叠线以下且
矮 tab 被「垫到最高 tab」的等高策略撑出大片空白——产品验收不通过，重设计如下
（逐项与产品确认，决策记录 2026-06-12）：

- **固定头尾**：难度徽章排 + 下划线子导航（无卡片框、无说明文字）+ 页内 hairline
  固定在顶部；面板的「开始导出」按钮区固定在底部；页面本体永不滚动。
- **横向永不滚动（硬性）**：内容必须压缩进可用宽度；每个 tab 页套竖向 QScrollArea
  （横向 AlwaysOff）仅作极矮窗口的兜底。
- **tab 等高垫高策略废除**（内嵌模式）：tab 区拉伸填满剩余高度，各页自然高度。
- **transport 条整条移除**（内嵌模式）：右侧预览区播放条是唯一的播放/定位面；
  「导出区间」tab 的「设为起点/终点」与当前时间显示改读主预览权威时钟
  （`previewTimer_` 常驻 + 点击时再读一次）。原条上的 ▶/■ 本就只是普通播放/停止
  （"RangePreview" 是历史命名，并无区间试听语义），无功能损失。
- **片头实时预览（IntroPreviewWidget）在内嵌模式下删除**：它是最高的 tab 内容
  （320×220 固定），产品拍板牺牲以换取「默认一屏放下」。模态形态（已无入口）
  仍构造它。
- **封面/批量/ZIP 三个子页只留动作按钮**（弹窗类文案带 ↗），描述文字/模式标签全删。
- **「布局施工中」占位标签删除**。
- 样式：`UiTheme::exportLauncherPageStyleSheet`（徽章药丸 + 下划线子导航）+
  新增 `UiTheme::embeddedExportTabStyleSheet`（内嵌 tab 下划线风，模态不受影响）。

---

## 7. 明确排除项（不要做）

- **E-B 全内嵌**（封面合成器进主窗口工作区）：原生窗口容器 + splitter/QuickShell
  rehost 层叠风险，远期另立项。
- **L-B 折叠内嵌 / L-C 改回弹窗**：已否决，理由见 §3。
- 视频设置对话框「字体」tab（入口⑤）维持现状。
- `ExportCoverDialog` 内部任何改动（渲染机制两条硬约束是踩过崩溃学来的，
  feature-index §8c「⚠ Render mechanism」）。

---

## 8. 必须保持的契约（回归雷区清单）

1. **每条离开延迟页的路径都要无条件调 `onPageLeft()`**——新增的
   `switchToExportField()` 也不例外（照抄 metadata 骨架即可满足）。
2. **bottom-tab 模式表**迁移后为：编辑器 ON / 延迟 ON / 谱面信息 OFF /
   欢迎 OFF / **导出 OFF**。
3. SFX 电平隔离的 `isOnPage()` 语义不变（到达方式变化不影响）。
4. 延迟页 Ctrl+S app 级 filter 作用域不变；不要给新页面加同键 QShortcut
   （ambiguous 陷阱）。
5. 导出页 `activeDifficultyId_=0` ⇒ 不要在页内用 `hasActiveDifficulty()` 做判断。
6. 种子任务参数化时保持谱师 fallback 的 `trimmed().isEmpty()` 契约五处同步
   （feature-index §3）。
7. 双外壳（widget shell / QuickShell beta）都要过验收。
8. UI 布局问题先查 `.claude/skills/qt-ui-layout-pitfalls/` 路由表再动手。

---

## 9. 验收清单（第一期，全部手工 GUI，Release 构建）

- [ ] 侧边栏顺序：谱面信息 → 难度们 → 添加难度 → 导出 → 工具箱；无「延迟设置」项。
- [ ] 谱面信息页可见「延迟与偏移校准」卡，BPM/Offset 摘要随文档刷新（改 `&first`
      后回到谱面信息页数值正确）。
- [ ] 入口卡按钮进入延迟页：bottom-tab 出现、时间轴显示合成测试谱、playhead 保留；
      侧边栏高亮停在「谱面信息」。
- [ ] 延迟页「← 返回谱面信息」回到谱面信息页，bottom-tab 隐藏。
- [ ] 延迟页试听后切去任意页再回正常预览：SFX 音量无泄漏（对照 §2.3 事故）。
- [ ] 工具菜单「BPM && 延迟检测」仍直达延迟页。
- [ ] 导出页：难度徽章默认值正确（进入前难度 / last_opened 回退）；四卡均可用；
      切换徽章后导出视频/封面的目标难度正确（验证导出文件名/难度卡）。
- [ ] 无谱面体难度时三卡置灰带原因；ZIP 在仅 metadata 时行为与现状一致。
- [ ] 工具栏「导出」点击直接进导出页；悬停不再弹旧下拉。
- [ ] 工具菜单「导出谱面 / 批量导出」、文件菜单与工具箱「导出为ZIP」行为不变。
- [ ] `--quick-shell-beta` 下重复上述关键路径（切页/bottom-tab/导出页）。
- [ ] CTest 全绿（现状 24/24）；`feature-index.md` 已同步。

---

## 10. 交接备注

- 本文档对应的设计讨论含四张 UI 草图（现状入口分布 / L-A 两屏 / E-A hub /
  E-C 混合），结论已全部文字化在 §5/§6 的结构图与要点里，实施不依赖原图。
- 行号会漂移：以 §2 的函数名/成员名为锚，行号仅作初次定位。
- 实施完成后本文件 §「状态」改为「第一期已实施」，并把 §9 勾选结果记录在 PR。
