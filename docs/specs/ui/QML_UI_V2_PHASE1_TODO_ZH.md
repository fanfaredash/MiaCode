# QML UI v2 补完清单

> 文件名沿用历史（多处文档与仓库指南指向它），内容已重写为**当前唯一在推进的工作清单**。
> 目标架构与阶段定义见 [QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md](QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md)；
> 本文只登记「还差什么、完成标志是什么、什么待复核」。
>
> **基线：2026-09-01 工作树**。本文所有状态以当前工作树源码和目标测试实测为准。
> 上一版基线是 `117a76a1`（2026-08-29）；阶段 1、阶段 2 与三个「暂未更新支持」入口
> 都在这两个基线之间落地，第 2 章的实测表已按新基线整体重测。
>
> **目标口径补充（2026-08-31）**：本清单的“轻依赖”不是追求部署包中 DLL 数量的绝对最少，
> 而是让 UI 进程不再携带无主的 Widgets / v1 宿主依赖，并让 Multimedia、Network、OpenGL、
> SVG、FFmpeg、BASS 等依赖都能说明其所属功能、加载时机和验证方式。Qt Quick/QML 仍由 C++
> `QGuiApplication` + `QQmlApplicationEngine` 承载；QML 不能替代应用宿主。

## 0. 2026-08-30 反馈闭环与接线复核

- [x] **调整菜单动态项丢失**：`MainMenu.qml` 的 `Repeater` 曾创建非视觉的
      `AppMenuAction`，Qt Quick `Menu` 不会把它插入菜单，因而顶部只剩分隔线、`更多…`
      为空。现改为 `AppMenuItem`，快捷键文本由该视觉行直接承载；`Ctrl+J` 等全部谱面变换
      仍只来自 `ChartTransformCommands` + `ShortcutRegistry` 的同一张表。`qml_main_menu_spec`
      守住四个视觉 delegate、快捷键行与 `Ctrl+J` 的表项契约。
- [x] **移除「预览 → 切换实时预览」**：已移除菜单、工具栏、命令总线、持久化开关和隐藏预览
      分支。预览区常驻，避免销毁唯一 live `PreviewSurface` 后无入口恢复。
- [x] **字体/皮肤入口一致**：导出页的“皮肤”页签与“预览设置 → 皮肤”并存；二者都走 v2 的
      owner-live 预览状态和 `UiRequestService`，HUD 字体变更均调用 `PreviewRuntime::update()`
      立即刷新右侧预览，不恢复任何 v1 控件接线。
- [ ] **隐藏 v1/Widgets 接线复核（证据见下，旧有“仅封面导出”结论作废）**：
      1. `QmlUiBootstrap` 仍创建并隐藏 `MainWindow`；所有 QML 模型经它取得大量状态与命令。
      2. `QmlEditorPageHost` 的导出/延迟/离开页面路径仍调用
         `switchToExportField` / `switchToLatencyField` / `switchToDifficultyField` /
         `switchToMetadataField`。这些 `DocumentSection` 方法仍依赖 `editorStack_`、占位页、旧预览
         控件和侧栏 spinner；这是功能可用性会受隐藏 Widgets 状态影响的活跃接线。
      3. root 拖放已改为 `QmlChartDropBridge`（单一 QWindow event filter）+ `Main.qml` 非拦截
         QML 提示层；不再创建 `ChartDropOverlay(QWidget)`，也不再依赖 native overlay 生命周期。
      4. 本次已删除 `QmlEditorPageHost.cpp` 中遗留的 `QBoxLayout` / `QStackedWidget` /
         `QWindow` / `AdoptedWidgetCoordinates` 等**未使用** include；它们不是活跃调用，但此前使
         “`src/app/qml_ui` 无 Widgets”这一表述不可靠。

      已核对的非误报项：ZIP 打包使用 `UiRequestService`，偏好设置只转发
      `preferencesRequested` 给 QML，音视频工具也经 QML 请求边界；这些不是 v1 UI 回接。
      `QmlApplicationContext` 当前仍持有 `MainWindow& backend_`，所以“QML 页面无 Widgets”
      不能等同于“应用宿主已无 Widgets”；需要先抽出独立的 `ApplicationServices` / session
      所有者，再删除隐藏窗口。
      封面导出已于 2026-08-31 建立 v2 所有者并脱离此清单；后续拆除必须先为页面切换、拖放
      建立 v2 所有者，不能再把 v1 控件重新接回 QML。

- [x] **QML 文案单一通道（2026-08-31，2026-08-31 复核修正）**：QML 全部可见文案改经
      `UiText.qml` 单例进入既有 en/zh/ja 词典；76 个 v2 QML 文件里**没有**未包裹的中文字面量，
      也没有 `qsTr`，418 处 `UiText.text` 实参全部可解析。不引入独立翻译目录。
      复核后改掉两处与标题不符的实现：
      1. 同形词表原本写死 en/ja 译文，等于第二套真相，且 37 条里已有 17 条与词典不一致
         （音频设置 ja 音量調整→音声設定、预览设置 ja 表示設定→プレビュー設定、
         取消导出 ja 出力を取り消す→出力をキャンセル，以及一批 Title Case→句首大写）。
         现在改为 **源串 → 词典 key**（`qmlSourceKeys()`），译文只有词典一处；反查也改成
         构建一次的静态反向索引，重复中文取字典序最小 key，结果稳定且不再每次线性扫描全表。
      2. `ui_text_locale_spec` 原本只拒绝 `qsTr(`。`UiText.text` 找不到实参时回显实参，
         所以敲错的 key（封面页传的是 key，不是中文）会在三种语言下把 `cover.add_imag`
         打到界面上。现在 spec 断言每个 QML 实参满足 `UiText::hasQmlSourceTranslation()`，
         并且同形词表映射到的 key 必须真实存在。
      顺带：窗口标题栏的 `还原`（还原窗口）与 HUD 字体的 `还原`（重置）是 QML 内部同形，
      表里无法二选一，标题栏改为显式传 `window.restore`（新增词条）。
      `media_tools.batch_pv_clear_queue` / `net.upload_clear_queue` 的日文原本是未翻译的
      "Clear Queue"，一并补成 `キューを空にする`。
- [ ] **中文文案与 v1 逐条比对（未做）**：上一条只解决了 en/ja 取自哪里。中文会话下
      `textForQmlSource` 原样返回源串，所以 **v2 重构时的中文偏差一个都没有被改回去**，
      其中 169 条反查不到 v1 条目的还被固化进 `qmlOnly` 覆盖表（33 条与某个 v1 条目高度近似）。
      实例：v1 `dialog.preferences.title` = 首选项，v2 显示 偏好设置；
      v1 `media_tools.audio_video_processing` = 音频/视频处理，v2 显示 音视频处理；
      v1 `Tap流速` / `Layout整图大小`，v2 多了空格；
      v1「在 track.mp3 开头插入一段静音，**并自动备份原文件**。」在 v2 掉了备份那半句（语义变了）。
      做法：逐条比对 `qmlOnly` 的 169 条，能对上 v1 的把 QML 源串改回 v1 中文并从覆盖表删除，
      确实是新标签的再留在表里。
- [x] **封面导出 QML 原生化（2026-08-31，2026-08-31 复核修正）**：扩展命令
      （`export.cover.start`）、隐藏 MainWindow 的工具菜单项与侧栏都进入 `CoverExportPage.qml`；
      `QmlCoverExportSession` 持有布局/持久化/文件请求，复用纯数据模型、`SceneFrameRenderer`
      与同一份 `CoverComposer.qml`（拖动与画布点选沿用该组件自带的 handler，页面只补
      `selectionBinder`），并以同一场景离屏 Quick 渲染输出。原 CoverStudio Widgets 工作台及
      `CardFontSettings` 已删除。`qml_cover_export_contract_spec` 钉住页面路由、无 Widgets
      请求边界及删除面，避免回接旧窗口。
      复核修正：`coverchart` image provider 只注册在离屏渲染器自己的私有引擎上，应用引擎
      从未注册，而页面刻意让 `chartSceneBinder` 为空、谱面帧只走 `image://coverchart/`——
      结果是**谱面帧图层在页面里是空白的，导出的 PNG 里却正常**。已在 `QmlUiBootstrap`
      注册，spec 增加断言钉住。
      注意「菜单」指隐藏 MainWindow 的工具菜单与扩展命令：v2 可见菜单栏 `MainMenu.qml`
      本来就没有任何导出类条目（视频/封面/批量/ZIP 都没有），可见入口只有侧栏。
- [x] **封面页版式并入 v2（2026-08-31）**：页面此前虽然用了 `Theme` 和 `App*` 控件，版式却还是
      v1 的——每行手写 `Text + 控件`（标签列不对齐、滑杆没有读数也没有双击输入）、面板是圆角
      描边卡片、右栏是一条五段长滚动、图层行挂 `🔒` emoji、标题绑了 `Theme` 里并不存在的
      `headerFontSize`。现在：工作区照抄 `MainSplitView`（平面板 + `PanelHeader` + `SplitHandle`
      1px 分隔），表单行一律 `LabeledCombo` / `LabeledSlider`，右栏按 画板 / 难度卡 / 图层 / 预设
      分成 `panelTab`（与 导出中心、预览设置 同构）。
      顺带修掉一个真 bug：给 `AppSlider` 直接绑 `value` 会在第一次拖动后把绑定打断，四个滑杆
      从此不再跟随模型；`LabeledSlider` 的 `Binding … when: !pressed` 正是为此存在。
      emoji 依 `qt-ui-layout-pitfalls` 的既有结论去掉（Windows 按彩字渲染，拿不到主题色），
      图层行改为隐藏用 disabled 灰、锁定用一个词表达。
      `qmllint` 干净（同时发现并修掉 7 处 `height:` 写在 Layout 管理的子项上）。

## 1. 目标边界

有四件事在范围内：

1. **功能对齐补完** —— v2 QML 能原生完成 `dev` 上的既有工作流。
2. **删除 v1** —— 隐藏的 `MainWindow`、隐藏的 `PlainCodeEditor`、`QuickShellController` 兼容层与全部 Widget 页面/对话框。
3. **建立 Qt Quick/QML 应用宿主** —— 可见 UI 全部由 QML 承载，C++ 只保留
   `QGuiApplication` + `QQmlApplicationEngine`、服务装配和必要的 Qt Quick 类型注册。
4. **治理运行时依赖** —— 从链接和部署中移除无主的 Widgets/v1 依赖；其他模块按真实功能保留，
   并按编辑器、预览、媒体、导出等边界隔离和验证，而不是笼统地删除所有 Qt 模块。

与这四件事无关的重构一律不做（见架构文档第 11 节非目标）。

### 判定规则

- 一个 UI 条目只有同时满足「v2 QML 原生可用」**且**「不再需要任何 Widgets 类型」才能勾掉。仅“能通过隐藏 Widget 达成”记为**未完成**。
- “轻依赖”条目还必须说明模块的功能归属：`Core/Gui/Qml/Quick/QuickControls2` 是 QML 宿主基础集合；
  `Multimedia`、`OpenGL`、`Network`、`Svg`、`MultimediaQuickPrivate` 及 FFmpeg/BASS 等，
  只有在对应能力仍由该目标提供时才保留。Qt Quick 的传递库和 QML import plugin 不因出现在部署包中就视为遗留依赖。
- 自动 spec 通过 **不等于** 验收。原生桌面验收单独记录在第 6、7 章，构建与 CTest 不能替代。
- 失败是**值**不是对话框：应用层不得弹窗、不得阻塞（架构文档第 7 节）。新代码引入 `QMessageBox` / `QFileDialog` 视为反向进度。

## 2. 基线实测：还挂着多少 Widgets

