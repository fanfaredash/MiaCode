# QML UI v2 已解决事项归档（2026-09-05）

本文件承接 `QML_UI_V2_PHASE1_TODO_ZH.md` 中已经完成、无需继续排期的内容。
未完成事项只保留在 Todolist；GUI 验收不因工程项完成而自动勾选。

## 2026-09-05 新发现缺陷与修复

- **统一谱师 preference 与名义管理对话框（2026-09-05 定案）**：模式下沉为 `ChartWorkspace`
  的会话不变量（`setUnifiedDesignerEnabled` / `designerSlots`），开启期间顶层 `&des`、每个
  `&des_N`（含 chart-less slot）与新建难度的名义由 workspace 统一维护，调用点不再各自记得
  广播。preference 保留并沿用 v1 键名，但方向反转：加载时只读文档、只写 preference——文档
  满足统一状态才恢复模式，不满足则静默降级 preference 且不改文档，因此 save point 恒等于
  磁盘内容，也不会出现启动即脏。整源替换（字段源码、放弃、备份恢复）用同一入口重新核对，
  不一致就让模式退位而不是覆盖用户刚写的内容。UI 上「统一谱师」开关由
  `DesignerSlotsDialog.qml`（7 行 `&des_1..7` + 「所有难度采用相同名义」+ 候选选择含
  「直接清除」）取代，确定才提交一次事务，取消无副作用。autosave 不再镜像/持久化任何统一
  逻辑，逐字序列化 workspace 快照。
- **统一谱师导致无法关闭窗口**：这是 v1→v2 迁移时关闭/撤销语义未跟上造成的缺失。
  `ChartWorkspace::revertDifficultyChart()` 原来只恢复 chart，统一谱师修改的 level/designer
  仍保持 dirty，导致放弃后重复询问同一难度；现在恢复完整 difficulty section，并让新建难度
  可被放弃删除。整个文档放弃改为恢复内存 save point，不重新打开文件重新触发统一谱师偏好；
  统一谱师启用/加载曾尝试接回 runtime preference bridge，但启动即脏回归暴露出 v1 语义尚未
  复核；当前已撤下 project preference 的加载/保存接线，加载不再依据该偏好改变运行时模式，
  也不自动改写 `&des`/`&des_N`。具体持久化和关闭/放弃行为留待回查 v1 后重新设计。
- **脏 metadata/难度 tab 可直接关闭**：`EditorTabBar` 现在对所有 dirty editor key 询问保存；
  tab 的 x 按钮、Ctrl+W、Ctrl+F4 共用同一关闭守卫。难度 tab 只保存/放弃该 section，metadata
  tab 以 section id 0 保存/恢复整个文档；保存失败、取消选路径或恢复失败都不会关闭 tab。
  新增 en/zh/ja 关闭确认文案。
- **导出页切换变慢**：确认 v2 `QmlExportSession::enter()` 重复 seed/audition，且同页重入
  重复 leave→enter；现已合并为一次进入、同页切换只改单/批量 tab，并缓存字体选项及字体库
  目录扫描结果。后续 GUI 仍需记录冷/热切换的实际耗时，但代码根因与定向契约已处理。

## 2026-09-05 配置取舍

- **浅色主题补齐**：QML 主题 token 与 `UiTheme::Colors` 对齐，浅色/深色/跟随系统契约验证完成。
- **底栏高度配置迁移已丢弃**：旧绝对像素键不迁移到 v2 ratio 键，升级后采用 ratio 默认值。

## 本轮归档

- **元数据文案与字段归属**：侧栏、编辑器页签和元数据设置统一显示「谱面信息」；
  `clock_count` 统一显示「拍数」，并从通用 `其他 &xx 字段` 中排除；偏好设置、音视频
  处理等入口同步使用词典中的规范文案。
- **封面页缺失功能**：补上编辑器画布缩放（`Ctrl+0` / `Ctrl++` / `Ctrl+-`）、四个不持久化
  内置布局、用户预设重命名；内置帧布局在没有可渲染谱面时被拒绝，帧捕获失败时恢复旧布局。
  缩放只作用于编辑器预览，不改变规范化布局或离屏导出场景。
- **封面导出守卫与忙碌反馈**：没有有效难度、没有输出目录时给出 QML 通知；导出前暂停正在
  播放的预览，并在同步合成前让 BusyIndicator 获得一次绘制机会。
- **120 FPS 选项过滤**：仅在屏幕刷新率达到 119.5 Hz 时显示 120 FPS，并在选项文案中带出
  屏幕最大刷新率。

## Widgets 移除与宿主迁移

- 运行时 v2 UI 已不再依赖 `QApplication` / `QWidget` / `Qt6::Widgets`；未保存确认、保存
  路径、错误通知和页面离开守卫统一由 QML `UiRequestService` 与异步回调承载。
- `QGuiApplication` + 唯一 `QQmlApplicationEngine` 宿主已完成切换；`Qt6::Svg` C++ 直链及
  齿轮图标的死 C++ 渲染路径已删除。
- `src/app/ui/` 中仍被 QML 使用的 `UiText`、背景设置、主题解析和快捷键注册器已保留；
  已删除死的 Widget 表面、样式转发、菜单投影、旧文件菜单入口、空刷新桥和无调用方对话框
  工具。真实 fallback 已迁移，Widgets 仅留在 dev-tool spec target。
- `MainWindow`、`PlainCodeEditor`、`QuickShellController`、旧封面工作台、旧导出/媒体/ZIP
  Widgets 页面与对话框已从产品路径移除。

## 既往已完成的 QML 功能对齐

- 关于、音频设置、预览设置、偏好设置、延迟校准、音视频处理、整谱规范化、封面导出、
  批量导出、ZIP 打包均已有 QML 原生入口或明确的产品移除决定。
- 保存/退出确认、打开文件离开守卫、启动恢复、拖放建谱、导出结果提示、文件选择和进度
  浮层已脱离 Widgets；预览走带推送、数值双击输入、弹窗拖动、导出范围选择器等已完成。
- QML 文案单一通道已建立，en/ja 来源统一回词典；已确认的 7 项 parity 文案修正已完成。

## 不在本归档中的内容

- GUI 验收、Windows/Linux 发布验收、全量 CTest 的既有红项和跨平台依赖加载验证仍以
  Todolist 为准。
- 中文 v1 parity 的逐条比对尚未完成；本轮只关闭了已确认的元数据与入口文案反馈。
- PlaybackHost 4.9 后续拆分、依赖审计、内存/PV 等延后事项仍未解决；浅色主题与底栏配置取舍
  已在本轮归档。
