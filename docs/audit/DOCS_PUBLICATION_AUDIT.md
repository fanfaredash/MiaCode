# docs 公开发布审计

本审计记录当前 `docs/` 的公开化整理结果：公开文档按主题子目录保存，内部交接、英文对照、事故证据和开发过程记录保留在本地 `docs/_private/`，不进入 Git 跟踪。

## 当前结论

- 公开 `docs/` 只保留中文规格、中文测试/验收清单、运行/发布说明、少量历史归档。
- 规格与测试文档统一使用中文；已有英文对照已移入本地私有目录。
- 重要调查资料、交接记录和含本机路径的文档已私有化，不随公开仓库发布。
- `docs/_private/` 被 `.gitignore` 忽略，只作为维护者本地归档。

## 公开目录结构

| 目录 | 内容 |
| --- | --- |
| `docs/specs/chart/` | 谱面诊断、规范化、slide / 头材质等谱面行为规格 |
| `docs/specs/muri/` | 无理检测行为规格 |
| `docs/specs/preview/` | 预览与导出架构规格 |
| `docs/specs/timeline/` | Timeline 坐标、聚焦、图层顺序等规格 |
| `docs/tests/` | 与公开规格对应的测试/验收清单 |
| `docs/ops/` | 调试索引、日志排障、开源检查、发布检查 |
| `docs/archive/` | 明确标注为历史归档的旧设计说明 |
| `docs/audit/` | docs 公开化审计记录 |

## 已公开保留

### 分类决策

| 类型 | 公开处理 | 判断标准 |
| --- | --- | --- |
| 规格 | 公开，放入 `docs/specs/*/` | 描述当前代码应满足的稳定行为、架构约束或用户可见语义；缺少中文时先整理为中文公开版。 |
| 测试 / 验收 | 公开，放入 `docs/tests/` | 可复用的人工验收清单、迁移一致性检查或回归测试步骤；与公开规格互相引用。 |
| 运维 / 发布 | 公开，放入 `docs/ops/` | 不含本机路径、私有日志、账号、内部数据集的调试索引、日志说明、开源和发布流程。 |
| 过时信息 | 默认私有；仅少量保留到 `docs/archive/` | 已被当前架构替代，但仍能帮助理解历史决策；公开前必须标注 `Legacy archive`，并指向当前事实来源。 |
| 开发经验 / 交接 | 默认私有，保留在本地 `docs/_private/` | 包含执行过程、分支名、回滚笔记、事故证据、内部 handoff 或个人化调试上下文；只有重写成稳定公开规格后才公开。 |
| 英文对照 / 草案 | 默认私有 | 公开规格与测试统一只保留中文；英文内容仅作为维护者本地参考。 |

### 当前规格

- `docs/specs/chart/CHART_DIAGNOSTICS_AND_NORMALIZATION_SPEC.md`
- `docs/specs/chart/SLIDE_DELAY_AND_HEAD_MATERIAL_SPEC.md`
- `docs/specs/muri/MURI_DETECTION_SPEC.md`
- `docs/specs/preview/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`
- `docs/specs/timeline/TIMELINE_COORDINATE_FOCUS_SPEC.md`
- `docs/specs/timeline/TIMELINE_LAYER_STACK_AND_SLIDE_ORDER_SPEC.md`

### 测试 / 验收清单

- `docs/tests/MURI_DETECTION_TEST_CHECKLIST.md`
- `docs/tests/TIMELINE_COORDINATE_FOCUS_TEST_CHECKLIST.md`
- `docs/tests/TIMELINE_QTQUICK_GPU_PARITY_CHECKLIST.md`

### 运维 / 发布

- `docs/ops/DEBUG_INDEX.md`
- `docs/ops/OPERATION_LOG_PATTERNS_SPEC.md`
- `docs/ops/OPEN_SOURCE_CHECKLIST.md`
- `docs/ops/RELEASE_CHECKLIST.md`

### 历史归档

- `docs/archive/PREVIEW_RUNTIME_WORKFLOW.md`
- `docs/archive/VIDEO_EXPORT_MEMORY_ANALYSIS.md`
- `docs/archive/VIDEO_EXPORT_SUBPROCESS_ISOLATION.md`

这些归档文档不是当前事实来源；它们保留在公开树中，是因为文件自身已明确标注 `Legacy archive`，并指向当前规格。

## 已私有化

以下类型已移出 Git 跟踪，保留在本地 `docs/_private/` 或原有 ignored docs 中：

- 英文规格对照：公开规格只保留中文。
- 已实施迁移交接：例如导出页迁移记录、重构 handoff。
- 事故复盘和调试证据：例如全屏闪退、掉帧、硬解码调查。
- 性能计划和开发经验：例如播放启动延迟计划、上帝文件拆分记录、日志审计 backlog。
- 草案级未来 RFC：未整理为中文公开规格前不进入公开 docs。

## 脱敏处理

- 含本机 dump 目录、本机 Qt 路径、私有日志证据或本地数据集路径的 tracked 文档已私有化。
- 公开文档中的 local-only 文档链接已删除或改为公开摘要。
- `DEBUG_INDEX.md` 保留必要的调试 flag 信息，但不再链接未公开的调查记录。

## 后续检查

公开前仍需要：

1. 运行当前工作树 secret scan。
2. 运行 Git 历史 secret scan。
3. 检查 release 包内容是否与 `THIRD_PARTY_NOTICES.md` 和 `LICENSE_SCOPE.md` 一致。
4. 若未来要公开 `docs/_private/` 中的任何文档，先单独审查、脱敏，并改写为中文公开版。
