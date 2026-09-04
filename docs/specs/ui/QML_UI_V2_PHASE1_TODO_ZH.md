# QML UI v2 补完清单

> 文件名沿用历史（多处文档与仓库指南指向它），内容已重写为**当前唯一在推进的工作清单**。
> 目标架构与阶段定义见 [QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md](QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md)；
> 本文只登记「还差什么、完成标志是什么、什么待复核」。
>
> **基线：2026-09-02 工作树**。本文所有状态以远程合并后的当前源码和目标测试实测为准。
> 当前基线为合并提交 `4416596d`（远程 `origin/feature/qml-ui` 已同步）；其中包含
> `2aa9db83` 的 `MainWindow` 宿主迁移与 `src/app/mainwindow/` 删除。
> 上一版基线是 `117a76a1`（2026-08-29）；阶段 1、阶段 2 与三个「暂未更新支持」入口
> 都在这两个基线之间落地，第 2 章的实测表已按新基线整体重测。
>
> **目标口径补充（2026-08-31）**：本清单的“轻依赖”不是追求部署包中 DLL 数量的绝对最少，
> 而是让 UI 进程不再携带无主的 Widgets / v1 宿主依赖，并让 Multimedia、Network、OpenGL、
> SVG、FFmpeg、BASS 等依赖都能说明其所属功能、加载时机和验证方式。Qt Quick/QML 仍由 C++
> `QGuiApplication` + `QQmlApplicationEngine` 承载；QML 不能替代应用宿主。

## 0. 2026-08-30 反馈闭环与 2026-09-02 接线复核

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
- [x] **隐藏 v1/Widgets 接线复核（2026-09-02）**：`2aa9db83` 已将启动装配改为
      `QmlUiBootstrap` 构造 `ApplicationServices` + `Session`，`QmlApplicationContext` 只接收
      `ApplicationServices&`，`QmlEditorPageHost` 经 `EditorPageRouter` 路由，且
      `src/app/mainwindow/` 已删除。root 拖放仍由 `QmlChartDropBridge`（单一 QWindow event
      filter）+ `Main.qml` 提示层承载，不再创建 `ChartDropOverlay(QWidget)`。
- [ ] **运行时 Widgets 残留仍待清理**：`Session` / `RuntimeContext::Ui` / `RuntimeContext::State` 仍携带旧宿主状态，
      `src/app/main.cpp` 已切换为 `QGuiApplication`，但 CMake 仍链接 `Qt6::Widgets`；隐藏的
      `DocumentSessionHost` 还保留 native fallback 的旧切页方法。它们是阶段 4 的后续拆除项，
      不能因为 `mainwindow/` 已删除就宣称 Widgets 已归零。

      已核对的非误报项：ZIP 打包使用 `UiRequestService`，偏好设置只转发
      `preferencesRequested` 给 QML，音视频工具也经 QML 请求边界；这些不是 v1 UI 回接。
      封面导出已于 2026-08-31 建立 v2 所有者并脱离此清单；后续拆除不得把 v1 控件重新接回 QML。

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

| 项 | 实测（`4416596d`，2026-09-02） | 位置 |
|---|---|---|
| 链接声明 | `COMPONENTS … Widgets …` + `Qt6::Widgets` | `CMakeLists.txt:101`、`:844` |
| 应用对象 | `QGuiApplication`（2026-09-04 起；CLI 与 GUI 共用） | `src/app/main.cpp:412` |
| 隐藏 `MainWindow` | **已删除（0 文件）**；`Session`/runtime hosts 已接管装配 | `src/app/runtime/`、`src/app/runtime/SessionBootstrap.cpp` |
| Preview/Timeline 当前宿主 | `PreviewHost` / `TimelineHost` 分别占据两个 surface 槽位；兼容投影由 `PlaybackSurfaceAdapters` 转到同一个 `PlaybackCoordinator`，实现体仍约 8,975 行并待 4.9 清理 | `src/app/runtime/{preview,timeline,playback}/` |
| 旧宿主状态 | 原 `HostUi` / `HostState` 已外置为 `RuntimeContext::Ui` / `RuntimeContext::State`，目前仍是被多个宿主借用的共享迁移存储 | `src/app/runtime/RuntimeContext.h`、`Session.*` |
| ~~隐藏 `PlainCodeEditor`~~ | **已删除（2026-08-30）**。树上只剩两条历史注释；扩展相关校验已迁移到归档 API registry 与离线文档 | — |
| ~~`QuickShellController`~~ | **已退役（2026-08-30）**。`src/app/quick_shell/` 只剩 **246 行**预览合成表面管道（`QuickShellPreviewCompositeSurface.*` + 三个策略头），与外壳控制器无关 | — |
| QML 仍消费的 controller 属性/方法 | **0 个**（`shellController` 在 `*.qml` 里零命中） | — |
| Widget UI 代码量 | `cover_export` 现为 **2,640 行 / 11 文件**的纯布局、持久化、谱面帧与 Quick 离屏渲染；**0 个 Widget UI 文件**（其中 `CoverCompositionPersistenceGuard.{h,cpp}` 75 行已不在 app target 里，只剩 `cover_layout_model_spec` 还编译它——是删是接需要定夺，见 §3.C）（2026-08-31 删除原 CoverStudio 工作台）<br>*（此前已删除：export_page 738、BatchExportPanel 组 947、MainWindow 嵌入面板机制 479、`VideoExportDialog` 组 4,691、`NetBatch*Dialog` 1,479、`PvBatchCompressionDialog` 393、`PlainCodeEditor` 组 2,272、`QuickShellController` 组 993）* | `src/tools/cover_export` |

### v2 自身新代码里的 Widgets 泄漏（优先清）

| 位置 | 内容 |
|---|---|
| ~~`src/app/qml_ui/export/QmlExportSession.cpp`~~ | ~~`QFileDialog` ×5、`QMessageBox` ×5~~ —— **已清零（2026-08-29）**。`src/app/qml_ui` 全目录现已无 Widgets 对话框 |
| `src/app/qml_ui/QmlEditorPageHost.*` | 已不再收养 `QWidget`，页面路由改走 `EditorPageRouter`；runtime 的 `DocumentSessionHost` 仍保留 native fallback 的旧切页方法，不能记作 Widgets 已归零（详见 §0） |
| `src/app/qml_ui/drop/QmlChartDropBridge.*` | 仅负责 root `QWindow` 拖放事件、路径筛选、请求代次和 busy/late-callback 保护；视觉提示由 `Main.qml` 承载 |

`MainView.qml` 的对话框已是 `QtQuick.Dialogs`（非 Widgets），这条不用改。

### 2.1 轻依赖目标与当前事实（2026-09-02）

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
>
> **（2026-09-04 更正）**「`src/app/ui/` 中与其共用的辅助件」这句话会误导：该目录 18 个文件里
> `UiText` / `AppBackgroundSettings` / `ThemeVariantResolver` / `ShortcutRegistry` 都有 QML 路径
> 生产消费者，**不是待删的 Widgets 辅助件**。真正的阻碍是 `DialogLocalization.h`（纯 Widgets）
> 以及 `UiNativeWindowTheme` 的 `QWidget` 重载那一侧；`UiTheme` 的旧样式表函数族已在
> 2026-09-05 后续批次删除。
> 逐项证据与执行方案见 §「逐步移除 Widgets：进度与第 2 步交接（2026-09-04）」。

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
      **当时留在树上的 `showMessageBox`**：`onNewFile` / `onOpenFile` / `openRecentFile` /
      `switchToWelcomePage` / 扩展 `showMessage` / 删除难度的 v1 与扩展路径——其中前三者当时
      只挂在隐藏 `MainWindow` 自己的 File 菜单 `QAction` 上，QML 外壳没有任何入口能走到。
      2026-09-05 已删除旧新建、打开、最近文件的 Session 转发和 native 文件对话框入口；
      `switchToWelcomePage` 仍保留为无难度文档初始化的内部路径，及其必要的同步 native fallback。
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


### 阶段 1–3.5（已完成，精简归档）

> 详细工程记录见 `src/app/runtime/ASSEMBLY.md` 与各阶段提交说明；此处只留成果与仍然有效的结论。

- **阶段 1 — 文档域单一所有者**：文档事务、save-point dirty、revision 标记的分析快照全部脱离窗口，
  改由独立的文档域服务持有。隐藏的 Widgets 文本编辑器与第二份文档副本已删除；过程中修好了两个
  原本就坏的功能（扩展编辑 API 与谱面变换命令都作用在一个从未与 QML 选区同步的隐藏光标上）。
- **阶段 2 — 退役旧外壳控制器**：25 个 QML 消费点迁到三个模型对象；**轮询彻底消失**，离散状态与
  播放头改为推送。旧控制器与其抽象基类已删除。
- **阶段 3 — 导出服务与 Widget 对话框归零**：选文件 / 消息 / 进度统一到两条无 Widgets 的共享边界，
  四条导出与媒体流程共用同一个进度浮层；封面导出重建为 QML 页面；一批 Widgets 对话框与面板组删除。
  守卫用「只链核心库」的方式保证 Widgets 对话框放不回来——链接失败，而不是靠字符串扫描。