| 项 | 实测（`4f30a2d2`） | 位置 |
|---|---|---|
| 链接声明 | `COMPONENTS … Widgets …` + `Qt6::Widgets` | `CMakeLists.txt:101`、`:844` |
| 应用对象 | `QApplication` | `src/app/main.cpp:436` |
| 隐藏 `MainWindow` | **72 文件 / 42,750 行**（上一基线 73 / 48,732，−5,982） | `src/app/mainwindow/` |
| ~~隐藏 `PlainCodeEditor`~~ | **已删除（2026-08-30）**。树上只剩两条历史注释；扩展相关校验已迁移到归档 API registry 与离线文档 | — |
| ~~`QuickShellController`~~ | **已退役（2026-08-30）**。`src/app/quick_shell/` 只剩 **246 行**预览合成表面管道（`QuickShellPreviewCompositeSurface.*` + 三个策略头），与外壳控制器无关 | — |
| QML 仍消费的 controller 属性/方法 | **0 个**（`shellController` 在 `*.qml` 里零命中） | — |
| Widget UI 代码量 | `cover_export` 现为 **2,640 行 / 11 文件**的纯布局、持久化、谱面帧与 Quick 离屏渲染；**0 个 Widget UI 文件**（其中 `CoverCompositionPersistenceGuard.{h,cpp}` 75 行已不在 app target 里，只剩 `cover_layout_model_spec` 还编译它——是删是接需要定夺，见 §3.C）（2026-08-31 删除原 CoverStudio 工作台）<br>*（此前已删除：export_page 738、BatchExportPanel 组 947、MainWindow 嵌入面板机制 479、`VideoExportDialog` 组 4,691、`NetBatch*Dialog` 1,479、`PvBatchCompressionDialog` 393、`PlainCodeEditor` 组 2,272、`QuickShellController` 组 993）* | `src/tools/cover_export` |

### v2 自身新代码里的 Widgets 泄漏（优先清）

| 位置 | 内容 |
|---|---|
| ~~`src/app/qml_ui/export/QmlExportSession.cpp`~~ | ~~`QFileDialog` ×5、`QMessageBox` ×5~~ —— **已清零（2026-08-29）**。`src/app/qml_ui` 全目录现已无 Widgets 对话框 |
| `src/app/qml_ui/QmlEditorPageHost.*` | 已不再收养 `QWidget`，但仍通过隐藏 `MainWindow::DocumentSection` 切页；该路径会操作 `editorStack_` / 占位页等 Widgets，不能记作“仅页面记账”（详见 §0） |
| `src/app/qml_ui/drop/QmlChartDropBridge.*` | 仅负责 root `QWindow` 拖放事件、路径筛选、请求代次和 busy/late-callback 保护；视觉提示由 `Main.qml` 承载 |

`MainView.qml` 的对话框已是 `QtQuick.Dialogs`（非 Widgets），这条不用改。

### 2.1 轻依赖目标与当前事实（2026-08-31）

目标依赖分三层：

1. **QML 宿主基础层**：`Qt6::Core`、`Qt6::Gui`、`Qt6::Qml`、`Qt6::Quick`、
   `Qt6::QuickControls2`，以及 `QtQuick.Controls` / `Layouts` / `Window` / `Dialogs`
   等实际导入的 QML 模块。这是完整 QML 编辑器的正常基础，不以删除 `Qt6::Gui` 为目标。
2. **产品能力层**：`Qt6::Multimedia` 支撑音频、SFX、视频和设备枚举；当前导出/渲染代码
   还实际使用 OpenGL；BASS、FFmpeg、QtAVPlayer、SoundTouch、miniz 是现有媒体和导出能力的
   外部依赖。它们应保留，但归属到 Preview/Media/Export，而不是算作 UI 遗留。
3. **可选能力层**：Net 引擎目前仍被加入 `MiaCode` 源码目标并带来 `Qt6::Network`，即使入口
   暂时移除；需要在“恢复 Net 页面”或“隔离 Net 插件/独立目标”之间作出选择。`Qt6::Svg`、
   `Qt6::OpenGL`、`Qt6::MultimediaQuickPrivate` 也要分别记录直接使用点和平台条件。

**依赖治理要求**：

- `Qt6::Widgets`、`QApplication`、`QWidget`、`QDialog`、`QStyle` 及隐藏 `MainWindow` 必须归零。
- `ShaderTools` 若只用于构建期工具，不计入运行时依赖；CMake 不应把它误写成产品 DLL 依赖。
- `Qt6::MultimediaQuickPrivate` 暂时无法移除时，必须封装在单一媒体适配层并锁定 Qt 版本；
  长期目标是改用公共 Multimedia/VideoOutput API。
- 依赖完成不能只靠 `grep` 或 `otool`：要分别记录冷启动、编辑/预览、背景视频、普通导出、
  封面导出时的实际加载模块，并在 macOS 与 Windows 各自验证。

## 3. 功能对齐缺口

按「v2 用户能不能原生做到」分三类。

### A. 无 QML 实现，入口弹「暂未更新支持」（0 项，已清空）

三处触发点全部补上了真实实现（2026-08-30）。`unavailableFeatureRequested` 在 `*.qml` 里
**已无任何发出方**——`MainToolBar` / `MainMenuCommands` 的信号声明与 `MainView` 的处理器
仍留在树上作为通用机制，尚未删除；若确认不再需要，随阶段 4 一并清掉。

| 功能 | 状态 |
|---|---|
| ~~关于 MiaCode~~ | **已完成（2026-08-30）**：`components/AboutDialog.qml`，事实来自 `QmlUiSettings::aboutInfo()`（版本宏 + `QSysInfo` + UiText）。v1 的图标彩蛋（连点三次）未搬。 |
| ~~音频设置~~ | **已完成（2026-08-30）**：`preview/AudioSettingsDialog.qml` + `QmlAudioSettingsModel`。10 条通道（音量 + 静音）与 Break 星星尾判音开关，写入即应用到运行中的预览并持久化；「设为/恢复本地默认」两个预设按钮保留。**试听已于同日补齐**：模型自带 `QtPreviewSfxRuntime`（首次试听时才创建，页面关闭时释放），220 ms 静默去抖，拖动中不响、松手才响，预览正在播放时不试听。顺带修好两处：主静音会带着其余通道一起变（v1 行为），以及行不再随每次改动整体重建（原先会在拖动中把滑块本身销毁）。 |
| ~~预览设置~~ | **已完成（2026-08-30）**：`preview/PreviewSettingsDialog.qml` + `QmlPreviewSettingsModel`，视频 / 玩法 / 皮肤三个页签。值住在 `MainWindow::previewRenderSettings()` / `setPreviewRenderSetting()`（每个键各自的 canvas setter / 舞台媒体重路由 / 轮廓重算 / muri 重应用，写入即持久化）；皮肤页另承载全局皮肤、判定效果、判定线，以及分区域 HUD 字体的选择、导入、重置和样张。HUD 字体变更后直接请求运行中的 `PreviewRuntime` 重绘。文案全部取自 `dialog.render_settings.*` 的 UiText 键，与 v1 同源。v1 的第三个页签 **性能** 只有「预览刷新率」一项，v2 已在 偏好设置 → 性能 里，未在此重复。判定效果显示由 v1 的下拉勾选改为四个并排复选框（四个状态同时可见）。 |

### B. 有入口，但落到 Widget 对话框/页面（8 项已清空）

| 功能 | v2 入口 | 落点 |
|---|---|---|
| ~~延迟校准~~ | `ToolsSidebarPage.qml` → `openLatencyPage()` | **已完成（2026-08-29）**：`LatencyPage.qml` + `QmlLatencyModel`，`LatencySandboxController` 原样复用；`WindowContainer` 与 `QmlEditorPageHost` 的整套 widget 宿主机制已删除 |
| ~~音视频处理~~ | → `openMediaProcessingTools()` | **已完成（2026-08-29）**：`MediaToolsDialog.qml` 启动页 + `PrependBlankDialog.qml` + `PvBatchCompressionDialog.qml`（QML），`QmlMediaToolsModel` 承接；`src/tools/media` 已无 Widgets |
| ~~整谱规范化~~ | → `openNormalizeWholeChart()` | **已完成（2026-08-29）**：`NormalizeOptionsDialog.qml` + `QmlDocumentModel::normalizeChartSelection`，结果作为编辑器事务应用（undo 覆盖）；Widget 对话框与 `DocumentSection::onNormalizeWholeChart` 已删除 |
| ~~Net 批量下载 / 上传~~ | —— | **功能已暂时移除（2026-08-29）**：两个对话框与全部入口删除；引擎（`NetClient`、workers、scanner、diagnostics，均无 Widgets）保留在树上，恢复时直接补 QML 页面 |
| ~~封面导出~~ | `openCoverExport()` / 侧栏 | **已完成（2026-08-31）**：`CoverExportPage.qml` + `QmlCoverExportSession`。左图层 / 中央同源 `CoverComposer` 预览（拖动与点选沿用组件自带 handler）/ 右检查器，布局导入与另存、预设、背景、卡片字体、图层图片/文字、谱面帧和输出目录由 QML 页面完成；`CoverCompositeRenderer` 用该同一 QML 场景离屏输出。旧 CoverStudio/CoverComposerView/面板/对话框整组删除。 |
| ~~打包 ZIP~~ | → `packAsZip()` | **已完成（2026-08-29）**：走 `UiRequestService` 选路径与提示、`JobProgressService` + `JobProgressOverlay.qml` 显示进度与取消 |
| ~~偏好设置~~ | `QmlCommandService::openPreferences()` | **已完成（2026-09-01）**：`PreferencesDialog.qml`（界面/背景/编辑器/性能/快捷键五页签，快捷键录制内置为页签而非二级弹窗），`QmlPreferencesModel` + `QmlAppBackgroundModel` 承接；扩展页签仍不恢复 |
| ~~批量导出~~ | `openBatchExport()` | **已完成（2026-08-29）**：QML `ExportVideoPage` 的 batch 页是唯一批量界面，`BatchExportPanel` 组已删除 |

> 0b（扩展宿主）已按 2026-09-01 决策删除产品运行时；v1 manifest/schema/SDK/docs/API registry 仍作为归档与离线校验面保留。
>
> **0c 已于 2026-08-29 由所有者重新定向：偏好设置 / 延迟检测 / 音视频处理三页「补成 QML」，不删除。**
> 这推翻了架构文档第 10 节记录的相反决定（"哪怕功能会缺失也要做"）——那节与第 8 节的 0c ⏸ 行
> 必须在本批工作中改齐，否则只读架构文档的人会做反。
>
> **排期决定（2026-08-29 所有者）：**
> - **Net：功能暂时移除。** 入口与两个 Widget 对话框已删除；`src/tools/net` 的引擎部分保留。
> - ~~**封面导出：放到最后做**~~ —— **已完成（2026-08-31）。**
>
> B 类已不再阻挡 `Qt6::Widgets` 的最终摘除；当前阶段仍保留的阻碍是隐藏 `MainWindow` 及
> `src/app/ui/` 中与其共用的辅助件。拖放 bridge 已不再引入新的 Widgets 依赖。

### C. 已知功能缺失（v1 有、v2 尚未补）

- [x] **封面页未接线的 v1 功能（2026-08-31 发现，同日随 UI 改版接线）**。`QmlCoverExportSession`
      早已实现并导出了这些 API，但页面上没有入口，功能等同于缺失。现在都有控件了：
      重置布局 / 保存布局到文件 / 导入布局文件 / 打开最近 / 清除最近 进标题栏的「布局 ▾」菜单，
      复制图层 与 上移 / 下移 进左栏图层操作区，谱面帧背景亮度 / 透明度 与 文字超长策略
      进右栏检查器。重置是破坏性的，改为经 `UiRequestService` 先问一次（v1 同款问法）。
      `qml_cover_export_contract_spec` 现在逐个断言这九个能力在页面里有调用点，避免再次
      「有 API 无入口」。
      仍未接线、留待定夺的只剩：画布缩放（`Ctrl+0/+/-`）、预设重命名（`renameUserPreset`）、
      四个 v1 内置预设名。`setActiveLayerCenter` 是纯死代码：拖动由 `CoverComposer.qml`
      直接改模型，不走 session，可以删。
