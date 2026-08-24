# QML UI v2 执行、修复与人工验收审计

> 状态：**未验收通过**。本文件记录原计划的实施提交、首次阶段完成后的缺陷修复，以及 2026-08-24 的原生桌面验收结果。它不替代原始计划；原计划见 [QML_UI_V2_PHASE1_PHASE2_EXECUTION_PLAN_ZH.md](QML_UI_V2_PHASE1_PHASE2_EXECUTION_PLAN_ZH.md)。

## 原始计划与实施提交

原计划以 `origin/feature/qml-ui` 的 `0c58201b` 为基线，要求依次完成 Task 1–10、每个 Task 单独提交。实际记录如下：

| Task | 提交 | 结果摘要 |
| --- | --- | --- |
| 1 | `a87aeffe` | 文档诊断 revision-aware 投影与过期缓存门控。 |
| 2 | `dc42f2f6` | 同一 document presentation snapshot、完整源码两阶段预检。 |
| 3 | `269abdb3` | QML root lifecycle、ChartDrop 和 overlay 生命周期。 |
| 4 | `1b7b9679` | 阶段 1 验证记录与相关规格/构建修正。 |
| 5 | `39ff3979` | v2 Timeline 控制、tab 状态和高度持久化首次迁移。 |
| 6 | `0edd4ce4` | validation/Muri 对齐分析面板及 revision-safe 导航。 |
| 7 | `80da128a` | v1/v2 共用 `SimaiTextEditPolicy`。 |
| 8 | `74aff5e4` | QML editor controller、IME/paste bridge 与无焦点 completion popup。 |
| 9 | `298c0c8b` | 查找替换、书签、caret/touch-pad 路径与 QML handler fallback 防护。 |
| 10 | `a8b30dac` | 阶段 2 文档、完整 Release 构建与针对性 CTest 记录。 |

Task 10 当时没有把桌面 GUI 手工矩阵标记为通过；这是正确的保守记录。随后原生桌面验收发现运行时断链，进入以下修复阶段。

## 首次阶段完成后的根因审计与修复

| 提交 | 修复的根因与范围 |
| --- | --- |
| `2990a9c0` `fix(qml-ui): synchronize document replacement analysis` | 统一 document replacement 的 QML 状态发布；静默 reset editor tabs，避免重复选择 difficulty 使 revision/慢分析失效；显式 validation 调度同 revision Muri 分析；validation、Muri 与 static refs 写齐 provenance 后才通知。 |
| `a6f57953` `fix(qml-ui): restore timeline follow controls` | 恢复四个 follow 状态及 `followProgress` 持久化；移除底栏 340px 硬上限；将 divider 高度回写 controller；恢复 Timeline header 安全边界和正确坐标；后台 Tab 不再阻断播放期间的 editor-follow 调度。 |
| `714bbdf3` `fix(qml-ui): restore editor navigation and authoring` | 建立 backend → QML editor 的难度/revision-safe navigation；补齐 completion 键盘路由、正文右键菜单、统一 caret 居中、按难度书签侧栏与标题难度；使 Ctrl+preview authoring 走 focus/IME/revision gate、单次 undo 和 preview seek；导航仅在可见、非 metadata、匹配难度/revision 的编辑器中确认。 |

这些提交均有定向 CTest、Release `MiaCode` 构建与 `git diff --check` 证据；自动证据不能代替下列原生 GUI 观察。

## 2026-08-24 原生桌面验收结果

以下结果来自实际桌面运行。`通过` 仅表示本次观察没有发现问题，并不替代跨平台回归。

| 项目 | 本次结果 | 备注/后续动作 |
| --- | --- | --- |
| Validation | 通过 | 本次未发现检测问题。 |
| 切换 Chart | 通过 | 本次未发现状态滞留。 |
| Timeline ↔ preview | 部分通过 | 播放时代码跟随可移动光标；暂停时不能正确移动。`Command`+点击文本仅移动 Timeline，预览未同步。 |
| Timeline 缩窄/缩放 | 待复测 | 已修正高度/几何链；本轮没有明确确认最终视觉和命中效果。 |
| Completion popup | 失败 | 弹出位置与样式不符合预期；Up/Down/Tab/Enter/Escape 没有可感知的高亮/选择反馈。 |
| 编辑器正文右键菜单 | 失败 | 菜单没有正常工作。 |
| 书签 | 通过 | 本次未发现问题。 |
| 标题当前难度 | 通过 | 标题已包含当前难度。 |
| `Command+Z` | 失败 | 某些情形失效并输入字母 `z`。 |
| IME：输入 `a` 后直接 Enter | 失败 | 会插入大量重复 `a`，属于 P0 文本损坏问题。 |

## 未关闭的验收门槛

在以下项目均通过原生桌面复验前，QML UI v2 **不得**宣称阶段 2 已完全验收：

1. 暂停状态的 preview-follow 与 `Command`+点击文本后的 preview seek。
2. completion popup 的光标锚定、主题样式、当前候选高亮以及 Up/Down/Tab/Enter/Escape 的视觉反馈。
3. 编辑器正文右键菜单的鼠标与键盘 context-menu 路径。
4. `Command+Z` 绝不降级为文本输入；IME commit 后直接 Enter 不重复插入。
5. 窄窗口 Timeline 缩放、header hit target 与四个 follow 状态的桌面观察。

后续修复必须先添加可执行的失败回归（优先真实 QML、`QInputMethodEvent` 和快捷键事件，不以源码字符串扫描替代），再实现最小修复；每个独立修复提交后重新运行 Release 构建、相关 CTest 和对应的原生桌面用例。

## 与原计划的关系

- 原计划的 Task 1–10 实施提交保持不重写，以便追溯每项边界和规格的引入时间。
- `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md` 是当前手工验收摘要；本文件提供提交级历史和失败项。
- 当前分支包含三个后续修复提交，但本次验收暴露的失败项尚未修复；本次仅记录，不修改生产或测试代码。