- **阶段 3.5 — 应用宿主脱钩与依赖分层**：装配对象成为七个应用服务的**唯一所有者**（此前分属两个
  UI 对象，「文档域归谁」取决于问谁）。QML 应用上下文不再持有窗口引用：九个域改由窄接口承接，
  推送信号经通知中继转发，**窗口方法面 120 → 9、私有成员直读 17 → 0、QML 类型的 friend 授权 5 → 0**。
  同期建立依赖 allowlist 与其守卫，把网络模块移出产品运行时。

**阶段 1–3.5 留下的、仍然有效的三条注意事项**：

1. **增量时间轴解析实际从未运行**。它唯一的驱动方是隐藏编辑器的变更信号，而该信号被写入抑制挡住；
   隐藏编辑器删除后这条路彻底断了，v2 的每次编辑都是全量重建。要恢复得改由工作区提交驱动
   （它知道确切编辑范围）。**所有者已决定继续延后。**
2. **文档读取有一个延迟同步陷阱**。窗口时代那份文档副本与工作区之间是延迟同步的，后端替换文档
   （启动 / 拖放 / 原生打开 / 崩溃恢复）之后存在一个窗口期。导出快照必须读它所依据的那一份副本，
   不能顺手改读工作区，否则会列出旧的难度列表。
3. **网络框架仍会被加载**——它是 QML 模块的传递依赖。阶段 3.5 改变的是「产品是否自己使用网络」，
   不是部署包里少一个框架。不允许把这条记成依赖数量减少。

### 阶段 4 —— 删除 `MainWindow`，收口为 Qt Quick/QML 宿主

- [x] **阶段 4 第一至三批 Widgets 拆除（2026-09-01）**：删除隐藏侧栏忙碌指示器及其调用点、
      隐藏导出设置用的字体 widget 适配器、已无生产调用方的自定义布局件与可编辑数值标签。
      QML 侧的字体选择与双击输入路径不变。守卫会拦住这些残留回归。
- [x] **阶段 4 第四批 / 媒体工具所有权迁移（2026-09-01）**：六个媒体接口改由 runtime 的媒体宿主
      接管并注册到装配对象槽位，旧窗口 sections 迁出，媒体任务不再存在平行 owner。
- [x] **阶段 4 第五批 / 窗口实现迁入 runtime（2026-09-02）**：窗口实现全部迁入 `src/app/runtime/`，
      旧 `src/app/mainwindow/` 目录删除；`Session` 改为 QObject 装配壳，入口、CLI 与延迟沙盒
      统一走新的 runtime 装配，QML 应用上下文不再持有窗口引用。
- [ ] **PlaybackHost 二次拆分（阶段 4.5–4.9，当前主线）**：目标仍是「一个时间域、一个播放权威、
      两个独立投影」；4.8 已完成协调器契约收缩和旧类型重命名，4.9a 已完成运行时上下文边界外置，
      后续继续清理按宿主共享状态与 Widgets 残留，之后才算本主线完成。
- [ ] **runtime Widgets 残留清理**：拆掉 `RuntimeContext::Ui` / `RuntimeContext::State` 的共享状态袋（原 `HostUi` / `HostState`）及 native fallback
      依赖；宿主与 CLI entry 已先切换为 `QGuiApplication`，唯一 `QQmlApplicationEngine` 由
      `QmlUiBootstrap` 持有。完成源集迁移后，再从 CMake、生产 target 和全部 fallback 去除
      `Qt6::Widgets` / `QWidget` / `QApplication` API。
- [ ] **4.5–4.9 后续主线（非 GUI 验收）**：

      > 统一边界原则：**一个时间域、一个播放权威、两个独立投影**。Preview 与 Timeline 可以
      > 分别拥有资源和视图状态，但不能各自维护播放头、计时器或 seek 真相。GUI 验收另行处理；
      > 本主线先以契约、单元测试、装配检查和跨模块回归推进。

      1. **4.5–4.8（已完成，精简归档）**。四步依次立起了播放契约与三个宿主：
         窄的播放控制接口 + 只读播放快照（携带 session 代次、文档 revision、播放序号、
         canonical chart time、走带状态与倍速，旧回调在边界丢弃）；`TimelineHost` 接管时间线
         投影并设命令闸门统一校验 revision / 代次 / 写入顺序；`PreviewHost` 接管预览运行时、
         舞台媒体与音频，只通过窄的时钟与走带端口与协调器对接；最后旧 `PlaybackHost` 收缩并
         重命名为 `PlaybackCoordinator`，直接实现三个窄契约，两个无状态 adapter 承接旧槽位。
         **详细工程记录见 `src/app/runtime/ASSEMBLY.md`。**

      2. **4.9 拆掉共享状态袋、让 Session 只做装配（进行中，当前主线）**

         **4.9a–4.9c 已完成**：共享 `Ui` / `State` 类型外置到独立的运行时上下文头文件，宿主构造
         签名改用显式上下文类型；时间线存储切进独立的域记录，兼容引用留在原处、拷贝被禁用，
         并新增一个**独立编译单元**用编译期断言逐字段钉住「借用方不得拥有」；删除全部零引用死存储
         及其连带的只有声明没有实现的方法簇。

         **4.9d 切断协调器对 `Session&` 的依赖（进行中）**——本主线的关键路径。
         起点：构造签名要 `Session&`，实现体有 **204 处 `session_.` 调用、73 个不同成员**，
         因此没有任何测试能构造协调器，完成门槛第 1、3 项为空，根因是同一个。

         | 步 | 内容 | `session_.` |
         |---|---|---|
         | 1 | 自指绕路 / 死代码 / 已持有同一存储 | 204 → 169 |
         | 2 | canonical 时钟计算搬进协调器 | 169 → 155 |
         | 3 | Widgets 补妆函数体搬进协调器 | 155 → 125 |
         | 4a | 薄转发（只碰已持有的引用），不需要端口 | 125 → **79** |
         | 4b | 窄端口 + 信号路径 | 79 → 0（**下一步**） |
         | 5 | 构造签名去掉 `Session&` | — |

         **前四步没有引入任何新抽象**——全是删冗余、搬函数体、直读协调器已持有的同一份引用。
         关键杠杆是协调器与 `Session` 持有的是**同一个**上下文引用，所以只要函数体不越出
         这份存储，绕道就是纯冗余。

         **4b 的关键前提（已查证）**：三个候选宿主端口**自己的构造签名都要 `Session&`**，
         所以直接注入它们并不能达成完成判据——测试里仍需先造 `Session`，只是把依赖藏到下一层，
         **`session_.` 归零而目标落空**。必须用窄抽象接口。

         **4b 的处置方案**（剩余 79 处按功能分组）：

         - **30 处不需要新接口**：文档工作区读取与编辑器跟随同步的两个协作者都是已验证可独立
           构造的应用层类，直接注入即可；三处信号改走已有的通知中继。
         - **46 处收敛为四个窄接口**：偏好持久化、舞台媒体、文档状态、校验刷新。约 19 个虚方法，
           实现类就是现有的四个宿主。**刻意不做成单个聚合接口**——那只是给 `Session&` 换名字，
           接口镜像神对象不是解耦，而且会丢掉「哪个宿主拥有什么」这个信息。
         - **7 处判定依赖运行时分支**，热路径是薄转发、首次构造分支跨宿主，静态读不出走哪条，
           需单独定夺，不塞进任何接口凑数。
         - **3 处零散**（延迟沙盒指针、一个无归属的 Session 自有字段）需单独定归属。

         **顺序上的硬性要求**：构造签名一去掉 `Session&`，**立刻写 fake-clock 那个 spec**，
         不要继续清理。那个测试是「端口够不够用」的唯一证明；四个接口都造完才发现构造不出来，
         就是白造。

         **4.9d 的诚实边界**：它让协调器可测、依赖显式且窄，但**不会让运行时摆脱 `Session`**——
         实现这些接口的宿主自己仍要 `Session&`，生产路径上 `Session` 照旧装配一切。
         宿主本身脱钩是 4.9e 往后的事。

      3. **4.9e / 4.9f（未开始）**：canonical 播放状态收进协调器私有成员、跨域读者改读播放快照；
         补齐下列完成门槛测试，并替换掉两处「文本扫描冒充生命周期保证」的检查。
         **4.9e 的一个已知真问题**：时钟契约建好了却被所有潜在消费者绕开——外部调用者要的是
         实时外推值，而契约给的是按 tick 写入的采样值，两者播放中不是同一个数，
         所以不能机械改成走契约，契约本身可能需要同时暴露两种读法。

      **非 GUI 完成门槛**：补齐协调器 fake-clock 的 play/pause/resume/stop/seek/scrub/rate 测试，
      `TimelineCommandGate` 的 revision / sequence / drag-follow 顺序测试，三宿主装配与生命周期测试，
      以及 parser → timeline → preview → export 的 revision / chart-time 对齐回归；并用依赖检查确认
      `TimelineHost` 不含 Preview 实现、`PreviewHost` 不含 Timeline 实现、协调器不含 QML/QSG/media UI。

- [ ] `src/app/ui/` 的 widget 辅助件（`UiComponents` 等）
      随之删除或改为非 Widget 服务；同时替换
      `QStyleFactory`、`topLevelWidgets`、native Widget effect 等旧语义。
- [ ] `main.cpp` 最后换为 `QGuiApplication` + `QQmlApplicationEngine`；不得机械替换后继续
      依赖 `QApplication` API。