- [ ] **封面页两处交互缺口（2026-08-31 复核发现，未修）**。`busy` 从头到尾在同一次同步调用里
      置真又置假，中间不回事件循环，所以 `BusyIndicator` 一帧都渲染不到——全分辨率合成期间
      界面直接卡住且无反馈；`exportCover()` 在输出目录为空时静默 return。另外 v1 的
      `onExportCover` 会在没有选中难度时提示「当前未选中难度，无法导出封面。」并暂停正在
      播放的预览，新路由无条件 emit，两个守卫都没了。

- [x] **新建 = 打开任意音频并原地建谱**（2026-08-30 所有者定型，同日实现）。
      先说清为什么不能是"一个能同时选文件夹和文件的窗口"：Qt 给不出。
      `QtQuick.Dialogs.FileDialog` 的 `FileMode` 只有 `OpenFile / OpenFiles / SaveFile`，
      `FolderDialog` 是另一个类型；两者都没有任何自定义挂点
      （FileDialog 全部成员只有 `fileMode` / `selectedFile(s)` / `currentFile(s)` /
      `currentFolder` / `options` / `nameFilters` / `selectedNameFilter`），
      Widgets 的 `QFileDialog` 也只有 `setSidebarUrls`（仅非原生生效）。
      平台层的 `NSOpenPanel.accessoryView` / `IFileDialogCustomize` 存在但 Qt 不暴露。
      **所以选择只能落在一种资源上**，所有者定为音频。
      现在的行为：`文件 → 新建`（Ctrl+N）→ 过离开守卫 → 选音频 →
      **在音频所在目录原地建 `maidata.txt`**（不是子文件夹）→
      把音频复制一份为 `track.<原扩展名>`；**本来就叫 `track.*` 就不复制**。
      `maidata.txt` 或 `track.*` 已存在时各确认一次。
      还有一条不能沉默的情形：引擎按 `track.mp3 → .wav → .flac → .ogg` 的顺序找音轨，
      所以复制落在靠后的扩展名、而靠前的已存在时，谱面会用另一个文件——
      这时会明确提示是哪个，而不是留到播放时才发现。
- [x] **恢复 v2 字体功能**（2026-08-30）。`QmlExportSession` 以 QML 原生文件请求边界复用
      `FontLibrary` 的导入/校验/便携复制数据层：导出页的片头页可独立选择、导入、重置难度卡
      display/body 字体，并即时刷新试听；导出页的 `皮肤` 页签**和**`预览设置 → 皮肤` 都可按区域
      选择、导入、重置 HUD 字体，且会立即重绘右侧预览。片头字段仍由 `VideoExportSettings` 持久化并流入试听/导出快照；
      HUD 继续经 `preview::scene::setPreviewHudCustomFontPath` 分区持久化。未恢复任何 v1 窗口或
      Widgets 接线。

- [x] **音频设置的试听**（2026-08-30 记录，同日补齐）。`QmlAudioSettingsModel` 自带一个
      `QtPreviewSfxRuntime`：首次试听时才创建（构造会起一条音频工作线程，多数会话根本不开
      这个页面），页面关闭时 `releaseAudition()` 释放，与 v1 对话框本地运行时同一生命周期。
      样本未就绪时先发一次异步加载，由它的 completion 播放等待中的那一声。
- [ ] **预览刷新率的 120 FPS 选项在 v2 未按屏幕刷新率过滤**（2026-08-30 记录）。v1 只在
      检测到 ≥119.5 Hz 时才把 120 FPS 放进菜单，并把「屏幕最大刷新率」一项显示成
      `屏幕最大刷新率 (N Hz)`；v2 偏好设置 → 性能 的 `QmlPreferencesModel::frameRateOptions`
      两者都没做。留到「v1 & v2 文案逐项对比」那一轮一起处理。
- [x] **预览走带的时间条停在 0**（2026-08-30 用户报告，同日修复）。阶段 2 退役
      `QuickShellController` 时，它那条轮询计时器是 shell 唯一的预览状态来源；替代方案
      把底栏那一半改成推送，预览这一半却换成了一个以 `playing_` 为闸门的采样计时器——
      而没有任何东西告诉模型播放已经开始，闸门永不开启，于是时间轴跟随一切正常、走带
      却一直停在 0。现在播放头由**移动它的那个函数**（`applyQtPreviewPosition`）通过
      `shellPreviewPlayheadChanged` 播报，播放标志由**唯一的写入者**
      （`MainWindow::setPreviewPlayingFlag`）播报，模型不再采样任何东西——顺带修好了
      暂停时拖动时间轴/键盘定位不会移动走带滑块的问题。
      漂移守卫：`preview_transport_push_spec`。
- [x] **数值读数不能输入**（2026-08-30 用户要求，同日完成）。v1 的 `EditableValueLabel`
      单击即可就地输入；v2 现在是 `components/EditableValue.qml`，**双击**读数（音量的
      「50%」、预览设置的各滑块、偏好设置字号）变成输入框，提交时钳制到区间并对齐步进，
      与拖动走同一条应用路径。Esc 取消，失焦提交。
- [x] **设置弹窗不能拖动**（2026-08-30 用户要求，同日完成）。`components/DialogDrag.qml`
      把一块透明抓取区挂到样式自己画的标题栏上——外观零改动——首次拖动时释放居中锚点，
      位置在本次会话内跨开关保留，并始终钳制在窗口内。已用于 音频设置 / 预览设置 / 偏好设置。
      **仍是 modal**：遮罩会把预览压暗一半，且弹窗开着时无法按播放。用户只要求「可拖动」，
      所以没动 modal；若要边放边调，需要把这两个预览类弹窗改成 modeless。
- [x] **导出区间的可视化进度条**（2026-08-30）：`ExportRangeSelector.qml` 补回整曲轨道、
      区间高亮、双端点与只读播放头；最小区间为 `min(5 s, 全谱时长)`，端点只移动自身、选中区间可整体平移，
      都经与右侧预览相同的 scrub 通道拖动。短区间有视觉最小宽度；移动的「开始／结束」标签替换为静态提示带内的
      悬停／拖动时间戳，起止数值输入保留且随未聚焦的范围变更刷新。
- [x] **启动崩溃恢复弹窗仍是 Widgets**（2026-08-29 已修）：`MainWindow.DocumentAutosaveFlow.cpp`
      的三处 `UiDialogs::showMessageBox` 已改走 `UiRequestService`；
      `restoreBackupFilePath` 在确认处切开为 `restoreBackupFilePath` + `applyBackupFile` 续延。
- [x] **保存 / 退出确认弹窗仍是 Widgets**（2026-08-29 记录，用户报告；2026-08-30 已修）。
      关闭流程改为两段式：`Main.qml` 的 `onClosing` 一律先拒绝，再问
      `QmlShellLifecycle::requestClose()`，答案经 `closeDecided` 回来后窗口自己再关一次。
      C++ 侧 `confirmShellClose()` 换成 `requestShellClose(continuation)`，问句之下的所有清理
      原样搬进续延（**顺序契约不变**：破坏性清理只在确认为 true 之后）。提示本体是
      `UiRequestService::requestChoice`——新增的三选一请求，未列出的答案一律解析为 dismissal，
      所以「关掉窗口」永远不会被当成保存或放弃。
      同一轮里另外三处 v2 能走到的 Widgets 弹窗一并处理：**音频拖放建谱**（预览确认 + 未保存
      决策 + 三个结果提示）、**保存失败**的三条、**启动目标缺失**的两条。
      还顺带修了一个双弹窗缺陷：删除难度时 `DifficultyList.qml` 已经问过一次，
      `deleteDifficultyField` 又在后面弹了一个 Widgets 确认——现在 v2 桥接传
      `alreadyConfirmed=true`。以及删掉了没有任何调用方的 `confirmDeleteBookmark`。
      **留在树上的 `showMessageBox`**：`onNewFile` / `onOpenFile` / `openRecentFile` /
      `switchToWelcomePage` / 扩展 `showMessage` / 删除难度的 v1 与扩展路径——全部只挂在隐藏
      `MainWindow` 自己的 File 菜单 `QAction` 或扩展宿主上，QML 外壳没有任何入口能走到，
      随阶段 4 一起消失。同理 `showUnsavedChangesDialog` 与 `maybeSaveBeforeContinue()` 同步版。
- [x] **打开文件的未保存提示是平台原生弹窗**（2026-08-30 记录并修复）。`MainView.qml` 的三个
      `QtQuick.Dialogs.MessageDialog` 在 macOS 上渲染成 NSAlert——不是 Widgets，但同样不是
      应用自己的界面。现在都是 `components/ChoiceDialog.qml`，`src/app/qml_ui` 已无
      `MessageDialog`。
- [x] **控件高亮有记忆**（2026-08-29 报告，同日修复）。所有者已确认所指即 `AppComboBox`。
      成因是 `background.border.color` 读 `activeFocus`：ComboBox 点击即取焦点并保持，
      重开弹窗时焦点被恢复，边框继续是重音色。改用 `visualFocus`——只有键盘到达的焦点才画指示。
      同一属性同步应用到 `AppCheckBox` / `IconButton` 的焦点环。文本输入控件（`AppTextField` /
      `AppTextArea`）**保持 `activeFocus`**：正在输入时边框就该亮着。

### D. 已是 QML 页面，但仍借 Widgets 完成子流程（0 项）

已清零。视频导出页的文件选择与结果提示于 2026-08-29 改走 `UiRequestService` +
`components/UiRequestHost.qml`；`src/app/qml_ui` 全目录再无 Widgets 对话框
（`QmlShortcutModel.h` 里唯一的 `QtWidgets` 字样是一条注释）。

### E. 影子 Widget 状态（无用户入口，但仍在链上）

- [x] `ExportLauncherPage`（738 行）已删除（2026-08-29）。导出难度的真相移到 `QmlExportSession`：
      `resolveToolsMenuExportDifficultyId()` 现在读 `pageSessionActive() + selectedDifficultyId()`，
      原先的 `menuActionDifficultyId()` / `selectedDifficultyId()` 两级回退只差一个会话门控，合并为一处。
      同时删掉恒真的 `qmlExportCenterActive_` 标志——它只在 `QmlUiBootstrap` 里被置 true，v1 外壳删除后
      它守护的每个 `else` 分支都是死代码。隐藏 `editorStack_` 里换成一个空 `QWidget` 占位：
      `PreviewPlaybackGlue.cpp:42` 等 7 处仍在用 `currentWidget() == chartPage_/metadataPage_`
      判断"当前是哪个字段"，若导出页不再占据栈位，这些判断会在导出页上误判。
      **本项没有新增行为 spec**（`MainWindow` 无法实例化）；保证来自编译、`export_page` 零引用，
      以及全量 CTest 未新增失败。导出难度在 Tools 菜单各动作里的实际取值仍需原生桌面确认。

## 4. 阶段任务与完成标志

### 4.0 排期（2026-08-29 所有者指定）

**本批**（做完一项提交一次）：

1. ~~阶段 1 + 阶段 2~~ —— **已完成（2026-08-30）**。隐藏 `PlainCodeEditor` 已删、
   `qml_document_lifecycle_contract_spec` 已绿、25 个 `shellController` 消费点已迁往
   `QmlTimelineModel` / `QmlPreviewModel` / `QmlShellLifecycle`、`QuickShellController`
   与轮询已退役。唯一留在阶段 2 里的是 `TimelineQuickModel` 所有权，**所有者已决定延后**。
2. ~~关于 MiaCode / 音频设置 / 预览设置~~ —— **已完成（2026-08-30）**，三个「暂未更新支持」入口全部补上真实实现；`unavailableFeatureDialog` 在 A 类已无触发点。
3. ~~侧边栏书签折叠重新设计~~ —— **已完成（2026-08-30）**。右侧那个独立折叠按钮已删除，
   难度行本身就是折叠控件：点击非当前难度＝切换难度（行为不变），点击**当前**难度＝
   展开 / 收起它的书签；折叠指示器在**左侧**，与「难度」分组标题的箭头对齐，
   槽位恒占位所以各行色块仍然对齐。**只有当前难度显示书签**——行既然是折叠控件，
   非当前行按下去是切换而不是折叠，留在别的难度上的展开态就会是一个屏幕上没有东西能收起的列表。
   漂移守卫：`qml_editor_controller_spec` 新增一条断言（`foldsBookmarks` 存在、`foldButton` 不存在）。
