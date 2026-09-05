# QML UI v2 当前 Todolist

> 更新：2026-09-05。已解决内容移至
> [QML_UI_V2_PHASE1_ARCHIVE_2026-09-05_ZH.md](QML_UI_V2_PHASE1_ARCHIVE_2026-09-05_ZH.md)，
> 本文只保留未解决事项。GUI 验收仍单独记录，不由构建或 CTest 自动替代。

## 一、当前工程主线

### 1. PlaybackHost / 4.9 后续拆分

- [ ] **4.9d：让 `PlaybackCoordinator` 脱离 `Session&`**。完成剩余 79 处 `session_.` 迁移，
  将文档状态、舞台媒体、偏好持久化和校验刷新收敛为四个窄接口；构造签名去掉 `Session&`
  后立即补 fake-clock spec。
- [ ] **4.9e：明确播放权威写入接口**。在用户走带命令与运行时权威写入之间建立分离契约，
  逐个迁移关停清零、工程恢复速率、媒体暂停重锚、文档/导出/延迟静默重定位四类写入方。
  先由所有者裁决方案 A/B/C，再实现。
- [ ] **4.9e：私有化 canonical 播放状态**。完成 `RuntimeContext::State` 中播放字段的归属迁移，
  保留并收窄现有架构 spec，不以删除既有断言代替完成。
- [ ] **4.9f：补齐非 GUI 完成门槛**：协调器 fake-clock 的 play/pause/resume/stop/seek/scrub/rate，
  `TimelineCommandGate` 的 revision/sequence/drag-follow 顺序，三宿主装配生命周期，以及
  parser → timeline → preview → export 的 revision/chart-time 对齐回归。
- [ ] **清理 4.9f 已知假覆盖**：生产暂停提交路径仍未调用已测试的暂停策略函数；需先决定
  `currentGeneration` 与 `visualSecond` 的语义，再把生产路径接入同一策略并补回归。
- [ ] **删除或处理死的 `ShellHost::closeEvent`**：逐个核对关闭因果链后再决定是否删除，避免
  用表面 grep 误判仍有调用。
- [ ] **处理未决的小项**：`documentValidationChanged` 的 6 个无消费发射点、
  `appliedQmlWorkspaceRevision_` 的归属，以及延后决定的增量时间轴解析。

### 2. 依赖与发布验收

- [ ] **完成依赖 allowlist 的真实功能审计**：分别记录 `ShaderTools`（构建期）、`Qt6::OpenGL`、
  `Qt6::Network`、`Qt6::MultimediaQuickPrivate`、FFmpeg/BASS 的直接使用点、加载时机和平台条件；
  不能把 QML 传递依赖误记成产品主动依赖。
- [ ] **完成部署扫描**：冷启动、编辑/预览、背景视频、普通导出、封面导出分别记录 macOS
  与 Windows 的实际模块加载，并确认 SVG image plugin 是否随安装包提供。
- [ ] **处理跨平台发布**：Windows/Linux Release 构建与启动、文件请求、媒体播放、普通/封面导出
  走查；补齐当前只在 macOS 做过的 GUI 证据。
- [ ] **清理/保留 `CoverCompositionPersistenceGuard`**：它已不在产品 target，仅由
  `cover_layout_model_spec` 编译；需决定是否保留为测试专用保护层，或改成更窄的 spec fixture。

## 二、功能与文案

- [ ] **中文文案与 v1 逐条比对**：继续处理 `qmlOnly` 中尚未确认的 v1 parity，优先检查
  语义变化、遗漏句子和多余空格；不要把真正新增的 v2 标签机械改回 v1。当前已关闭的
  元数据字段文案反馈不再重复排期；关闭确认相关的新缺陷见 GUI 验收项。
- [x] **浅色主题补齐（2026-09-05）**：QML 外壳的 surface/border/text/accent token 已与
  `UiTheme::Colors` 对齐，并完成浅色、深色、跟随系统的词典/主题契约验证；后续只保留 GUI
  走查，不再作为功能缺口排期。
- [x] **底栏高度配置迁移已丢弃（2026-09-05）**：旧的绝对像素键不再迁移到新的比例键，升级后
  使用 v2 的 ratio 默认值；这是有意的配置取舍，不再补旧键迁移。
- [ ] **补回缺陷 2：方向键 seek**；明确焦点、播放中/暂停中和导出页的边界后实现并加 spec。
- [ ] **补回缺陷 5：全屏控件**；先由所有者确认新 UI 形态，再实现，不恢复旧 Widgets 控件。