- [ ] `CMakeLists.txt` 的 Qt components 与 `MiaCode` target 去掉 `Widgets`；生产代码、全部
      构建 target 和隐藏 fallback 均不得重新引入 QWidget 类型。
- [ ] `ShaderTools` 若仅为构建期工具，不计入运行时依赖；`Qt6::Svg`、`Qt6::OpenGL`、
      `Qt6::Network`、`Qt6::MultimediaQuickPrivate` 按实际功能和平台条件单独验收。
- **Architecture Complete 完成标志**：隐藏 `MainWindow` / native QWidget 拖放 overlay 已清除，
      但当前仍未完成，因为 `Qt6::Widgets`、`QApplication`、`RuntimeContext::Ui` / `RuntimeContext::State` 共享袋与协调器内部
      的旧 runtime 依赖仍在。必须完成 4.5–4.9、移除 Widgets 依赖，并通过 Release 构建、全量 CTest、
      QML import 部署扫描及跨模块时间域回归；macOS / Windows 依赖记录与功能 parity 另行处理，
      GUI 结果不作为本阶段的工作项。

### 测试基线的一个噪声源（2026-09-03 实测）

`qml_export_video_page_spec` **是 flaky 的，不是稳定红**：连跑三次，两次通过、一次失败。
失败时的报错是 QML 资源与字体加载（图标 SVG 打不开、`Theme.qml` 的 `Qt.font()` 收到无效子属性），
属加载时序竞争，与被测逻辑无关。

**影响**：全量 CTest 的「105 项 104 通过、唯一红 `qtavplayer_platform_spec`」这条基线**带噪声**——
偶尔会出现第二个红。判断某轮改动是否引入回归时，遇到这一项变红应先重跑确认，
不要直接当成回归去追。稳定红只有 `qtavplayer_platform_spec` 一项。

**未处理**：定位这个竞争需要 QML 加载路径的排查，不属当前架构主线范围。

### 阶段 4.9f 结论与一处虚假信心（2026-09-04）

**门槛第 1 项（协调器 fake-clock 七种转换）无法靠扩展纯策略模块满足。**
`src/audio/PreviewAudioPlaybackFlowPolicy.h` 已纯函数化的只是 play/resume/pause/播放中 seek 的
**完成协议**子决策（其 spec 确实已用字面秒数当假时钟）。七个命令入口的**分支选择**统一读
`state_.previewSfxRuntime_`——具体类 `QtPreviewSfxRuntime*`，**不在协调器四个抽象端口之后**，
伪造端口也换不掉它；stop / scrub / rate 三种全带副作用，策略模块无对应结构。
两条出路：给 `previewSfxRuntime_` 也开第五个窄端口，或接受「完整转换测试＝链接协调器」
（后者卡在链接闭包 246 文件不收敛，见 4.9d 记录）。

**一处虚假信心（已核实，未修）**：`beginManualPause` / `beginDeviceChangePause` /
`supersedePendingPauseForPlay` 三个策略函数**在 `src/` 生产代码里零调用**，
全部调用点都在 `PreviewAudioPlaybackFlowPolicySpec.cpp` 与 `PreviewAudioDeviceChangePolicySpec.cpp`。
生产侧在 `playback/PlaybackState.cpp:827-833` 手写了另一版：
**策略函数设 6 个字段（多出 `currentGeneration`、`visualSecond`），生产只设 4 个。**
所以不是「等价手写」，是两份不完全相同的实现，而**被测的是没人用的那一份**。

后果：读到「有暂停策略 spec」的人会以为暂停身份/代次语义有覆盖，实际生产路径一行未被验证。
**这比缺测试更糟**——缺测试至少不会给人信心。

未修的原因：修它要把生产的暂停提交路径改成构造 `PauseRequest` → 调策略 → 写回 `state_`，
且要先定夺那两个多出字段该不该设。暂停路径是热代码，本环境无 GUI 验证能力。
**未验证**：生产是否在同一路径的别处设了那两个字段（若设了则只是形状不同，若没设则行为确实分叉）。

### 逐步移除 Widgets：进度与第 2 步交接（2026-09-04）

所有者 2026-09-04 指令：「4.9-f 先搁置。逐步移除 Widgets」。本节是该主线的**活动交接页**，
接手者从这里读起。

#### 五步计划与当前位置

| 步 | 内容 | 状态 |
|---|---|---|
| 1 | 清 `RuntimeContext::Ui` 死指针 | 已完成 `46350fa9` |
| — | 修自动保存吞元数据（顺手发现的数据丢失） | 已完成 `8e96fb33` |
| — | 移除所有者裁决为「移除」的缺陷 3 / 4 / 6 | 已完成 `825a63b6` |
| **2** | **剪 `src/app/ui/` 的死表面** | **已完成（2026-09-04）** |
| 3 | 齿轮图标渲染搬进 QML，摘掉 `Qt6::Svg` | 已完成（2026-09-04） |
| 4 | `main.cpp` → `QGuiApplication` + `QQmlApplicationEngine` | **宿主切换与 GUI 初步验收通过（2026-09-04）**：CLI entry 已切换；唯一 engine 仍由 `QmlUiBootstrap` 持有；runtime backend 的 Widgets 剥离转入第 5 步继续 |
| 5 | CMake 去掉 `Qt6::Widgets` / `Qt6::Svg`，移入 `dependency_allowlist_spec` 禁止表 | **进行中**：产品 `Qt6::Svg` 已移除；旧原生 shell 三组翻译单元及旧 C++ 全屏播放实现已移出产品源集；`Qt6::Widgets` 等待剩余 runtime 源集迁移后移除 |
| — | 补功能：缺陷 2（方向键 seek）、缺陷 5（全屏控件） | 未开始；#5 会产生新 UI，需所有者定夺 |

累计：`Ui` 字段 104 → 34；三轮净删约 1530 行。

第 4 步单列出来是因为它**没有自动化覆盖**：失败形态是「测试全绿但应用起不来」，
本环境无 GUI 观察能力（见 §7.4），必须由所有者启动确认。

#### 第 3–5 步执行结果（2026-09-04；截至本轮）

- **第 3 步已完成**：删除 `Shared` 中无调用的 `makeSettingsGearIcon` 与 `QSvgRenderer`，
  保留 `src/app/qml_ui/resources/icons/settings.svg` 及其 QML 消费者；`MiaCode` 的
  `Qt6::Svg` 直链、顶层组件声明和 dev-tool 的冗余直链均已移除，产品 SVG 直接依赖已进入
  `docs/ops/DEPENDENCY_ALLOWLIST.md` 禁止表。这里的验收口径是 C++ 直接依赖；QML SVG
  image plugin 是否随安装包提供仍需打包走查。
- **第 4 步宿主切换已完成，GUI 初步验收通过**：`main.cpp` 与两条 CLI entry 使用 `QGuiApplication`；主程序不再设置
  QWidget Fusion style、QWidget tooltip effect、top-level widget 诊断或 QWidget 全局主题过滤。
  `QmlUiBootstrap` 继续持有并加载唯一的 `QQmlApplicationEngine`，没有在 `main.cpp` 创建第二个
  engine。所有者已启动 GUI 做初步验收，反馈“未发现问题”；这项人工验收不替代后续
  runtime 源集与无 Widgets 构建验证。
- **第 5 步进行中，本轮完成第一批产品源集拆分并继续收口播放 UI**：`CMakeLists.txt` 已将旧原生
  `runtime/shell/Interaction.cpp`、`Runtime.cpp`、`Shell.cpp` 移出 `MiaCode`，保留的
  `ShellHost` 只负责 QML 根窗口生命周期、关闭事务和运行时日志；`SessionLifecycle.cpp`
  承接 Session 析构、预览停机与窗口标题等非可视生命周期。同步移除了 document/editor/
  validation 中对原生 shell 布局、焦点和窗口诊断的调用。`MiaCode` Release 目标已通过编译。
- **第 5 步本轮继续收口播放 UI**：删除纯 QWidget 的
  `runtime/playback/Fullscreen.cpp`，移除 `Session` / `PlaybackCoordinator` 的原生全屏
  转发、控件状态、样式和图标实现；全屏显示与关闭按钮继续由
  `src/app/qml_ui/layout/MainSplitView.qml` 持有。`Qt6::Widgets` 仍保留，因为
  `Session` / document / validation / playback 的其余 runtime backend 尚未完成源集迁移。
  产品目标仍直接链接 `Qt6::Widgets`，且 `Session` / `SessionBootstrapFinalize` / playback /
  document dialog / validation 等剩余翻译单元仍包含 Widgets 类型，因此本轮不把第 5 步宣称为完成。
  本轮 `MiaCode` Release 编译通过；针对性测试 5/5 通过，全量 CTest 为 105/108 通过，
  仅保留既有的 `timeline_model_spec`、`qml_editor_controller_spec`、`qtavplayer_platform_spec`
  三项失败。测试/dev-tool 的 Widgets 依赖暂不属于产品目标验收。