4. ~~保存 / 退出确认弹窗改为 QML~~ —— **已完成（2026-08-30）**。关闭流程改为两段式，
   未保存提示走 `UiRequestService::requestChoice`（新增的三选一请求）+ `ChoiceDialog.qml`；
   §3.C 列出的调用点里**所有 v2 能走到的**都已迁移，v1 菜单专属的那些留给阶段 4。

**本批四项全部完成。** 下一轮见下。

**批次中途插入并已完成（2026-08-30，用户报告 / 要求）**：预览走带时间条停在 0（回归，见 §3.C）、
数值读数双击输入、设置弹窗可拖动。

**插入本批之后、下一轮之前（2026-08-30 所有者指定）：文档机制全盘审计与重新设计。**
所有者报告 7 项缺陷并判定文档机制需要重新设计（和 v1 不完全相同）。审计与设计已写在
[QML_UI_V2_DOCUMENT_MODEL_AUDIT_AND_REDESIGN_ZH.md](../../audit/QML_UI_V2_DOCUMENT_MODEL_AUDIT_AND_REDESIGN_ZH.md)，
含三个待拍板的产品决定。落地顺序见该文第 5 章：
Esc/非文本键 → 每视图撤销历史 → section 级脏 → 关闭标签守卫 → 新建/打开最近/关闭文档 → 两套三选一收敛。

**后续优先级（2026-08-31 调整）**：当前工作分成两条并行但不可混淆的线：

1. **Architecture Complete**：完成隐藏 `MainWindow` / native QWidget 依赖脱钩，
   抽出独立的应用服务与 session 所有者，最终切换 `QGuiApplication`，并让主程序的直接/递归
   依赖符合 allowlist。
2. **Release Complete**：完成中文文案 parity、封面页剩余功能、120 FPS 过滤、原生桌面验收、
   aggressive-GC 稳定性和跨平台发布检查。

封面谱面帧的首屏加载、正确播放位置、旧难度缓存清理等本轮问题已从功能主线闭环；封面页
忙碌反馈、导出守卫、缩放/预设等仍按 §3.C 保留。中文文案 parity 和 120 FPS 不再阻塞
Widgets 架构清理，但在 Release Complete 前必须完成。不得回接任何已删 Widgets 表面。


### 阶段 1（收尾）—— 文档域单一所有者

已落地：`ChartWorkspace`、`ChartWorkspaceFileService`、`AnalysisService`（`src/app/v2/`），生产 QML 的文档事务、save-point dirty、revision-stamped 分析快照均不再以 `MainWindow` 为真相。

剩余：

- [x] 删除隐藏 `PlainCodeEditor` 与第二份文档副本（2026-08-30）。`src/editor/PlainCodeEditor.*`
      与 `BracketCompletionPopup.*` 已删除，应用不再链接任何 Widgets 文本编辑器。
      过程中修好了两个原本已坏的功能：扩展 `editor/*` API 与 14 个谱面变换命令，
      两者此前都作用在一个从未被 QML 选区同步过的隐藏光标上。
- [x] 修复 `qml_document_lifecycle_contract_spec`（2026-08-29，见 §7.1）。
- **阶段 1 除延后项外已收尾。**
- [ ] **增量时间轴解析已实际失效**（2026-08-30 记录）。`applyTimelineQuickChange` 只由隐藏编辑器的
      `contentsChange` 驱动，而每次 v2 写入都经 `setEditorText` 抑制脏跟踪，所以它从未运行——
      v2 的每一次编辑都是全量重建。隐藏编辑器删除后这条路彻底断了。重新接上的正确位置是工作区提交
      （它知道确切编辑范围），属于阶段 2 `TimelineQuickModel` 所有权那一项，**所有者已决定继续延后**。

### 阶段 2 —— `PreviewSession` / `TimelineSession`，退役 `QuickShellController`

> **2026-08-30 所有者决定：`TimelineQuickModel` 所有权迁移继续延后。**
> 阶段 2 的其余部分（`shellController.*` 消费点迁移、MainWindow 改推送、删 `refreshTimer_`
> 与 `src/app/quick_shell/`）按「一次做到底」推进。

- [x] 把 25 个 `shellController.*` QML 消费点搬走（2026-08-30）。落点是三个对象而非两个：
      `QmlTimelineModel`（时间轴与底栏页签）、`QmlPreviewModel`（原有对象，接管预览剩余项：
      画布比例、播放切换、速度步进、交互日志）、`QmlShellLifecycle`（根窗口关闭契约）。
      其中三处不需要新对象：`exportPageActive` 与 QML 已有的 `pages.activePageId === "export"`
      是同一个条件；`workspacePanelsSwapped` 就是 `preferencesModel.previewOnLeft`。
- [x] **轮询消失**（2026-08-30）。`refreshTimer_` 每隔 200ms/33ms 拉 ~25 个值、无论是否播放。
      现在离散状态由 `MainWindow::shellPresentationChanged` 推送。
      *更正（2026-08-30 同日）：这一条最初留了一个「播放时钟仍需采样、定时器只在播放期间运行」
      的例外，而那个例外本身就是缺陷——闸门 `playing_` 从来没有推送方，定时器从未启动，走带
      一直停在 0（见 §3.C）。播放头改由 `applyQtPreviewPosition` 通过
      `shellPreviewPlayheadChanged` 推送，采样定时器已删除，`QmlPreviewModel` 现在不采样任何东西。
      漂移守卫 `preview_transport_push_spec` 会拒绝把定时器放回去。*
- [ ] `TimelineQuickModel` 所有权从 `MainWindow` 迁到 `TimelineSession`。**已延后。**
      *更正（2026-08-30）：此前记的「增量解析本身已完成，剩下的只是所有权」在实现上成立、
      在运行上不成立。`applyTextChange(QString,…)` 确实取代了 `applyContentsChange(QTextDocument*)`，
      但唯一的驱动方是隐藏编辑器的 `contentsChange`，而它被 `setEditorText` 的抑制挡住，
      所以 v2 从未走过增量路径——每次编辑都是 `scheduleTimelineRefresh()` 全量重建。
      隐藏编辑器删除后连这条驱动也没有了。要恢复增量，得改由工作区提交驱动（它知道确切编辑范围），
      这正是本项要做的事，不只是搬所有权。*
- **完成标志**：~~`refreshTimer_` 消失~~✅；~~QML 不再出现 `shellController`~~✅；
      `src/app/quick_shell/` 部分删除——`QuickShellController.{h,cpp}`（866 行）与
      `QuickShellContracts.h`（127 行，三个抽象基类）已删；目录里剩下的
      `QuickShellPreviewCompositeSurface.*` / `QuickShellPopupPosition.h` /
      `QuickShellKeyboardActivation.h` / `QuickShellPreviewSurfacePolicy.h` 是预览合成表面的
      平台管道，与外壳控制器无关，随阶段 4 的 `MainWindow` 一并处理。

### 阶段 3 —— `ExportService` 与 Widget 对话框归零

已建成的共享边界（`src/app/v2/`，均无 Widgets，各自有只链 `Qt6::Core`+`Qt6::Test` 的 spec）：
`UiRequestService`（选文件 / 消息 / 带动作的消息）与 `JobProgressService`（进度 + 不确定态 +
按 token 的协作式取消）。`MainWindow` 持有唯一实例，`MainView.qml` 承载唯一的 `UiRequestHost`
与 `JobProgressOverlay`。

- [x] **进度条归一（2026-08-29）**：视频导出、批量导出、ZIP 打包、音视频处理（4 条 ffmpeg 流程）
      现在全部走同一个 `JobProgressOverlay`。`src/app` 已无 `QProgressDialog`。同时删除了两处
      并行进度表示：预览传输条上的 inline 导出进度（`videoExportUseInlineProgress_` 自嵌入面板
      删除后再无人置 true）与恒返回 -1 的 `shellVideoExportProgressSeconds()`（连带去掉
      `QuickShellController` 轮询间隔对它的依赖）。

- [x] `QmlExportSession` 的 `QFileDialog` / `QMessageBox` 换成 QML 侧 `QtQuick.Dialogs` + 结果值
      （2026-08-29）。落点是可复用的 `miacode::v2::UiRequestService` + `components/UiRequestHost.qml`，
      而不是导出页就地改写——B 类的 Net / 音视频处理 / 偏好设置页同样需要选文件与提示，共用这一条边界。
      `ui_request_service_spec` 只链 `Qt6::Core` + `Qt6::Test`，所以任何把 Widgets 对话框放回这条边界的
      改动都会**链接失败**，不依赖字符串扫描；`qml_export_video_page_spec` 驱动真实 `FileDialog` /
      `FolderDialog` / `MessageDialog` 走完请求—应答回环。仍需原生桌面确认实际弹窗外观与取消语义。
- [x] **封面导出重建为 QML 页面 + 无 Widgets 作业 API（2026-08-31）**：由
      `QmlCoverExportSession` + `CoverCompositeRenderer` 完成，见 §0 / §3.B。
- [x] **删除 CoverStudio 组（2026-08-31）**。`NetBatch*Dialog` 已在更早批次删除；
      `ExportLauncherPage`、`BatchExportPanel` 组、`VideoExportDialog` 组亦已删除。
- [x] **废弃 `VideoExportDialog` 的扩展 API 形态**（2026-08-29）。扩展命令 `export.video.start`
      与方法 `export/startVideoExport` 原本打开模态导出对话框，现在改为打开 QML 导出中心的单个导出页
      （`onExportPreviewVideo()` 保留同名但换成 QML 路由）。扩展面能力未削减，Widgets 对话框消失。
      `FontLibrary` 作为无 UI 的字体数据服务保留；`CardFontSettings` 已随原封面 Widgets
      工作台删除，`HudFontSettings` 仍属于阶段 4 的隐藏 MainWindow 遗留。
- [x] `ChartDropOverlay` 改为 `QmlChartDropBridge` + `Main.qml` QML 提示层（2026-09-01）。
- [x] **阶段 3 / 0b 本轮收口记录（2026-09-01）**：扩展宿主、嵌入运行时、Open Bridge、watcher、
      扩展偏好设置/事件/手势及 bundled deployment 已从产品运行时移除；manifest/schema/SDK/docs/API
      registry 与离线校验仍保留。背景设置恢复为 `QmlAppBackgroundModel` + `AppBackgroundSettings`，
      根窗口由 QML 负责背景图和 overlay token。ChartDrop 已由单一 `QmlChartDropBridge` 事件适配器
      和 `ChartDropImportService` 异步事务服务承接，释放时会使迟到回调失效，QML 只显示非拦截提示层。
      构建后产物不再保留旧的 `extensions` 目录。
- **本轮验证记录**：主程序及相关 target 构建通过；新增/定向 CTest 7/7 通过；扩展一致性与 macOS
      打包契约通过；最新全量 CTest 通过 88/89 项。剩余 `qtavplayer_platform_spec` 的
      seek-before-start 断言未涉及本轮改动，另行处理。GUI harness
      按所有者要求跳过，故背景视觉和桌面拖放仍不记为原生 GUI 验收通过。
- **阶段 3 完成标志**：`grep -rl "QtWidgets\|QDialog\|QMessageBox\|QFileDialog" src/tools src/app/qml_ui`
  只剩**扫描这些字符串的 spec 自己**。2026-08-31 复核实测：
  `QmlExportFontContractSpec` / `QmlDocumentLifecycleContractSpec` / `QmlCoverExportContractSpec` /
  `UiRequestServiceSpec` / `UiTextLocaleSpec`（都是断言里写着这些名字），
  `QmlShortcutModel.h`（注释一句），加上唯一一个真的用 Widgets 的
  `src/tools/video_export/HudFontSettings.cpp`——它只被隐藏 MainWindow 的导出设置对话框调用，
  属于阶段 4 的遗留。原来那条「为空」的写法永远不可能成立，因为 spec 必须提到被禁的名字。