## 三、延后问题与稳定性

- [ ] **QV4 aggressive GC 验收**：Windows Release 与 Linux Release 各跑完整播放/跟随/暂停/重播
  轮次，确认没有 `Qt6Qml.dll` 访问冲突。
- [ ] **播放期高位内存**：在 PlaybackHost 二次拆分前后，用同一场景区分 QtAVPlayer/D3D11VA
  帧池、QML/Qt Quick 和私有堆归属，不预先归因于 preview texture cache。
- [ ] **切换文档后的 PV 异常**：仅在再次复现时按现有埋点继续排查。
- [ ] **音视频处理工具 GUI 验收**：采样率、提取音频、前置空白、音量归一化，以及 PV 批量压制
  队列的进度、取消和失败呈现。
- [ ] **全量 CTest 基线清理**：当前保留 `timeline_model_spec`、`qml_editor_controller_spec`、
  `qtavplayer_platform_spec` 三项既有失败；先区分上游样式断言/平台长期红，再决定是否修复或
  正式登记为已知限制。

## 四、GUI 验收（独立于工程清单，当前未全部完成）

- [ ] 对本轮封面画布缩放、内置预设、用户预设重命名、预设失败回滚和新增文案做原生桌面走查。
- [ ] **启动后文档初始即脏需 GUI 回归**：疑似由统一谱师 project preference 接线引起，现已
  暂时撤下加载/保存接线，运行时统一谱师只在当前会话由用户显式启用；需回查 v1 代码后再
  决定 preference 的真实持久化、加载恢复、字段不一致时的处理和关闭/放弃语义。当前先用
  带有历史 `.miacode/preferences.json` 且 `&des`/`&des_N` 不一致的工程冷启动，确认标题/关闭
  流程不再显示启动即未保存。
- [ ] **统一谱师关闭窗口仍需 GUI 回归**：这是 v1→v2 迁移时遗漏的关闭/撤销功能，曾出现
  “Master 难度有修改”在点放弃后仍无法关闭窗口；代码已恢复完整 section 回滚并接回
  runtime bridge（当前不持久化 project preference），需确认真实窗口放弃后只问一次且能退出。
- [ ] **脏 metadata/难度 tab 关闭仍需 GUI 回归**：修改谱面信息或难度后点击 x、Ctrl+W 或
  Ctrl+F4 都应询问保存；代码/spec 已覆盖 metadata section 0、难度完整 section、保存失败/
  取消保留 tab，需确认原生弹窗文案和交互。
- [ ] **导出页切换变慢仍需 GUI 回归与耗时取证**：v1 已知原因是重复加载字体，v2 已定位并
  处理重复 seed/audition、同页重入和字体选项/目录扫描缓存；需记录首次进入与重复进入的
  冷/热耗时，确认没有新的回归原因。
- [ ] 对跨难度撤销/脏点、未命名文档保存、自动保存实时刷新、导出范围选择器、音频拖放建谱、
  Esc 行为等历史未逐项记录的路径补验收；代码/spec 已完成的项仍需原生桌面回归。
- [ ] 完成 Windows 侧整体走查；macOS 的初步宿主验收已通过，但不替代上述逐项检查。

## 五、维护治理（新增）

- [ ] **MiaCode-dev-guide 很多说明在 v2 很可能已经过时，需要重新评估或直接删除。**
  先按 `feature-index`、`cross-chain-linkage`、`design-ledger` 三类逐项核对生产代码与测试，
  删除历史叙述，保留仍能指导定位和跨模块同步的约束。
- [ ] **spec test 数量过多，是否有合并/精简的方案。**
  先统计按功能/目标/fixture 的重复覆盖与启动成本，区分源码契约、单元测试、集成测试和
  GUI 验收；提出合并/拆分原则后再改 target，避免为减少数量而削弱边界覆盖。

## 六、完成判据

- [x] 产品代码当前无 `Qt6::Widgets` / `QApplication` / `QWidget` 依赖；真实 fallback 已走
  QML/异步服务边界。后续只需防止回归，并由依赖审计补齐跨平台证据。
- [ ] 每个架构项同时有代码、spec、Release 构建和必要的跨平台/GUI 证据；“spec 全绿”不能
  单独视为验收完成。
- [ ] 任何入口、跨模块契约或运行时依赖变化，都同步更新
  `.codex/skills/miacode-dev-guide/references/` 下的有效说明；过时内容进入归档或删除。