- **第 5 步本轮继续收口旧播放控件边界**：删除已无调用方的 `runtime/Session.cpp`，清理
  `Shared.h/.cpp` 中未被使用的 outline delegate、图标和样式辅助；删除从未实例化的旧
  QWidget 预览滑条同步、tooltip、回调和滑条专属状态。QML 的
  `PreviewTransport.qml` / `ExportRangeSelector.qml` 继续使用通用
  `beginScrub()` / `updateScrub()`，播放头通知改名为明确的
  `PlaybackCoordinator::publishPreviewPlayhead()`，因此没有削弱 QML 播放头推送。
  延迟试听、时间线、媒体加载和导出安装路径均已迁移；`MiaCode` Release 编译通过，针对性
  测试 7/7 通过，全量 CTest 仍为 105/108 通过，失败项与既有基线完全相同。`Qt6::Widgets`
  仍不能移除：验证页仍有 `QSlider`，Session/document/editor/validation 的其余 Widgets
  源集仍未完成迁移。
- **第 5 步继续削薄传递依赖**：`RuntimeContext.h` 不再直接包含 `<QtWidgets>`，共享上下文只对
  尚存的迁移期指针使用前向声明；多个仅使用 Core/Gui 的 runtime 翻译单元同步移除宽泛的
  `<QtWidgets>`。媒体工具的前置空白参数改从 `ChartWorkspace::document().extraFields` 读取，
  不再依赖未构造的隐藏元数据编辑框；播放暂停的旧 QWidget 按钮呈现分支也已移除，QML 的
  `ShellNotifications::presentationChanged` 推送保留。产品 Release 构建及上述 7 项针对性测试
  均通过；Qt Widgets 仍只在确有旧控件实现的 runtime/UI 路径中保留。
- **第 5 步本轮继续收口隐藏编辑器与预览控件镜像**：移除旧预览倍速按钮/菜单、倍速提示
  和停止/暂停按钮指针；QML 继续通过 `PreviewSurface::playbackRateLabel()` 与
  `ShellNotifications::presentationChanged` 获取同一份后端状态。标题、艺术家、谱师和
  Extra 字段不再回写未构造的 `QLineEdit`/`QTextEdit`，字体/行距偏好只更新 QML 所需的
  设置状态，窗口标题与导出标题直接读取 `ChartWorkspace`。同时删除仅服务于这些隐藏编辑器
  的 undo/clean-anchor、文本格式化 helper 与空同步 stub。`MiaCode` Release 编译通过，针对性测试 7/7 通过，
  全量 CTest 仍为 105/108 通过，红项与既有基线一致。
- **第 5 步本轮完成 validation UI 的旧 Widget 投影收口（2026-09-05）**：删除验证与 Muri
  的 `QListWidget` 投影、包装 delegate、重排、激活/右键菜单和底部 `QTabWidget` 容器镜像；
  `RuntimeContext::Ui` 不再持有这些列表/容器指针，`PlaybackValidationPort` 不再暴露列表刷新
  方法。保留校验缓存、错误装饰、QML `QmlAnalysisSnapshot`、Muri 时间线装饰和底部 tab 的
  状态投影；同时删除无生产调用方的旧撞尾阈值 `QDialog` 路径与空书签列表查询。`MiaCode`
  Release 编译通过，针对性测试 7/7 通过，全量 CTest 仍为 105/108 通过，失败项与既有基线
  完全相同。剩余 `Qt6::Widgets` 依赖集中在尚未迁移的 runtime/document/shared/layout 路径，
  因此第 5 步仍未完成。
- **第 5 步本轮继续移除空的 QWidget 布局桥（2026-09-05）**：确认
  `workspaceContentWidget_` 从未被生产构造，删除 `RuntimeContext::Ui` 字段、旧 rehost 刷新
  helper、PlaybackCoordinator / Session 的布局缓存与页面刷新空转发，以及 document 页面切换
  对这些空操作的调用。预览画布比例仍更新同一份状态并刷新 Quick 合成表面，左右面板互换仍
  保留状态和偏好持久化；布局翻译单元不再包含 Widgets 头。`MiaCode` Release 编译通过，
  针对性测试 7/7 通过，全量 CTest 仍为 105/108 通过，失败项与既有基线完全相同。
- **第 5 步本轮继续清理传递依赖（2026-09-05）**：移除 6 个实际不使用 QWidget 类型的
  runtime 翻译单元中的宽泛 `<QtWidgets>`（导出快照/worker、StageMedia、文档变换、播放
  布局与 tick）；仍真实调用文件/消息对话框或 QApplication 弹窗状态查询的路径保持不动。
  `MiaCode` Release 编译通过；此前完整 CTest 的 105/108 基线未改变。
- **第 5 步本轮继续收口 Session 头文件（2026-09-05）**：删除 `Session.h` 中已无成员/签名
  使用的旧 MainWindow 控件前置声明及无用 `QEvent` 头；保留字体 `QAction`、菜单/工具栏、
  导出进程、滚轮事件和 QML 根窗口等仍实际构成边界的类型。Release 重编译通过，针对性
  测试 7/7 通过。
- **第 5 步本轮继续清理原生菜单投影与宽泛头（2026-09-05）**：删除无调用方的最近文件/
  恢复备份 `QMenu` 投影及 Session/Document 转发；runtime 中所有宽泛 `<QtWidgets>` include
  均改为精确头或删除。真实的文件/消息对话框、字体 QAction/工具栏和 QApplication 弹窗状态
  门控保持不变，避免把 native fallback 误判成已迁移。`MiaCode` Release 构建通过，针对性
  测试 7/7 通过；完整 CTest 仍为 105/108 通过，3 个失败项与既有基线完全相同。
- **第 5 步本轮继续清理共享菜单样式转发（2026-09-05）**：删除无调用方的
  `runtime::shared::styleRoundedMenu(QMenu&)` 及其 `QMenu` 依赖；native 未保存对话框所需的
  `centerDialogOnAnchor` 保持不变。
- **第 5 步本轮继续清理死的 native 主题样式（2026-09-05）**：确认旧菜单、滚动条、编辑器、
  预览和关于页的 `UiTheme` 样式函数均无生产调用方，删除对应 Widgets 样式源、QPalette/QIcon
  依赖，以及两个未使用的播放布局样式 helper；`UiTheme::colors()` / `isDarkTheme()` 继续
  作为 QML 与原生窗口共享的配色源。`MiaCode` Release 编译通过，针对性测试 7/7 通过；
  全量 CTest 仍为 105/108 通过，三个既有失败项未变化：`timeline_model_spec`、
  `qml_editor_controller_spec`、`qtavplayer_platform_spec`。
- **第 5 步本轮继续移除隐藏快捷键 Actions（2026-09-05）**：QML 已直接绑定
  `ShortcutRegistry` 的字体快捷键，删除未被调用的 Session 字体 `QAction`、滚轮快捷键注入
  和 `SettingsHost::applyConfiguredShortcuts()` 转发；字体设置与 QML 快捷键入口保持不变。
  `MiaCode` Release 编译通过，针对性测试 7/7 通过；全量 CTest 仍为 105/108 通过，
  三个既有失败项未变化：`timeline_model_spec`、`qml_editor_controller_spec`、
  `qtavplayer_platform_spec`。
- **第 5 步本轮继续移除空的工具栏边界（2026-09-05）**：`finishFrameBootstrap()` 的
  `QToolBar*` 参数从未被使用，调用方始终传 `nullptr`；删除该参数、前置声明和 Widgets
  include，启动定时器与其余 bootstrap 行为不变。`MiaCode` Release 编译通过，针对性测试
  7/7 通过；全量 CTest 仍为 105/108 通过，三个既有失败项未变化。
- **第 5 步本轮继续移除死的 native 文件菜单入口（2026-09-05）**：确认 v2 的新建、打开、
  最近文件、另存为和页面切换不经过旧 `Session` 菜单槽；QML 使用 `QmlDocumentModel` /
  `DocumentBridge` / `UiRequestService`。删除旧 `Session` 新建、打开、最近文件、保存转发，
  以及 `DocumentFileFlow.cpp` 中的 native 新建/打开文件对话框路径；保留 QML 实际使用的
  文件服务、启动目标、备份恢复、空文档初始化和异步离开文档流程。同步更新
  `qml_document_lifecycle_contract_spec`，使其守卫新的 QML 新建链路和旧入口缺失。`MiaCode`
  Release 编译通过，针对性测试 7/7 通过；全量 CTest 仍为 105/108 通过，三个既有失败项
  未变化。
- **第 5 步本轮继续移除空的编辑器 UI 刷新桥（2026-09-05）**：确认
  `updateEditorHeader`、`updateEditorStatus`、`updateEditorEmptyState`、`updateMetadataPageMode`
  等 runtime 实现均为空，删除对应的 Session/DocumentSessionHost 转发、调用点及延迟 UI 刷新
  状态位；时间线游标、预览跟随、QML 状态发布和删除难度确认逻辑保持不变。`MiaCode` Release
  编译通过，相关测试 8/8 通过；全量 CTest 仍为 105/108 通过，三个既有失败项未变化。
- **第 5 步本轮继续移除空的文件标签刷新桥（2026-09-05）**：确认
  `PlaybackCoordinator::updateCurrentFileLabel()` 实现为空，且全仓唯一调用仅在设置文件路径时；
  删除 coordinator/Session 转发、空实现和调用，文件路径仍由 `updateWindowTitle()` 统一进入 QML
  标题状态。`Qt6::Widgets` 仍保留给真实 native fallback。`MiaCode` Release 编译通过，相关
  测试 8/8 通过；全量 CTest 仍为 105/108 通过，三个既有失败项未变化。