### 阶段 3.5 —— 应用宿主脱钩与依赖分层

这是删除 `MainWindow` 之前新增的架构关卡，避免把“换应用对象”误当成架构完成：

> **当前状态（2026-09-01 第二轮）**：依赖分层三项（4 / 5 / 6）已完成，服务所有权（第 1 项）已完成。
> 隐藏 `MainWindow` 仍然存在：`QmlApplicationContext` 仍持有 `MainWindow& backend_`，文档切页仍经过
> 隐藏 `DocumentSection`/Widgets 状态，因此 `QtWidgets` 仍是当前产品依赖，且在 allowlist 里被明确
> 标为「遗留 / 阶段 4 退出」。第 2、3 项不能由本轮的服务装配标记完成。

- [x] `ApplicationServices` 已建立并**成为真正的所有者**（2026-09-01）。
      `src/app/v2/ApplicationServices.{h,cpp}` 是一个不含 Widgets 的 `QObject`，持有
      `ChartWorkspace`、`ChartWorkspaceFileService`、`AnalysisService`、`EditorSyncController`、
      `ChartDropImportService`、`UiRequestService`、`JobProgressService` 七个服务。
      在此之前这七个服务分属两个 UI 对象：`MainWindow` 持有后四个，`QmlApplicationContext`
      持有前三个——「文档域归谁」取决于你问哪一个，而两个答案都是 UI 对象。
      现在 `QmlUiBootstrap` **先**构造 `ApplicationServices`、**再**构造 `MainWindow` 并把它传进去；
      窗口只借用，`ui_.uiRequests_` / `editorSyncController_` / `chartDropImportService_` 全部改为
      指向装配对象，`QmlApplicationContext` 的 `uiRequests()` / `jobProgress()` / `editorSync()`
      也不再经过 `backend_`。销毁顺序写死为「服务最后释放」。
      CLI 导出路径（`cli_video_export.cpp`）走同一条装配。
      顺带把 `uiValidationLocale()` 的实现从 `MainWindowShared`（一个 QtWidgets TU）搬到
      `miacode::v2`，`MainWindowShared` 保留同名函数转发——「解析器用哪个语言校验」不该需要 widget 层。
      守卫 `application_services_spec` 只链 `Qt6::Core`/`Gui`/`Test`，所以任何 QtWidgets 依赖爬进
      装配对象都会**链接失败**；它还扫描整个 `src/app`（除装配自身），禁止任何地方再 `new` /
      `make_unique` / 以值成员形式构造这七个服务——否则装配对象会变成第三个所有者而不是唯一所有者。
      这条反向验证已实测：在 `MainWindow.h` 里加一个 `UiRequestService` 值成员，守卫立刻失败。
- [ ] `QmlApplicationContext` 不再持有 `MainWindow& backend_`；QML 通过窄 QObject 门面访问
      services / sessions，不能再经隐藏窗口取得状态、命令或页面切换。
      *进展（2026-09-01）*：
      1. 文档域、UI 请求、作业进度、editor-sync 已改为经 `services_` 取得；`backend_` 不再是这
         四者的来源。`uiRequestService()` / `jobProgressService()` 这两个方法**已从 QML 层完全消失**
         （13 处调用点改为构造时注入 `UiRequestService&` / `JobProgressService&`）。
      2. 剩余耦合已全部登记为可度量清单：
         [QML_UI_V2_BACKEND_SURFACE_ZH.md](QML_UI_V2_BACKEND_SURFACE_ZH.md) 按未来所有者
         （PreviewSession / TimelineSession / ExportSession / 文档 / 偏好设置 / 延迟 / 媒体工具 /
         外壳宿主）分组列出 **120 个方法 + 17 个直接读取的私有成员 + 5 个 friend 授权**。
         守卫 `qml_ui_backend_surface_spec` 对代码与清单做集合相等比较：新增耦合失败，
         搬走了却没改清单也失败，文档虚报计数同样失败——四个方向都已反向验证。
      3. **本轮发现并记录的事实**：`MainWindow.h` 把 5 个 QML 类型声明为 `friend`，它们绕过公有
         接口直接读写窗口私有成员（`QmlExportSession` 一个就占 15 个）。这与本项要求的
         「窄 QObject 门面」正相反，所以清零顺序是**先消 friend 与私有成员读取，再削减公有方法**；
         先搬公有方法只会让剩下的耦合更隐蔽。
      4. 按上述顺序先动了第一项：`QmlEditorPageHost` 的两处私有成员读取换成等价公有访问器
         （私有成员 17 → 15，方法数不变，因为这两个名字本来就在清单里被别的文件用着）。
         它的 `friend` 授权仍保留——四个 `switchTo*Field` 声明在私有 `.inc` 里，把它们改成公有
         等于把接口做宽；它们随第 3 项（页面路由归 QML 宿主）一起消失。
      5. **预览外观八个值搬进 `miacode::v2::PreviewAppearanceState`（私有成员 15 → 4）**。
         皮肤目录/variant、判定特效、判定线外观、slide 提前、tap 判定文字距离、中央显示模式、
         开场音——这八个值决定屏幕预览和导出视频**两边**怎么画，所以三个调用方都要用它们。
         它们现在由装配对象持有；`MainWindow` 把同名成员按引用绑到这一份上（`MainWindowState`
         里的声明删除，`state_.x` 与别名一起改指），所以读写它们的约 26 个文件一行没动。
         窗口改为**响应** `skinChanged` / `judgeEffectStyleChanged` / `introSoundChanged`
         去应用到实时表面并持久化——三个信号分开是因为代价不同，合成一个会让每次切判定特效
         都重载整套皮肤。`previewCanvas_` / `previewSfxRuntime_` / `previewAudioSettings_` 三个
         私有成员换成两个窄方法 `refreshPreviewSurfaces()` / `applyPreviewSfxLevels()`。
         结果：`savePortableState` 和 `applyPreviewSkinDirectoryToSurfaces` 从 QML 面上**消失**
         （持久化归值的所有者，不归调用方），方法数仍是 120（去二进二）。
         **同时消掉一处真实缺陷来源**：皮肤选择的「比较目录名 → 赋值 → 推导 variant → 应用 →
         持久化」原本有**三份拷贝**（QML 导出页、QML 预览设置页、Widgets 导出设置对话框），
         各自推导 variant，任何一份写错就会出现「目录是 skinDX、variant 还是 Standard」。
         现在 variant 由 `PreviewAppearanceState::variantForDirectory()` 单点推导，
         `setSkinDirectory()` 根本不接受不一致的组合，`preview_appearance_state_spec` 守住这条。
         顺带把 `PreviewSkinVariant` 从 `MainWindow` 的嵌套枚举移到
         `core/video/PreviewRenderSettings.h`——它是渲染设置，不该逼每个消费方包含窗口头文件。
      6. **剩下三个可直接换掉的私有成员改走窄访问器（私有成员 4 → 1，方法 120 → 124）**。
         `document_` 的四处读取改走本来就公有的 `documentDifficultyIds()` /
         `documentDifficultyChartText()`（语义完全等价：`difficultyIds()` 就是难度表的键集，
         所以 `contains(id)` 与 `difficulty(id) != nullptr` 是同一个问题）；
         `projectLastOpenedDifficultyId_` 与 `muriRenderOptions_` 各加一个具名访问器。
         方法数上升是预期内的：`friend` 给出的是**无边界**访问权，换成具名访问器后这份耦合
         第一次变成可枚举、可逐条搬走的东西。
         **全仓库现在只剩 `QmlExportSession` 的 `exportSection_` 一处私有成员直读**，
         它是真正的导出引擎，必须随 `ExportSession` 一起搬，不能用访问器糊过去。
      7. **记录一个下一步的陷阱**：`document_` 的读取虽然改走了公有访问器，但读的仍是
         `MainWindow` 的文档副本而不是 `ChartWorkspace`。看似应该顺手改读工作区，但
         `MainWindow` → 工作区是**延迟**同步的（`documentReplaced` 经
         `QMetaObject::invokeMethod` 排队后才 `adoptBackendDocumentReplacement()`），
         后端替换文档（启动 / 根窗口拖放 / 原生打开 / 崩溃恢复）之后存在一个窗口期，
         此时改读工作区会让导出页列出**旧的**难度列表。要动它得先处理那条延迟同步。
         细节已写进 [QML_UI_V2_BACKEND_SURFACE_ZH.md](QML_UI_V2_BACKEND_SURFACE_ZH.md)。
      8. **friend 授权 5 → 2**。把 `QmlCommandService` / `QmlPreviewModel` /
         `QmlPreviewSettingsModel` **本来就在调用**的 8 个方法从私有改为公有，集中成
         `MainWindow.h` 里一个具名的「QML 页面的有界入口」块，然后删掉这三条 `friend`。
         8 个方法是：5 个皮肤/判定线目录查询（纯路径解析与目录枚举）、`applyPreviewOutlineVariant`
         与 `setMuriRenderMode`（两者早就带 `persist` 参数，与既有偏好设置公有面同形）、
         `onPreferences`。**清单计数一个都没动**——这些名字本来就在清单里；变的是访问权从
         「无边界，任何后续改动都能拿到任何东西」变成「就这 8 个」。
      9. **导出引擎接缝立起来，私有成员直读归零，friend 授权 2 → 1**。
         `exportSection_` 是全仓库最后一处私有成员直读，也是唯一一处加访问器只会让问题
         **更正式**的耦合——页面依赖的不是一个值，而是窗口内部的一整块引擎。
         做法是先立接口：`miacode::v2::ExportEngine`（七个操作：seed task、apply live settings、
         start/stop audition、launch video/batch export、cancel），`MainWindow::ExportSection`
         实现它并把自己装进 `ApplicationServices` 的槽位，`QmlExportSession` 只认接口。
         那 3,600 行实现**没有搬**——搬它是阶段 4 的事；变的是方向：实现真的搬出窗口时，
         只有实现侧要改，页面一行不动。
         槽位而不是快照：消费方绑定 `exportEngineSlot()`，窗口在 `~MainWindow` 最开头撤销引擎，
         所以撤销对每个持有者立刻可见——导出页是 QObject child，会比 `exportSection_` 活得久，
         这正是防止它调进一个已析构 section 的机制。
         剩下的 `currentPreviewAuthoritativeAudioClockSecond` / `refreshExportIntroState`
         按第 8 条的办法公开，`QmlExportSession` 的 friend 授权随之删除。
         守卫 `export_engine_spec` 只链 Core+Gui+Test，证明这个契约不需要窗口就能实现，
         并守住槽位纪律与「用户取消是结果不是失败」。
         **只剩 `QmlEditorPageHost` 一条 friend**：四个 `switchTo*Field` 驱动隐藏 widget 栈，
         公开它们等于把一件本该消失的事写进正式接口，随第 3 项一起消失。
     10. 顺带修好上一轮的一处文档损坏：清册的按文件分块在正则重生成时丢了换行，
         把小标题粘到了上一条 bullet 后面（15 块显示成 8 块）。本次整节按结构重新生成。
     11. **页面路由改由 `miacode::v2::EditorPageRouter` 承接，QML 类型的 friend 授权归零，
         方法 124 → 118**。四个 `switchTo*Field` 声明在私有 `.inc` 里，上一轮判定「公开它们
         等于把一件本该消失的事写进正式接口」——这个判断成立，所以做法和导出引擎一样是**先立接口**：
         七个操作（`hasActiveDifficulty` / `activeDifficultyId` / 四个 `enter*Page` /
         `packChartAsZip`），`MainWindow` 实现，`switchTo*Field` **保持私有**。
         `QmlEditorPageHost` 现在只认接口，`friend class QmlEditorPageHost` 删除——
         至此 `MainWindow.h` 对 QML 类型的 friend 授权全部为 0，只剩 widget 侧的
         `LatencySandboxController`（随阶段 4 处理）。
         槽位纪律与导出引擎一致：装配对象持槽，窗口在 `~MainWindow` 最开头撤销。
     12. **顺带修掉一个真实缺陷：`switchToExportField` 曾在切换发生前就返回 `true`**。
         它把真正的切换延后一个事件循环 tick，理由是「导出页要建嵌入式视频面板、会阻塞 UI 线程，
         先让侧栏 Export 行上的忙碌转圈画出来」。这两个理由现在都不成立：嵌入式面板随 Widgets
         导出对话框一起删了；侧栏是 QML，那个 spinner 建在**隐藏**的 `outlineList_->viewport()` 上，
         永远到不了屏幕。留下来的只有「报告一个尚未发生的成功」——保存守卫拒绝切换时，
         QML 页面宿主仍会把导出页显示出来，外壳和文档对「用户在哪一页」的认知就此不一致。
         现在改为同步执行并返回真实结果（`currentWidget() == exportPlaceholderPage_`），
         三个 `*OutlineExportBusySpinner` 辅助函数删除。
         `editor_page_router_spec` 守住这一条，反向验证过：把延迟改回去立刻失败。
         **未做、已记录**：`outlineBusySpinner_` 本体和 `tickOutlineBusySpinner()`
         在导出流程里还有 4 处调用，同样画在隐藏 widget 上、同样看不见。整套删除会扩散到
         导出流程，不属于本项，留作阶段 4 的清理。