- **第 5 步本轮继续清理空的 runtime UI 桥（2026-09-05）**：移除旧媒体工具/关于/设置槽的空
  转发、文档侧栏与页面填充空方法、按难度设计师空对话框入口，以及无实现的 `noteStatus` 状态
  汇报；同时删除仅服务于旧 Widget 侧栏折叠的状态位。QML 书签折叠状态仍由
  `ViewState.qml` 持有，真实媒体工具与文档切换/保存逻辑保持不变。`MiaCode` Release 编译通过，
  相关测试 8/8 通过；全量 CTest 仍为 105/108 通过，三个既有失败项未变化。
- **第 5 步本轮继续收口旧状态栏参数（2026-09-05）**：`showStatusMessage` 原先只透传到已
  删除的空 `noteStatus`，现从文件打开与文档应用链路的 Session/DocumentSessionHost 接口移除；
  `showErrors`、文件编码记录、启动恢复和真实 QML/native 错误处理保持不变。`MiaCode` Release
  编译通过，相关测试 8/8 通过；全量 CTest 仍为 105/108 通过，三个既有失败项未变化。
- **第 5 步本轮继续迁移文档错误/确认回退（2026-09-05）**：打开失败改由
  `UiRequestService::postNotice(Error, ...)` 通知 QML，删除难度确认收口到 QML
  `DifficultyList` 后再执行，移除 `DocumentFileFlow.cpp` / `DocumentPages.cpp` 的
  `QMessageBox` 与 `DialogLocalization` 依赖；未保存同步确认仍保留在真实 fallback 中。`MiaCode`
  Release 编译通过，相关测试 8/8 通过；全量 CTest 沿用上一轮 105/108 基线。
- **第 5 步本轮继续移除预览门控的 Widgets 查询（2026-09-05）**：`StageMediaHost` 的创作态
  检查从 `QApplication::activeModalWidget/activePopupWidget` 改为
  `QGuiApplication` 的 modal window 与可见 popup window 检查，模态或 popup 窗口出现时仍会
  禁止触控创作输入。`MiaCode` Release 编译通过，相关测试 6/6 通过；完整 CTest 为
  105/108 通过，失败项与既有基线完全相同。
- **第 5 步本轮继续清理快捷键注册器的旧 Widgets 边界（2026-09-05）**：确认
  `ShortcutRegistry` 的 `QAction/QShortcut` 应用重载全仓没有调用方，QML 实际使用的是纯
  `QKeySequence`、文本和编辑接口；删除三个旧重载、前置声明、Widgets include 及仅服务于
  它们的兼容快捷键展开逻辑，QML 快捷键行为保持不变。`MiaCode` Release 编译通过，相关
  测试 4/4 通过；完整 CTest 为 105/108 通过，失败项与既有基线完全相同。
- **第 5 步本轮继续删除无调用方的对话框工具（2026-09-05）**：确认
  `DialogLocalization.h` 中的通用 `showMessageBox`、`execMessageBox`、`prepareDialogWindow`、
  独立定位实现和 `localizeButtonBox` 均无仓库调用方；保留未保存确认仍在使用的
  `QMessageBox` 本地化、预览快捷键保护和 detached-parent 行为，删除死工具及其专用 include。
  `MiaCode` Release 编译通过，相关测试 4/4 通过；完整 CTest 仍为 105/108，三个既有失败项未变化。
- **第 5 步本轮继续清理对话框堆叠死代码（2026-09-05）**：确认全局 modal 对话框批量关闭、
  对话框堆叠 guard、transient-parent 绑定及未使用的预览快捷键/模态查询辅助均无生产调用方；
  保留当前未保存确认实际使用的快捷键保护和 parent 行为。`MiaCode` Release 编译通过，相关
  测试 4/4 通过；完整 CTest 仍为 105/108，三个既有失败项未变化。
- **第 5 步本轮继续削薄 native 对话框定位查询（2026-09-05）**：`Shared::centerDialogOnAnchor`
  已先通过 `QGuiApplication` 的 active/visible top-level window 完成定位，后续重复的
  `QApplication::activeWindow()` QWidget 查询删除；真实的 `QDialog/QWidget` parent 定位和屏幕
  边界保护保持不变。`MiaCode` Release 编译通过，相关测试 4/4 通过；完整 CTest 仍为
  105/108，三个既有失败项未变化。
- **第 5 步本轮继续收窄对话框 guard 的应用类型（2026-09-05）**：
  `PreviewShortcutOverrideGuard` 安装事件过滤器只需要 `QObject`，改用
  `QCoreApplication::instance()`，`DialogLocalization.h` 不再直接依赖 `QApplication`；实际
  `QDialog/QMessageBox/QWidget` fallback 行为保持不变。`MiaCode` Release 编译通过，相关测试
  4/4 通过；完整 CTest 仍为 105/108，三个既有失败项未变化。

#### 第 2 步：一处需要更正的既往判断

此前编排者向所有者描述第 2 步为「删 `src/app/ui/` 的 widget 辅助件（5 个文件）」。**这是错的**，
两处都错：

1. **目录是 18 个文件，不是 5 个。**
2. **`src/app/ui/` 不是可整体删除的目录。** 其中四组有 QML 路径生产消费者，必须原样保留：

| 文件 | QML 侧消费者 |
|---|---|
| `UiText.h/.cpp` | `QmlTimelineModel`、`QmlDocumentModel`、`QmlUiSettings`、`QmlPreferencesModel`、`QmlLatencyModel`、`QmlMediaToolsModel`、`QmlAppBackgroundModel`、`QmlPreviewSettingsModel`、`QmlAudioSettingsModel`、`v2/ApplicationServices.cpp` 等 12 处 |
| `AppBackgroundSettings.h/.cpp` | `qml_ui/preferences/QmlAppBackgroundModel.h` + `AppBackgroundSettingsSpec` |
| `ThemeVariantResolver.h/.cpp` | `qml_ui/QmlUiSettings.cpp` + `ThemeVariantResolverSpec` |
| `ShortcutRegistry.h/.cpp` | `qml_ui/QmlShortcutModel.cpp` + 独立测试 target |

所以第 2 步的真实内容**不是删文件，是剪死表面**。

#### 一个会造成假结论的检索陷阱（务必记住）

`src/app/ui/` 在 include path 上，这些头是**裸文件名**被引用的：

```
#include "UiTheme.h"            // 实际写法
#include "ui/UiTheme.h"         // 不存在，按这个搜会得到全 0
```

编排者第一次用 `grep -rl "ui/UiTheme\.h"` 得到「零 includer」，据此差点判定
`UiTheme` / `DialogLocalization` / `WindowParityMetrics` 全是死文件。**实际相反**：
`UiTheme.h` 有 40 个 includer，`DialogLocalization.h` 有 35 个。搜索这批头一律用裸文件名形式。

（另注：zsh 下 `grep --include=*.cpp` 不加引号会被 glob 吃掉并报 `no matches found`，
必须写成 `--include='*.cpp'`。第一次统计就是这么静默失效的。）

#### 第 2 步调查结论（已核实，可直接据此执行）

**任务 A —— 75 个 include 里 55 个是死的**（引了头但正文一次都没用过对应命名空间）：

- `UiTheme.h`：40 个 includer，**26 个零处 `UiTheme::`**
- `DialogLocalization.h`：35 个 includer，**29 个零处 `UiDialogs::`**

死 include 集中在 `src/app/runtime/` 下（document / playback / export / preview / media /
validation / editor 各子目录），是 Widgets 时代共享 include 块的残留。

**尚存的真实耦合**（这才是后续步骤要面对的）：

- `UiDialogs::` 活调用点 6 个文件：`shell/Interaction.cpp`（3 处）、
  `document/DocumentFileFlow.cpp`（3 处 `QMessageBox`）、`document/DocumentFlow.Internal.h`（4 处）、
  `document/DocumentFlow.cpp`、`document/DocumentPages.cpp`、`validation/ValidationRender.cpp`（`QDialog`）。
  `DialogLocalization.h` 本身是纯 Widgets（`QDialog` / `QApplication::topLevelWidgets` / `QWidget`）。
- `UiTheme::` 活调用点 13 个文件；按符号统计：`colors` 32、`Colors` 12、`scrollBarStyleSheet` 5、
  `isDarkTheme` 5、`styleRoundedMenu` 3、`applyApplicationTheme` 3、`previewPanelStyleSheet` 2、
  `menuSelectionCheckIcon` / `editorShellStyleSheet` / `applicationPalette` /
  `aboutDialogStyleSheet` 各 1；旧 `pausePreviewButtonStyleSheet` 已随隐藏暂停按钮分支删除。
  注意 `colors()` / `isDarkTheme()` 连 `src/editor/BracketScopeHighlighter.cpp` 都在用，
  **是 QML 路径也依赖的配色源，不能跟着 Widgets 一起摘**。
- `UiNativeWindowTheme.h` 被 `qml_ui/QmlUiBootstrap.cpp` 引用（用的是 `QWindow` 重载那一侧），
  `QWidget` 重载那一侧才是待摘的。

**任务 B —— 6 个零外部调用方的 `UiTheme` 样式函数**：
`resolvedTheme`、`editorFindBarStyleSheet`、`metadataPageStyleSheet`、
`metadataEmptyHintLabelStyleSheet`、`outlineListStyleSheet`、`compactToolbarButtonStyleSheet`。

**未核实**（执行者必须自己补上）：上述统计**排除了 `UiTheme.cpp` 自身**，
所以看不到文件内部调用；也**没查 `src/tools/` 下是否有文本扫描 spec 按名字钉住这些函数**。
两项任一命中都不能删。

#### 当前测试基线（2026-09-04 实测，与 §「测试基线的一个噪声源」记录的旧基线不同）

全量 CTest **108 项 / 105 通过**，3 个红：

- `qtavplayer_platform_spec`（长期红）
- `timeline_model_spec`、`qml_editor_controller_spec`（上游样式合并带入的布局断言）

这 3 个与 Widgets 移除无关。判断某轮是否引入回归，看**红的数量与名字是否变化**。

#### 第 2 步执行结果（2026-09-04）

已按上面的调查执行，且没有扩大到仍有 QML 生产消费者的四组共享文件。

- **任务 A**：删除 `UiTheme.h` 死 include 26 处、`DialogLocalization.h` 死 include 29 处，共 55 处；
  与调查清单一致。删除后没有暴露传递包含缺口，因此没有补任何直接 Qt include。
- **任务 B**：删除 `editorFindBarStyleSheet`、`metadataPageStyleSheet`、
  `metadataEmptyHintLabelStyleSheet`、`outlineListStyleSheet`、`compactToolbarButtonStyleSheet` 的
  `.h` 声明和 `.cpp` 定义。`resolvedTheme` 保留，因为 `UiTheme::isDarkTheme()` 仍在
  `UiTheme.cpp` 内调用它；5 个删除名在 `src/tools/` 没有文本扫描命中。
- **升级项**：没有触发“补直接 include”“真实行为耦合”或“修改既有 spec”升级规则。
- **验证**：增量 Release 全目标构建通过；完整 CTest 为 108 项 / 105 通过，3 个既有红项仍为
  `timeline_model_spec`、`qml_editor_controller_spec`、`qtavplayer_platform_spec`，没有新增红项。

#### 交接：第 2 步的子代理 Execution Packet（原文保留，可直接下发）

> 下面这份 packet 是执行前准备的原始版本，现保留作审计记录（此前所有者曾中断下发）。
> 其中「执行前请自己复核」「升级规则」两段是本仓库多次踩坑后固化的要求，不要删减。

```text
仓库 /Users/caoyusen/Desktop/MiaCode，分支 feature/qml-ui，工作区干净。这是 Widgets 移除第 2 步。

## 背景（不要重新调查，这些已核实）

src/app/ui/ 不是可整体删除的目录——UiText / AppBackgroundSettings / ThemeVariantResolver /
ShortcutRegistry 都有 QML 路径消费者，必须保留。真正能做的是剪掉死表面。

注意：这些头文件是**裸文件名 include**（#include "UiTheme.h"），不是 ui/UiTheme.h。搜索时用裸形式。

## 任务 A：删掉 55 处死 include

UiTheme.h —— 26 个文件，全部零处 UiTheme::（src/app/runtime/ 下）：
  SessionBootstrapFinalize.cpp, Session.cpp, SessionBootstrap.cpp,
  document/{DocumentDesignerFlow,DocumentFileFlow,DocumentPages,DocumentFlow,
             DocumentAutosave,DocumentTransforms}.cpp,
  playback/{SessionForwarding,Playback,IntroRegion,LayoutUi,FramePacing,Tick,
            QuickParse,Seek,AnalysisFlow}.cpp,
  export/{ExportSnapshot,ExportFlow,ExportWorker}.cpp,
  preview/{StageMediaRoute,WarmupAndSettings}.cpp,
  editor/EditorDisplay.cpp, media/{MediaTools,MediaJobs}.cpp

DialogLocalization.h —— 29 个文件，全部零处 UiDialogs::（src/app/runtime/ 下）：
  SessionBootstrap.cpp, SessionBootstrapFinalize.cpp, Session.cpp,
  document/{DocumentDesignerFlow,DocumentAutosave,DocumentTransforms}.cpp,
  playback/{FramePacing,Layout,LayoutUi,SessionForwarding,Playback,QuickParse,Tick,
            TimelineFlow,IntroRegion,AnalysisFlow,Seek,PlaybackState}.cpp,
  validation/{ValidationFlow,ValidationRuntime}.cpp,
  media/{MediaJobs,MediaTools}.cpp,
  export/{ExportSnapshot,ExportFlow,ExportWorker}.cpp,
  preview/{StageMediaRoute,WarmupAndSettings}.cpp, editor/EditorDisplay.cpp

执行前请自己复核：对每个文件重新 grep -c 'UiTheme::' / grep -c 'UiDialogs::' 确认为 0
再删那一行 include。我的清单可能不全或有误——以你的复核为准，并在 Result Packet 里
报告任何与我清单不符的地方。

关键风险 —— 传递包含：DialogLocalization.h 和 UiTheme.h 会拉进 <QMessageBox>/<QDialog>/
<QApplication>/<QPalette> 等 Qt Widgets 头。某个文件可能正在靠这条传递链用 Qt 类型
而自己没有直接 include，删掉后会编译失败。

处理办法：给那个文件补上它真正需要的那个直接 include（例如 #include <QMessageBox>），
不要把 ui 头恢复回去。补直接 include 是改进，不是回退。在 Result Packet 里逐个列出
你补了哪些直接 include 到哪些文件——这份清单本身就是"还剩多少真实 Widgets 耦合"的证据。

## 任务 B：删掉零调用方的 UiTheme 样式函数

src/app/ui/UiTheme.h 声明的 17 个函数里，外部调用方计数为 0 的有 6 个：
  resolvedTheme, editorFindBarStyleSheet, metadataPageStyleSheet,
  metadataEmptyHintLabelStyleSheet, outlineListStyleSheet, compactToolbarButtonStyleSheet

删之前必须逐个确认两件事：
1. 它在 UiTheme.cpp 内部是否被别的函数调用（我的统计排除了 UiTheme 自身文件，
   所以看不到内部调用）。内部有调用方的不要删。
2. src/tools/ 下是否有 spec 按名字扫描它。如果有 spec 提到这个名字，不要删，
   直接在 Result Packet 里报告——本仓库的文本扫描 spec 是钉实现拼写的契约，
   删除或削弱它们需要编排者裁决。

确认干净的才删（.h 声明 + .cpp 定义一起删）。

## 升级规则（务必遵守）

1. 零调用方 ≠ 死代码：如果某个函数体除了返回字符串之外还碰了别的状态、或读写了
   全局/单例，那它可能是"从未被接上的功能"而不是垃圾。报告，不要删。
2. 如果删某处 include 后的编译错误暗示的不是缺 Qt 头、而是真实的行为耦合
   （比如缺的是本仓库自己的类型），停下来报告。
3. 任何需要改动既有 spec 断言的情况，一律停下来报告，不要自行改 spec。

## 构建与测试约束（硬性）

- 磁盘只剩 9.2Gi，build-macos 已占 5.2G。只做增量构建，绝对不要 clean/重新 configure。
  构建前先 df -h /，低于 3Gi 就停下报告。
- 并行度上限 -j4，更高会 OOM。
- 跑测试要跑整个 ctest 套件，不要用 -R 只跑子集。
- 已知 3 个红：qtavplayer_platform_spec、timeline_model_spec、qml_editor_controller_spec。
  基线 108 项 / 105 通过。如果红的数量或名字变了，那才是你的问题。

## Result Packet 需要包含

- 任务 A：实际删除的 include 行数（分 UiTheme / DialogLocalization），与清单不符之处
- 任务 A：为修传递包含而补上的直接 include 逐条清单（文件 → 补的头）
- 任务 B：实际删掉哪几个，哪几个因内部调用/spec 扫描而保留（附证据）
- 升级项：任何触发上面 3 条升级规则的东西
- ctest 结果：总数 / 通过数 / 红的名字
- commit hash 与 diffstat
```

#### 第 1 步遗留的一件未做事项

`825a63b6` 删掉 `g_invalidStarPreviewEnabled` 后，`parseInternal` 的第三个参数在**两个调用点
都恒为 `false`**，它守着的解析分支全部不可达。清理它要动解析器内部分支逻辑，
未在无人监督时进行（解析器是本轮栽过跟头的模块——0 字节 qrc 伪装成解析错误那次）。
**单独一轮处理。**

### 元数据页 UI 反馈待办（2026-09-04 所有者截图标注）

来源：所有者在应用截图上标注的三组反馈（2026-09-04），照实记录，未做需求扩展。