- [ ] `QmlUiBootstrap` 不再创建隐藏 `MainWindow`；根窗口、拖放、关闭和对话框 transient parent
      都由 QML 宿主及应用服务明确拥有。
      *进展（2026-09-01）*：前置条件已满足——服务不再由窗口创建，窗口的存在不再是它们的前提。
      但 `QmlUiBootstrap::start()` 仍然 `make_unique<MainWindow>`，本项未完成。
- [x] 为 `MiaCode` 建立 Qt 与第三方依赖 allowlist（2026-09-01）。
      [docs/ops/DEPENDENCY_ALLOWLIST.md](../../ops/DEPENDENCY_ALLOWLIST.md) 按宿主 / 渲染 /
      媒体 / 导出 / 平台 / 遗留六层登记 `MiaCode` 链接的每一个库，逐条写明平台条件、直接使用点、
      加载时机和验证方式。守卫 `dependency_allowlist_spec` 解析全部
      `target_link_libraries(MiaCode …)` 与文档三张表比对，五个漂移方向都有反向验证：
      新增依赖漏登记、文档留着已删依赖、禁止表里的库被链接、Qt 版本不一致、QtAVPlayer 头文件
      泄漏出适配层。
      顺带清掉一条假依赖：`OpenGL` 曾被写成 `REQUIRED` 组件却没有任何 target 链接
      `Qt6::OpenGL`；守卫新增「REQUIRED 组件必须被链接或声明为构建期组件」这一条后它无法再回来。
- [x] `src/tools/net` 移出主程序（2026-09-01）。Net 页面已从 v2 产品运行时移除，所以
      `src/tools/net/` 现在只在 `net_client_spec` 里编译（`MIACODE_BUILD_DEV_TOOLS=ON` 才构建），
      `Qt6::Network` 从 `MiaCode` 的链接行和产品作用域 `find_package` 一并删除，改到 dev-tools
      分支。两个此前无人断言的批量 worker 也搬进该 target，否则它们会变成谁都不编译的死代码。
      **诚实记录**：`QtNetwork` 框架仍会被加载——它是 `Qt6::Qml` 的传递依赖。本项改变的是
      「产品是否自己使用网络」，不是部署包里少一个框架；allowlist 的「传递依赖」一节写明了这点
      和复核命令，不允许把它说成依赖数量减少。
- [x] `Qt6::MultimediaQuickPrivate` 锁定并收口（2026-09-01）。它在 `src/` 里**没有任何直接
      使用点**，存在的唯一理由是 `third_party/QtAVPlayer` 的 `QT_AVPLAYER_MULTIMEDIA` 帧桥；
      allowlist 列出**允许 `#include <QtAVPlayer/…>` 的 7 个文件**（`PreviewStageMediaHost*` 六个
      TU 加 `PreviewSharedD3D11Device.cpp`），守卫扫描整棵 `src/` 树，多一个文件就失败。
      版本锁由守卫要求 `CMakeLists.txt` 每一处 `find_package(Qt6 <ver> …)` 与文档写死的 `6.8`
      一致来保证。改用公共 Multimedia/VideoOutput API 的后续项已写在 allowlist 末尾（阶段 4 之后，
      未排期，前置条件是帧桥不再需要 `qsgvideonode` 私有头）。

### 阶段 4 —— 删除 `MainWindow`，收口为 Qt Quick/QML 宿主

- [ ] 搬迁/删除 `src/app/mainwindow/` 全部文件；仅保留已抽出的应用服务、预览/媒体/导出和领域能力。
- [ ] `src/app/ui/` 的 widget 辅助件（`UiComponents`、`EditableValueLabel`、`FlowLayout`、
      `BusySpinner`）随之删除或改为非 Widget 服务；同时替换
      `QStyleFactory`、`topLevelWidgets`、native Widget effect 等旧语义。
- [ ] `main.cpp` 最后换为 `QGuiApplication` + `QQmlApplicationEngine`；不得机械替换后继续
      依赖 `QApplication` API。
- [ ] `CMakeLists.txt` 的 Qt components 与 `MiaCode` target 去掉 `Widgets`；生产代码、全部
      构建 target 和隐藏 fallback 均不得重新引入 QWidget 类型。
- [ ] `ShaderTools` 若仅为构建期工具，不计入运行时依赖；`Qt6::Svg`、`Qt6::OpenGL`、
      `Qt6::Network`、`Qt6::MultimediaQuickPrivate` 按实际功能和平台条件单独验收。
- **Architecture Complete 完成标志**：无隐藏 `MainWindow` / native QWidget 拖放 overlay，
      主 UI 进程无 `Qt6::Widgets` 和活动 QWidget 依赖；Release 构建、全量 CTest、QML import
      部署扫描通过，并完成 macOS / Windows 冷启动、编辑预览、媒体、普通导出和封面导出的
      依赖记录。功能 parity 与人工 GUI 结果另按 Release Complete 判定。

### 0b（已完成，归档保留）/ 0c（已改判并完成）

- [x] 0b 扩展宿主删除（2026-09-01）——产品运行时、宿主、Open Bridge、扩展 UI/事件/手势/
      watcher 与 bundled deployment 均移除；manifest/schema/SDK/docs/API registry 和离线工具保留。
      Net 引擎的 `Qt6::Network` 归属仍由阶段 3.5 决定。
- [x] 0c 偏好设置 / 延迟检测 / 音视频处理页 —— **三页均已补成 QML 原生页（2026-08-29）**。
      ~~架构文档第 8、10 节仍记录着相反的删除决定，需改齐。~~ **已改齐（2026-08-30）**：
      架构文档第 8 节的 0c 行改为 ✅「不删，补成 QML」，阶段 1 / 2 行同步标记完成；
      第 10 节「范围裁剪的代价」改为记录**实际**删除项（Net 暂移除、扩展页签与开发者工具），
      并写明「哪怕功能会缺失也要做」那条授权已被所有者收回。

## 5. 已完成（事实登记，不再逐条展开）

- 阶段 0a：v1 QuickShell 外壳、入口、原生表面再宿主、皮肤切换（`resolveUiSkin`）全部删除，`--ui=v1` / `MIACODE_UI_SKIN` 不复存在。
- 文档域：`ChartWorkspace` + `ChartWorkspaceFileService` + `AnalysisService` 接管文档事务、save-point dirty 与 revision 分析快照。
- 编辑器：共享 `SimaiTextEditPolicy`、补全、书签、查找替换、诊断跳转、语法高亮、快捷键（`QmlShortcutModel` 复用 `ShortcutRegistry`）。
- 同步链：`EditorSyncController` 统一播放跟随、离散导航、编辑器上下文与触控创作，全部经队列边界投递；`TimelineView`（QWidget 时间轴，约 3,470 行）已删除。
- 时间轴/底栏：缩放、亮度、follow-code、validation / Muri 三 tab、底栏高度联动、工作区面板互换。
- 预览：三态渲染模式（`PreviewRenderModeMenu.qml`）、负时间传输条、surface 互斥生命周期、统计投影去热路径。
- 导出：`ExportVideoPage.qml` + `QmlExportSession`，含片头音文件与 0–200% 音量，写入单个/批量 snapshot 与 worker。
- ChartDrop：root window 登记 + 事件过滤器 + overlay 同步。
- 行高亮：`ChromeRow.qml`（2026-08-29）把「HoverChrome 背景 + 内容内缩」封成一个类型，13 处
  `background: HoverChrome` 调用点迁移完毕。剩余直用者只有 `AppMenuItem`（必须是 MenuItem）
  与四个纯图标按钮（内容居中，没有会贴边的文字）。
- 编辑器外观：`EditorTextStyle`（QML 类型）把 行距 落到 TextArea 的 QTextDocument 上（每个
  块的 bottomMargin），`QmlUiSettings::setEditorAppearance` 让 字号 / 行距 都能实时到达 QML
  编辑器——此前两者都只写到了 v2 已不显示的 Widgets 编辑器。
- 设置页（2026-08-30）：关于 MiaCode、音频设置（含试听）、预览设置三页原生化，A 类清零。
  混音十条通道与预览的二十项都以**表**的形式存在（`QmlAudioSettingsModel` 的 `ChannelSpec`、
  `MainWindow::previewRenderSettings()` 的键值对），而不是逐个属性——Widgets 对话框正是因为
  逐个手写才让滑块与静音按钮各自接线并走样。
- 共享表单控件（2026-08-30）：`LabeledCombo` / `LabeledSlider` 从偏好设置里的内联组件提升为
  组件；`EditableValue` 补回 v1 `EditableValueLabel` 的就地输入（改为双击）；`DialogDrag`
  让设置弹窗可以拖开，且不改变任何外观。
- 推送而非轮询（2026-08-30）：`shellPresentationChanged`（离散壳状态）与
  `shellPreviewPlayheadChanged`（播放头）取代了 `QuickShellController` 的轮询定时器，
      `QmlPreviewModel` 不再采样任何东西；`preview_transport_push_spec` 钉住这条契约。
- 本地化与封面（2026-08-31）：`UiText.qml` 成为 v2 QML 唯一可见文案通道，源串经
  `qmlSourceKeys()` 映射到词典 **key**（译文只有词典一处），`ui_text_locale_spec` 断言每个
  QML 实参可解析；封面导出成为 `CoverExportPage.qml` 的三栏合成器，并以同源 Quick 场景输出，
  不再创建任何 Widget 工作台。页面上的谱面帧静帧依赖应用引擎注册 `coverchart` image
  provider（`QmlUiBootstrap`）——离屏渲染器注册在自己的私有引擎上，两处都需要。

## 6. 验收规则

1. 每个域搬迁**之前**先补该域的契约回归，确认红态再搬（架构文档第 9 节）。
2. QML 回归驱动**真实组件与真实事件**，不用源码字符串扫描代替。
   —— §7.1 正是源码扫描式契约的失效案例。
3. `QT_QPA_PLATFORM=offscreen` 在本机会触发既有 macOS 平台插件崩溃，**不能**用作桌面视觉/输入验收。
4. 原生桌面验收按平台分别记录；macOS 通过不能推断 Windows 通过。

## 7. 遗留问题与更新后待复核

### 7.0 应用背景功能已恢复（2026-09-01）

- **实现**：背景设置由纯数据契约 `AppBackgroundSettings` 和 `QmlAppBackgroundModel`
  承接；`Main.qml` 以非交互 QML 图层加载背景图，并通过 `Theme.qml` 的 light/dark
  overlay token 为工具栏、状态栏、面板、编辑器标题、输入框和代码编辑器提供透明度。
- **状态**：偏好页已恢复为五页签中的“背景”页，路径可读性、清空、选择、透明度、模糊、
  缩放与位置均走 QML 模型；无效持久化路径不会覆盖当前有效状态，保存失败会保留当前值并返回错误。