- [ ] **1. 三处标签统一改为「谱面信息」**
  现状：三处显示不一致——
  - 左侧「谱面」侧栏条目：`src/app/qml_ui/sidebar/ChartFieldSidebar.qml:43`，
    `text: UiText.text("元数据")`，显示「元数据」。
  - 编辑区顶部标签页：`src/app/qml_ui/editor/EditorTabBar.qml:61`，
    `titleForKey()` 里 `return UiText.text("元数据")`，显示「元数据」。
  - 元数据表单的段标题：`src/app/qml_ui/editor/EditorPane.qml:315`，
    `text: UiText.text("谱面信息设置")`，显示「谱面信息设置」。

  要改成：三处统一显示「谱面信息」。

  背景：上一轮曾按所有者审核把段标题从「元数据」改回 v1 的「谱面信息设置」；这次所有者
  进一步定为三处统一用「谱面信息」——比 v1（谱面信息设置）和当前 v2（元数据）都短，
  是新决定，不是回退。

  落点提示：三处 QML 都是通过 `UiText.text(...)` 查表取文案，本体不在 QML 里。
  「元数据」是 `qmlOnlyEntries()` 里的字面量表条目（`src/app/ui/UiText.cpp:4316`：
  `{QStringLiteral("元数据"), {QStringLiteral("Metadata"), QStringLiteral("メタデータ")}}`）；
  「谱面信息设置」是键值表条目 `editor.metadata`（`UiText.cpp:1727`）和 `sidebar.metadata`
  （`UiText.cpp:1777`），两个键当前指向同一中文文案。改动前要确认这两个键有没有被
  上述三处之外的地方引用，避免连带改动。

- [ ] **2. `clock_count` 字段标签改为「拍数」**
  现状：`src/app/qml_ui/editor/EditorPane.qml:347`，`label: UiText.text("clock_count")`；
  文案本体在 `src/app/ui/UiText.cpp:4302`：
  `{QStringLiteral("clock_count"), {QStringLiteral("clock_count"), QStringLiteral("clock_count")}}`，
  中/英/日三语当前都显示字面 `clock_count`。

  要改成：中文标签改为「拍数」。

  注意：加这个字段时曾有一条约束「不要把 clock_count 译成中文」，那是当时的指示；
  现在所有者明确要中文标签「拍数」，**以本条为准**，覆盖前述约束。

- [ ] **3. 「其他 &xx 字段」不应再重复显示 `&clock_count=4`**
  现状：`clock_count` 已有专用输入框（`EditorPane.qml:345-349`），但同一表单下方
  「其他 &xx 字段」自由编辑区（绑定 `documentSession.metadataExtraText`，见
  `src/app/qml_ui/QmlDocumentModel.cpp:159` 的 `metadataExtraText()`，经
  `src/app/v2/ChartWorkspace.cpp:196` 调用 `SimaiDocument::parseUnmanagedFields`）
  仍会把 `clock_count` 当「未受管字段」列出，与上方的专用框重复。

  根因 / 过滤逻辑位置：受管字段名单在
  `src/core/chart/document/SimaiDocument.cpp:55-69` 的
  `isReservedMetadataKey(const QString& key)`；当前判定为受管的只有
  `title` / `artist` / `first` / `des` / `video` / `kBookmarksFieldKey`
  （行 57-59），以及 `lv_N` / `des_N` / `inote_N` 难度后缀字段（行 64-66）。
  `clock_count` 不在其中，因此会被 `parseUnmanagedFields`
  （`SimaiDocument.cpp:234-246`，内部靠 `isReservedMetadataKey` 过滤）保留进
  未受管字段列表。

  要改成：把 `clock_count` 加入 `isReservedMetadataKey` 的受管字段判定
  （`SimaiDocument.cpp:57-59` 那一行 `if`），使其和 `title`/`artist`/`first`/`des`/`video`
  一样被排除出「其他 &xx 字段」。

### 待所有者裁决（2026-09-03，均不阻塞推进）

推进 4.9d/4.9e 过程中查实、但需要产品或架构决定的四项。**都已查证到可以决策的程度，未擅自处理。**

1. ~~关闭应用时不再做最后一次自动保存检查~~ —— **该结论是错的，2026-09-03 已更正，无需处理。**
   原判断来自对比 `ShellHost::closeEvent`（死代码）与 `finishShellClose`（活路径）两个**函数体**，
   发现后者没有 `runAutosaveCheck`。**但没有跟进被调函数做了什么**：真实链路是
   `requestShellClose` → `documents_->requestLeaveDocument()`，而后者在**函数开头第一件事**
   就是 `runAutosaveCheck(false)`（`DocumentFileFlow.cpp:207`）；同步版 `maybeSaveBeforeContinue()`
   同样如此（`:181`）。死掉的 `closeEvent` 是先调 `maybeSaveBeforeContinue()`（内含一次）
   再显式调一次，属**重复**；活路径只跑一次，覆盖完整。**缺口不存在。**
   *方法教训*：这与同期另一处失误同类——用 `grep "session_"` 断定「剩余调用不碰 Session API」，
   漏了 `guard->` 那 74 处。两次都是**用一个看得见的表面替代实际的因果链**。
   `ShellHost::closeEvent` 因此成为真正的纯死代码，但**未删**：保留它的原有理由已不成立，
   而同一轮里刚证明自己会漏看因果链，不宜再凭一次对比就删 90 行；要删值得单开一轮逐个核对。

2. **`documentValidationChanged` 有 6 个发射点、0 个消费方**——无任何 `connect` / `NOTIFY` /
   QML 处理器。与此前已清理的 `noteStatus` 空函数同类：不报错、不挂测试，只是什么都不发生。
   本轮仍未删也未接线，保留待产品决定。
3. **`appliedQmlWorkspaceRevision_` 应搬进文档域**。它语义上属文档域（写于
   `DocumentFileFlow`，文档域自读两处），挂在 `Session` 上是早于域拆分的历史。
   4.9d 只加了只读查询未搬字段，因为两个文本扫描 spec 钉着它作为 Session 成员的字面拼写。
4. **4.9e 第 3 片的接口设计**——见下方 4.9e 条目。

### 阶段 4.9e 的设计问题（2026-09-03 调查结论）

**困境不是「字段放哪」，是「谁有权决定播放头」。** 读者多不要紧（快照能服务），
问题在**四类写入方，它们写的理由各不相同且都合理**：关停时的批量清零、从工程 JSON
恢复播放速率、媒体后端汇报暂停实际位置后的墙钟重锚、以及文档/导出/延迟域的静默重定位。

**真正的发现**：`PlaybackControl` 建模的是**用户走带命令**，但播放状态改变有另外四个正当理由。
硬塞进 `seek`/`play`/`pause` 只会带上错误副作用，或加一堆 flag 让命令名字说谎。
这些写入方绕过协调器**不是有人偷懒，是接口里没有它们要表达的东西**。

**三个候选方案与取舍**：
- **A. 扩 `PlaybackControl`** —— 一个接口搞定，但它会变成四件不相干事情的杂物袋，
  且 QML 消费者会看到一堆永远不该调的命令。
- **B. 分开「命令」与「权威」两个接口**（推荐）—— `PlaybackControl` 保持面向 QML 的走带 API，
  另立运行时内部契约承接四类写入。**这正是 4.5–4.8 已验证的按消费者切契约的模式**
  （`PlaybackControl` 给 QML、`PreviewPlaybackPort` 给预览、`AudioClockSource` 给时钟读者）。
- **C. 只收窄存储、不动写入方**（照 4.9b 切 `TimelineState` 的办法）—— 改动最小，
  **但治不了病**：决定权仍分散在四个域。「一个播放权威」说的是谁决定，不是字节住哪。

**分片顺序（关键）**：读者与写入方可分开迁移。
- ~~e-1 读者迁快照~~ **撤回，不在第 3 片之前做**。查证后发现：
  `ApplicationServices::playbackControl()` 是可空槽位、`Session` 无现成转发访问器，
  115 个调用点各需判空并决定「槽位为空时读什么」——不是零风险机械改动。
  更关键的是**读者直读并不违反不变量**（它们在读不在决定），这一片的唯一价值是为私有化存储
  做准备，而私有化取决于第 3 片的设计。**先做即投机**：若第 3 片选了方案 C，这 115 处可能白做。
  正确位置是紧跟第 3 片之后。
- [x] **e-2 关停清零归位（2026-09-03 完成）**：那 15 行播放域字段清零本是 4.9d 拆分时留在
  Session 侧的残留，已移入 `PlaybackCoordinator::prepareForShutdown()`。
  **顺序是承重的**：必须先转发给 Session 的编排（停定时器），再清零——
  源码注释写明理由是防残留 singleShot 回调在拆卸期重入请求路径；顺序反了会重新引入一个
  关停期时序 bug，测试抓不到。协调器侧保留并改写了该注释。
- [ ] **e-3 定义权威接口、四个写入方逐个迁**（待裁决方案 A/B/C）
- [ ] **e-4 私有化存储 + 编译期边界 spec**（照 4.9b 先例）

**两个动手前必查项**：
1. `PlaybackCoordinatorSpec.cpp:98-104` 把「8 个宿主都持有 `RuntimeContext::State&`」
   **钉成了 spec 认证的架构事实**。4.9e 等于为播放子集打破它。该断言应**收窄范围**而非删除——
   删掉等于把一条认证过的架构约束悄悄取消。
2. `DocumentPages.cpp::clearTimelineAndPreview()` 把 canonical 字段与十几个非 canonical 字段
   混在同一批清零，**顺序是否敏感无人验证**。有顺序依赖的话，拆开写入方会引入一个只在
   「切换难度」时复现的 bug——正是 CTest 抓不到、只有 GUI 走查能发现的类型。

### 审计与盘点结论（2026-09-02，精简保留）

四次审计/盘点的结论，凡已落实到代码的过程细节都已删去，只留仍在指导后续工作的判断。

**架构审计（4.8 / 4.9a）**：两次审计各抓到一个真问题——失效流程只递增序号而没有 active guard；
共享上下文若声明在宿主之后会先析构、造成借用引用悬空。均已修复并补了回归。审计同时指出
「边界测试只做文本扫描」，以及建议的独立编译单元当时未实施；后者已在 4.9b 补上。

**存量盘点**：共享状态袋剩余 338 个自有字段 = 164 独占 / **165 跨域** / 9 零引用。
**「按宿主切存储」这条路只对时间线成立**——它是唯一自包含的域。播放相关的几个字段各有 8–12 个域
读写，塞进任何一个宿主的存储袋都只是换个地方共享；正确动作是让它们不再是字段，收进协调器私有
成员、其余人改读播放快照。这条结论直接决定了 4.9c–4.9f 的排法。

**完成门槛覆盖盘点**：5 项非 GUI 门槛里 **3 项完全没有测试**，1 项部分覆盖，1 项是文本扫描且漏项。
**三项空缺的根因是同一个**：协调器构造要 `Session&`，因此没有任何测试能构造它——不是没人写，是写不了。
这就是 4.9d 成为关键路径的原因。

**自我更正**：两处「生命周期保证」实为比较成员声明字符串在源码里的先后位置，其中一处是 4.9b 新增的。
当时给的理由（编译期断言看不到成员顺序）本身没错，但掩盖了真正的问题：**这些对象根本构造不出来**。
4.9d 之后应改为真的构造/析构观察。

**运维教训**：这台机器磁盘常年接近满，而磁盘写满在这里不只是「构建失败」，而是**静默污染产物、
伪造成源码级回归**——曾有两个 spec 因 0 字节的生成资源文件而表现为解析器错误，骗过了一轮判断。
判断 spec 红项前，若它倒在解析类断言上，先查构建目录里有没有 0 字节的生成源文件；全量构建前先看磁盘。

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
- [ ] **片头音**（换文件后试听、音量、重启后保留）—— **尚未开始测试**（所有者 2026-09-01 澄清：
      是没测，不是测出问题）。它是本轮唯一改了「谁负责重载音色库」的路径
      （`setIntroSoundFileName` 只发布，窗口响应 `introSoundChanged` 调
      `applyPreviewSfxLevels(reloadAssets=true)`），所以值得单独走一遍。
      静态复核结论：`task_.introSoundFileName` 由 `ExportFlow.cpp` 的 seed task 从外观状态取，
      `applyVideoExportPreferences` 不会从 JSON 覆盖它，所以任务副本与外观状态不会分叉，
      新旧两条 guard 等价；重载的 runtime、调用顺序、同步性也都与改动前一致。

**所有者同时报告：「导出已取消」仍是 Qt 风格弹窗。已定位并修复，见 §7.-4。**

### 7.-6 **v2 的 QML 外壳根本没有浅色配色**（2026-09-01 所有者追问后查实）

所有者在 §7.-5 修复后肉眼观察「差别不大」，并追问是否根本不存在两套配色。**追问成立，实测如下。**

- `theme/Theme.qml` 的 `colors` 是**一份写死的深色调色板**，没有任何浅色分支。
- `darkTheme` 在**整个 QML 层只被引用两次**：`Theme.qml:8` 自身定义，和 `Theme.qml:69`
  的 `overlayAlpha()`——后者只在背景图可读时改**叠加层透明度**，不改任何颜色。
- 时间轴是唯一例外，因为它的调色板来自 C++：`common/TimelineThemeConfig.h` →
  `UiTheme::colors()` → 真正的 `lightColors()` / `darkColors()`
  （`windowBg` 分别是 `#F8FAFD` 与 `#151A20`，确实是两套）。

**结论**：偏好设置里的浅色/深色/跟随系统，对 QML 外壳而言是**空操作**。
§7.-5 的修复仍然必要且正确（通知确实断了），但它能改变的只有背景叠加透明度和原生标题栏；
调色板换不了，因为没有第二套可换。

**这不是本轮引入的**，也不是 §7.-5 的遗漏：v1 的 Widgets 外壳走 `UiTheme::colors()`，
浅色是**能用的**；v2 的 QML 外壳从一开始就只实现了深色。所以这是一条 **v1 有、v2 没补** 的
功能缺失，应记在 §3.C 而不是当作 bug。

**修复路径（未做，需所有者定夺）**：
1. 最省事且不用凭空发明颜色：把 `Theme.qml` 的 token 映射到 `UiTheme::Colors`——
   它已有 44 个经 v1 验证的 token，浅深两套齐全，覆盖 surface / border / text / accent /
   menu / scroll / icon。这样 QML 与时间轴还能共用同一个调色板来源，不再各写一份。
2. **映射覆盖不到的部分**：`Theme.qml` 的 `syntax.*`（keyword / comment / duration /
   modifier / error / warning）在 `UiTheme::Colors` 里**没有对应项**，浅色值需要重新定色——
   这是产品判断，不该由实现顺手决定。
3. 在 1、2 完成之前，偏好设置里的主题选项对外壳是误导性的。

### 7.-5 主题切换只对时间轴生效（2026-09-01 所有者报告，已修）

**报告**：在偏好设置里改主题，只有时间轴跟着变。

**根因**：解析后的主题有两条改变路径，只有一条通知了 QML。

- 系统改配色 → `QStyleHints::colorSchemeChanged` → `QmlUiSettings::reloadTheme()`
  → 更新 `darkTheme_` → `emit themeChanged()` → `Theme.qml` 整套调色板重绑定。
- 用户自己选主题 → `QmlUiSettings::setThemeToken()` → `UiText::setPreferredTheme()`，**到此为止**。
  `UiText` 只负责存储和持久化，不通知任何人。

时间轴之所以还跟着变，恰恰是因为它**绕过了**这条通知：它是个 C++ QSG item，每次重绘直接读
`UiTheme::colors()`（由存储的偏好实时派生），根本不走 QML 的 `darkTheme` 属性。
于是唯一不依赖通知的那个界面，成了唯一会更新的那个。

**不是本轮接口重构引入的**：`git log -L` 显示 `setThemeToken` 自 `bf5bbe6d`
（本轮工作开始之前的 HEAD）加入时就没有调用 `reloadTheme()`。

**已修**：`setThemeToken()` 在写入偏好后调用 `reloadTheme()`——让用户发起的路径终点，
和系统发起的路径终点一致。后者早已在生产里跑，所以这条路径本身是被验证过的。
同时在 `QmlUiBootstrap` 里把 `themeChanged` 接到 `UiNativeWindowTheme::applyToWindow()`：
原生标题栏是**应用**上去的、不是绑定的，不重新应用就会保持出生时的配色。

**守卫**：`qml_ui_theme_contract_spec`（源码契约——`QmlUiSettings` 依赖 `MainWindowShared`
的编辑器字体度量，无法只链 Core）。它同时钉住前提：`UiText::setPreferredTheme` 仍然只存不通知，
所以每个写入者都欠外壳一次通知。反向验证过。

**待所有者复核**：中/英/日三种语言下切浅色/深色/跟随系统，确认编辑器、侧栏、底栏、预览、
导出页、各弹窗**和原生标题栏**都跟着变。语言切换仍然需要重启（未改动）。

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
**待复核**：进入 `PlaybackHost` 二次拆分之前先做分层取证（QtAVPlayer/D3D11VA 帧池、QML/Qt Quick、
私有堆），不得先验归因于 preview texture cache；拆分后用同一场景复测，确认内存归属没有被
PreviewHost / TimelineHost 的重复缓存放大。

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
| 文档桥 | `src/app/qml_ui/QmlDocumentModel.*`、`QmlDocumentProjection.*`、`src/app/runtime/document/DocumentBridge.cpp`、`DocumentSessionHost.h`、`DocumentFileFlow.cpp` |
| 预览桥（当前） | `src/app/qml_ui/QmlPreviewModel.*`、`src/app/v2/PreviewSurface.h`、`src/app/runtime/preview/PreviewHost.*`、`src/app/runtime/playback/PlaybackSurfaceAdapters.*` |
| 时间线桥（当前） | `src/app/qml_ui/QmlTimelineModel.*`、`src/app/v2/TimelineSurface.h`、`src/app/runtime/timeline/TimelineHost.*`、`src/app/runtime/playback/PlaybackSurfaceAdapters.*` |
| 播放域（当前） | `src/app/runtime/preview/PreviewHost.*`、`src/app/runtime/timeline/TimelineHost.*`、`src/app/runtime/playback/PlaybackCoordinator.*`；4.9 仍需清理共享状态与 Widgets |
| 编辑器 | `src/app/qml_ui/QmlEditorController.*`、`QmlEditorInputBridge.*`、`editor/SourceEditor.qml`、`SimaiSyntaxHighlighter.*` |
| 快捷键 | `src/app/qml_ui/QmlShortcutModel.*` ← `src/app/ui/ShortcutRegistry.*` |
| 视频导出 | `src/app/qml_ui/export/QmlExportSession.*`、`export/ExportVideoPage.qml` |
| 页面宿主 | `src/app/qml_ui/QmlEditorPageHost.*`（QML-only `EditorPageRouter`；native fallback 仍在 `DocumentSessionHost`，见 §0） |
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