- **契约验证**：`app_background_settings_spec`、`qml_app_background_model_spec` 和
  `theme_variant_resolver_spec` 已通过；按所有者要求，本轮不执行 GUI harness 视觉验收。

**本章是交接面。** 每条写明：问题是什么、当前状态、以及在 `117a76a1` 之后**必须重新核实什么**。
未列为"已复核"的条目，一律不得由构建、CTest 或历史记录推断为通过。

### 7.1 `qml_document_lifecycle_contract_spec` 已恢复绿（2026-08-29 已处置）

- **现象**：断言 `initial, navigation, and follow identities stay on the committed workspace revision` 失败。
- **原因**：该断言用源码字符串扫描，要求 `appliedQmlWorkspaceRevision_ > 0` 同时出现在
  `MainWindow.PreviewTimelineFlow.cpp` 与 `MainWindow.ValidationFlow.cpp`。`117a76a1` 重写了这两个文件：
  前者改为把 revision 直接交给 `editorSyncController_->requestNavigation(...)`（`PreviewTimelineFlow.cpp:1518`），
  后者的诊断跳转整段移走，只剩 `clearPreviewFollowDecoration()` 里的 `publishFollow`。
- **已复核（2026-08-29）**：门控确实完整地在 `EditorSyncController` 里，而且比原来更强——
  每条路径在**提交时**和**投递时**各查一次 `readinessAccepts`，因此在提交后、队列跑完前
  失效的请求不会被投递，而是以 `applied=false` 收尾。
- **处置**：新增 `editor_sync_controller_spec`（`src/tools/v2/EditorSyncControllerSpec.cpp`，
  只链 `Qt6::Core` + `Qt6::Test`），六项行为断言直接驱动真实对象：未就绪拒绝、
  过期 revision / 不同难度拒绝、投递前失效则不投递并回报未应用、隐藏编辑器会结清挂起导航、
  元数据模式不接受谱面导航、caret / 指针 / 触控锚点 / 预览 seek 共用同一门控且相同 caret 不重复发布、
  follow 作为投影原样携带发布方身份（这正是本轮 代码跟随 缺陷的落点）。
- 契约 spec 里那条断言改为只主张源码层面能看见的一半：导航与 follow 两个发布方都读
  `appliedQmlWorkspaceRevision_`，且 follow 不再读验证快照的 revision。
- **CTest 基线**（2026-08-30 更新）：**82 项**、唯一预期红仍是 `qtavplayer_platform_spec`，
  干净一轮是 **81/82**。这一轮新增 `editor_sync_controller_spec`、`preview_transport_push_spec`。
  数字会继续变；读的时候以本次运行的总数为准，不要以记忆中的数字判断。

### 7.2 QV4 GC 崩溃（Windows `0xC0000005` @ `Qt6Qml.dll+0x169A39`）

- 根因与复现见 [QML_RUNTIME_CRASH_AUDIT_ZH.md](../../audit/QML_RUNTIME_CRASH_AUDIT_ZH.md)。
- 审计推荐的 A/B/C 已落地：A —— `centerCursorInView()` 改为单一可合并的 `cursorCenterTimer.restart()`（`SourceEditor.qml:214`）；
  B —— `CompletionPopup.qml` 的光标/布局 `Connections` 已 `enabled: root.visible`；
  C —— `EditorSyncController` 队列边界。
- **待复核（D 项，未执行）**：`QV4_MM_AGGRESSIVE_GC=1` 下，代码跟随开启完整播放 10 轮、跨多 token、反复开关跟随、暂停/继续/停止/重播、弹窗开与关两种状态；Windows Release 与 Linux Release 各一轮。**这是本次重构最关键的验收，缺它则崩溃只能算"推测已修"。**

### 7.-1 GUI 验收结论（2026-08-29，所有者在 macOS 上走查）

所有者对本轮构建做了一次原生桌面走查，并判定：**除音视频处理工具外，全部判定为 GUI 验收通过。**

- **通过**：§7.3 的五项 + 新语义、§7.4 的底栏/时间轴菜单/渲染模式/面板互换/菜单栏位置、
  §7.6 的脏文档撤销联动、§7.9 的手工回归清单，以及 2026-08-29 两轮修复的可见结果
  （行高亮覆盖、对话框统一页脚与样式、快捷键录制、规范化下拉、侧边栏难度色块与书签折叠、
  语法列不再重叠、行距/字号实时生效、代码跟随在切换难度后仍然工作）。
- **未验收**：**音视频处理工具**（`MediaToolsDialog` / `PrependBlankDialog` /
  `PvBatchCompressionDialog` 四条 ffmpeg 流程 + PV 批量队列）。保持未验收状态。
- **这份结论覆盖不到的**：走查发生在 2026-08-29，**此后落地的一切都不在其范围内**——
  逐项清单见 §7.-0。另外本次走查在 macOS 上进行，所以
  ① §7.2 的 D 项（`QV4_MM_AGGRESSIVE_GC=1` 压测，要求 Windows Release 与 Linux Release 各一轮）
  **不因此结论而关闭**；
  ② §7.10 的「Windows 侧整体从未验证」同样不变。
  这两条继续按原状挂着，不得由本条推断为通过。

### 7.-3 2026-09-01 所有者验收（本轮改动）

所有者在 macOS 上走查了本轮（服务装配 / 预览外观 / 导出引擎 / 页面路由）引入的 GUI 面：

- [x] **预览外观与持久化**：皮肤、判定特效、判定线，预览设置与导出页共用同一份状态，重启后保留。
- [x] **页面切换**：导出页进/出/再进；未保存改动时选「取消」不再错误地显示导出页。
- [x] **导出运行与取消**、**HUD 字体**、**关闭流程**、**校验语言**、**文件对话框与进度**。
- [ ] **片头音**（换文件后试听、音量、重启后保留）—— **未验收**，本轮唯一没有确认的一项。
      它是本轮唯一改了「谁负责重载音色库」的路径（`setIntroSoundFileName` 只发布，
      窗口响应 `introSoundChanged` 调 `applyPreviewSfxLevels(reloadAssets=true)`）。

**所有者同时报告：「导出已取消」仍是 Qt 风格弹窗。已定位并修复，见 §7.-4。**

### 7.-4 阶段 3 的「Widget 对话框归零」有一个判定盲区（2026-09-01 发现并修复）

阶段 3 的完成标志是
`grep -rl "QtWidgets\|QDialog\|QMessageBox\|QFileDialog" src/tools src/app/qml_ui`
——**只扫这两处**。于是任何住在 `src/app/mainwindow` 却能被 v2 用户操作走到的 Widgets 弹窗，
都能原样通过这条检查。导出 worker 就是这样漏掉的：

- `MainWindow.ExportWorker.cpp` 的完成处理里有**三个** `QMessageBox`——取消、成功（打开/关闭）、
  失败（打开文件夹/确定）。它们全都在 v2 的主路径上：导出页 → 开始导出 → worker 结束。
- 后果所有者直接看见了：**取消单个导出弹 Qt 风格框，取消批量导出弹应用自己的通知**——
  同一个动作两种弹窗。批量那条早就走 `uiRequests_->postNotice`，单个这条没跟上。

**已修（2026-09-01）**：三个出口全部改走 `UiRequestService`——取消与无文件时的失败用
`postNotice`，成功与「有谱面文件的失败」用 `requestNoticeAction`（分别带「打开」「打开文件夹」
动作按钮，行为与原按钮一致；原「打开」按钮打开的一直是所在文件夹）。
`showCenteredLocalizedMessageBox` 辅助函数随之删除。

**守卫**：`export_engine_spec` 新增一条扫描——`src/app/mainwindow/sections/export/` 下不得出现
任何 `QMessageBox` / `QFileDialog` / `QInputDialog` / `QProgressDialog` 构造。整个导出域现在
只由 v2 走到，所以它可以拿到阶段 3 的 grep 给不了的那条检查。反向验证过。

**顺带查清、未修**：`src/app/mainwindow` 剩下约 28 处 Widgets 弹窗调用点
（`Dialogs.TrackMetadata.cpp` 11、`DocumentFileFlow.cpp` 10、`DocumentUi.cpp` 5、
`DocumentFlow.cpp` 1、`DocumentAutosaveFlow.cpp` 1）。逐一核对入口后判断它们**都在隐藏菜单路径上**：
`onNewFile` / `onSaveFileAs` / `openRecentFilePath` 都不被 `src/app/qml_ui` 调用（v2 走
`QmlDocumentModel` 的 `uiRequests_` 路径），删除难度那条被 `alreadyConfirmed` 挡掉。
**这是逐个入口核对的结论，不是全调用图证明**——若再出现「Qt 风格弹窗」报告，先查这份名单。

### 7.-0 2026-08-30 之后新增，全部**未验收**

§7.-1 的走查发生在 2026-08-29。此后落地的东西没有任何一项被在原生桌面上看过，
不得由构建或 CTest 推断为通过：

- [ ] **关于 MiaCode**：版本号 / 平台三元组 / 构建类型是否正确；平台与构建类型两行可选中。
- [ ] **音频设置**：十条通道的音量与静音、Break 星星尾判音开关、两个本地预设按钮；
      **试听**（松手出声、拖动中安静、预览播放时不试听）；主静音是否带动其余九行；
      拖动滑块是否不再中途中断。
- [ ] **预览设置**：视频 / 玩法两个页签共 20 项是否即时改变预览。
- [ ] **走带时间条**：播放时是否跟走；**暂停时**拖时间轴 / 键盘定位是否也移动滑块。
- [ ] **双击输入数值**：音量「50%」、预览设置各滑块、偏好设置字号。
- [ ] **弹窗拖动**：音频设置 / 预览设置 / 偏好设置拖标题栏；关掉再开是否保留位置；
      窗口缩小后是否仍被钳制在窗口内。
- [ ] **谱面变换的菜单与右键菜单入口**（2026-08-29 落地，走查在其之前，未覆盖）。
- [x] **侧边栏书签折叠**（2026-08-30 所有者验收通过）。
- [ ] **关闭确认**：脏文档关窗 → 保存 / 放弃 / 取消三条路径；取消后窗口与所有面板是否完好
      （破坏性清理必须只在确认之后发生）；Escape 与标题栏关闭按钮是否都等于取消。
- [ ] **难度标签拖动排序**：两个已打开难度标签拖到彼此上方是否交换顺序；当前编辑难度不切换，
      元数据标签同样可参与，关闭或重开文档后不应把排序写入谱面。
- [ ] **导出区间选择器**：范围页验证端点只移动自身、选中区间整体平移且时长不变；最小区间为
      `min(5 s, 全谱时长)`，短区间的端点和主体仍可明确命中。确认悬停／拖动只显示一个时间戳、不出现会换行的
      「开始／结束」标签；两类拖动都让右侧预览暂停并经同一 scrub 通道定位，播放头只读，数值输入与拖动双向同步。
- [ ] **打开文件的未保存提示**：三条路径 + 未命名文档选「保存」是否转到另存为。
- [ ] **音频拖放建谱**：预览确认 → 未保存决策 → 结果提示（失败 / 单个成功后询问切换 / 部分失败）。
- [ ] **删除难度只弹一次**（此前会连弹两个，第二个是 Widgets）。
- [ ] **保存失败 / 启动目标缺失**的提示是否为应用自己的弹窗。
- [x] **导出片头播放**（2026-08-30 所有者验收通过）：从 -5s 起播时进度条跟走、按钮显示播放中。
- [x] **导出页试听不再卡住**（2026-08-30 所有者验收通过）：进入导出页后在顶部切难度再播放。
- [ ] **Esc 不再输入字符**：编辑器里按 Esc 应什么都不发生；补全弹窗开着时关闭补全，
      查找栏开着时关闭查找栏。
- [ ] **跨难度撤销互不干扰**：在 A 改、切到 B、Ctrl+Z 只影响 B 自己的历史；切回 A 撤销仍在。
- [ ] **脏点留在真正改过的难度上**：改 Expert 切到 Master，脏点不应跟着走。
- [ ] **关标签的三选一**：改过的难度关标签应询问；「放弃」只还原它一个、其他难度不动；
      保存失败或取消另存为时标签应保留。
- [ ] **Save 只写当前难度**：改两个难度按 Ctrl+S，文件里应只更新当前那个；
      关窗时应逐个切到未保存难度询问，取消应真的不关。
- [ ] **未命名文档保存**：新建后不保存直接关标签 / 关窗选「保存」应弹出另存为；
      取消选择应保留标签 / 不关窗。
- [ ] **打开最近 / 恢复备份**：标签是否可读（文件夹名 / 时间戳），tooltip 是否给出完整路径；
      列表为空时只有一行禁用提示，且**上方没有空白行**。
- [ ] **新建**：选一个 `song.mp3` → 同目录出现 `track.mp3` 与 `maidata.txt` 并自动打开；
      选一个本来就叫 `track.mp3` 的 → 不产生多余副本；对已有谱面的目录再来一次 → 应询问覆盖。
- [ ] **新建 / 打开最近 / 恢复备份 / 关闭文档**在有未保存改动时是否先询问。
- [ ] **自动保存恢复实时**：编辑后静置约 2 秒，`恢复备份` 里 `latest` 的时间戳应更新
      （不必等 2 分钟，也不必关闭应用）；历史快照仍是 2 分钟一档，属预期。

### 7.-2 自动保存的**每次编辑安全网**在 v2 从未运行（2026-08-30 所有者报告，已复核并修复）

**报告**：改动后静置一小段时间没有产生备份，关闭后才产生；与 v1 的体感有差距。

**复核结论：报告属实，而且比"备份不实时"更严重。**

自动保存有**两条**写入路径，v2 只有一条在跑：

| | 触发 | 间隔 | v2 |
|---|---|---|---|
| `autosaveTimer_` | `updateDirtyState()` 在文档变脏时启动 | **2 分钟** | ✅ 在跑（v2 每次提交都经 `applyCommittedQmlDocument` → `updateDirtyState()`） |
| `autosaveIdleTimer_` | **`markCurrentFieldDirty()`** | **2 秒**（防抖） | ❌ **从不触发** |

`markCurrentFieldDirty()` 的调用方**全部**是隐藏 v1 `QLineEdit` 的 `textChanged`
（`artistEdit_` / `designerEdit_` / `difficultyLevelEdit_` / `firstEdit_` /
`difficultyDesignerEdit_`，见 `FrameBootstrap.cpp:1428-1450`）加两处直接调用。
v2 的编辑器是 QML，**没有任何东西往那些隐藏输入框里打字**——所以这条路在 v2 上是死的。

于是观察到的现象完全对上：编辑后 2 分钟内什么都不写；关闭时 `requestLeaveDocument` 里的
`runAutosaveCheck(false)` 补写一次 `latest.bak`，所以"关掉才出现备份"。

**更严重的一半**：`markCurrentFieldDirty()` 里还有崩溃恢复的快照投递——
`miacode::crash_recovery::updateSnapshot()`，全树**只有这一个调用方**。
所以 v2 下**异常退出不会留下任何恢复内容**：那条注释里写的"每次编辑的安全网"在 v2 上不存在。
这不是体验差距，是数据丢失面。

**更正此前的记录**：2026-08-30 我曾把这条记为「自动保存本身在跑，缺的只是手动恢复入口」。
那句话只对了一半——2 分钟的例行快照在跑，**每次编辑的安全网不在**。

**溯源**：这条不是"v2 从来没接"，是**接过、然后随着被删的部件一起走了**。
`ad66ac39`（删除隐藏谱面编辑器）移除的代码里有：

```cpp
QTimer::singleShot(0, this, [this]() {
    if (!suppressTextDirtyTracking_) {
        markCurrentFieldDirty();
    }
});
```

那是隐藏编辑器 `textChanged` 的处理器，也就是这张安全网**唯一**的驱动方。
删掉编辑器时它一并消失，QML 侧没有接替。

**已修（2026-08-30）**：把 `markCurrentFieldDirty()` 里的两件事抽成
`noteDocumentEditedForAutosave()`——重启 2 秒防抖、投递崩溃快照——
`markCurrentFieldDirty()` 调它（所以只有**一份**实现，v1 与 v2 两条路都走它），
v2 侧由 `applyCommittedQmlDocument()` 在 **`sourceChanged && dirty`** 时调用。
打开与保存也经过那里，但都不是编辑：前者的源码相对刚载入的内容没变，两者都让文档回到干净。
`updateDirtyState()` 仍排在前面，因为启动例行计时器是它的活。
漂移守卫：`qml_document_lifecycle_contract_spec`。

### 7.3 二阶段五项桌面验收已因同步链重写而失效

- 2026-08-24 记为通过的五项（`ca943a82` 暂停跟随装饰、`2a27630d` 书签/右键菜单、`75c89635` 命令修饰键、`f4251ca0` IME 提交递归、`f451ae09` Command+点击 seek，以及 `5ee23d38` 窄窗口 completion popup）针对的是旧跟随/导航链。
- `117a76a1` 把整条链换成值对象 + 队列投递，并重写了 caret 显隐、系统周期闪烁与播放中暂停语义；上游 merge 提交自述「Native GUI acceptance remains pending」。
- **已复核（2026-08-29，macOS，见 §7.-1）**：五项与新语义随本轮走查一并判定通过——普通点击只移动 caret 不居中；Ctrl/Command+点击先暂停再 seek 并居中 playhead；播放中指针按下立即暂停；播放期普通 caret 隐藏、跟随光标显示，暂停后由焦点恢复；caret 闪烁跟随 `Application.styleHints.cursorFlashTime`。Windows 侧仍未验证。

### 7.4 本次 UI 重构无任何 GUI 验收记录

`04401157` 合入的这批改动尚未被任何人在原生桌面上看过：

- 底栏（时间轴）UI 重构、`TimelineZoomMenu` / `TimelineBrightnessMenu`
- `PreviewRenderModeMenu`（sticky popup，非 `Menu`）
- 时间轴高度随底栏拖拽联动、工作区面板互换
- macOS 自定义菜单栏位置修正、响应式预览尺寸修正
- 新增共享控件 `AppCheckBox`、`AppDropDownButton`

**已复核（2026-08-29，macOS，见 §7.-1）**。窄窗口 / 1280×720 / 浅深色 / 中英文 / Large system font 的逐项组合未单独留记录，若后续出现相关报告按新问题处理。

### 7.5 底栏高度配置键变更，无迁移

`QmlUiSettings` 的 `ui/bottomPanelHeight`（像素）已换成 `ui/bottomPanelHeightRatio`（比例，默认 0.35，`QmlUiSettings.cpp:33-36`），**没有写迁移**。老用户升级后底栏高度会回到默认值。
**待复核**：确认这是有意为之；若否，补一次性迁移。

### 7.6 撤销栈 dirty 联动（已修，待 GUI 复核）

dirty 真相已迁到 `ChartWorkspace` 的完整文档 save point，`Ctrl+Z` / `Ctrl+Y` 回到已打开或已保存内容会自动回 clean。
**已复核（2026-08-29，macOS，见 §7.-1）**。

### 7.7 切换文档后 PV 异常（延后）

多次复现失败，需求延后。埋点保留在树上（`editor/document_replaced`、`editor/document_shown`）。
**待复核**：仅在再次复现时重启排查，不预先投入。

### 7.8 播放期高位内存平台（延后）

现有证据是"跃升后稳定"（private memory 约 957 MB 平台，第二段播放 1,051–1,064 MB），不是持续泄漏。
**待复核**：阶段 2 迁移 Preview/Timeline **之前**先做分层取证（QtAVPlayer/D3D11VA 帧池、QML/Qt Quick、私有堆），不得先验归因于 preview texture cache。

### 7.9 手工回归清单（2026-08-29 macOS 走查判定通过，见 §7.-1）

- [x] 脏文档、播放中、导出任务运行时的关窗流程与 v1 一致。
- [x] 设备热插拔暂停后，下一次播放走 cold Prepare。
- [x] play / pause / seek 按 completion 更新界面状态。
- [x] EraseByArea、烟花时长、BGM 过轨静音行为正确。
- [x] 音频拖放建谱与 ChartDrop 覆盖层（含 cancel）行为正确。
- [x] QML 导出片头音文件与音量：原生试听 + 成片确认。
- [x] `d534b393` 的 bookmark / touch input 改动 GUI 回归。
- [x] root 拖放、播放中关闭。
- [ ] **音视频处理工具（未验收）**：采样率 / 提取音频 / 前置空白 / 音量归一化四条 ffmpeg 流程，
      以及 PV 批量压制队列的进度、取消与失败呈现。这是唯一还没被走查覆盖的 v2 页面。

### 7.10 已知且接受、不修

- 低窗口高度下底栏拖不到标称 65%：`MainSplitView.qml` 的 `editorHost` 有 `SplitView.minimumHeight: 180`，`Main.qml` 窗口最低高度 480。标称范围仍是 20%–65%，这是当前壳层约束，不要去改 180，除非产品明确要求取消该像素下限。
- Windows 侧整体从未验证。
- 诊断开关 `MIACODE_SKIP_CRASH_HANDLER`：配合 Windows LocalDumps 时跳过 `crash_recovery::install()`，让访问冲突保持 `0xC0000005` 并生成转储。

## 8. 关键路径

| 角色 | 路径 |
|---|---|
| v2 启动 | `src/app/qml_ui/QmlUiBootstrap.*` |
| 契约根 | `src/app/qml_ui/QmlApplicationContext.*` |
| 文档所有者 | `src/app/v2/ChartWorkspace.*`、`ChartWorkspaceFileService.*` |
| 分析快照 | `src/app/v2/AnalysisService.*` → `QmlAnalysisModel.*` / `QmlAnalysisProjection.*` |
| 同步控制器 | `src/app/v2/EditorSyncController.*` |
| 文档桥 | `src/app/qml_ui/QmlDocumentModel.*`、`QmlDocumentProjection.*`、`sections/document/MainWindow.DocumentBridge.cpp` |
| 预览桥 | `src/app/qml_ui/QmlPreviewModel.*` |
| 编辑器 | `src/app/qml_ui/QmlEditorController.*`、`QmlEditorInputBridge.*`、`editor/SourceEditor.qml`、`SimaiSyntaxHighlighter.*` |
| 快捷键 | `src/app/qml_ui/QmlShortcutModel.*` ← `src/app/ui/ShortcutRegistry.*` |
| 视频导出 | `src/app/qml_ui/export/QmlExportSession.*`、`export/ExportVideoPage.qml` |
| 页面宿主（待删） | `src/app/qml_ui/QmlEditorPageHost.*`（不再收养 `QWidget`，但切页仍依赖隐藏 `DocumentSection` 的 Widgets 状态；见 §0） |
| 主壳 | `src/app/qml_ui/Main.qml`、`layout/MainView.qml` |
| 设置页 | `preview/AudioSettingsDialog.qml` + `QmlAudioSettingsModel.*`、`preview/PreviewSettingsDialog.qml` + `QmlPreviewSettingsModel.*`、`preferences/PreferencesDialog.qml` + `QmlPreferencesModel.*` |
| 表单控件 | `components/LabeledCombo.qml`、`LabeledSlider.qml`、`EditableValue.qml`、`DialogDrag.qml` |
| 开发索引 | `.agents/skills/miacode-dev-guide/references/feature-index.md` |

## 9. 更新规则

1. 条目状态变化时**同时**更新本文与架构文档第 8 节的阶段表。
2. 架构、入口或跨模块契约变化时，同步更新仓库指南（`feature-index.md` / `cross-chain-linkage.md`）。
3. 原生桌面验收结果写进第 7 章对应条目，注明平台与日期；构建与 CTest 不能替代。
4. 不得重新引入 v1 shell；恢复被裁剪功能须单独作产品决策。
5. 新代码不得引入 `Qt6::Widgets` 类型——那是本清单唯一的终点条件。
