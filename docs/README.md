# MiaCode 文档目录

本目录只保留适合随公开仓库发布的文档。内部交接、调查原始材料、英文对照和未脱敏开发记录保留在维护者本地 `docs/_private/`，不进入 Git 跟踪。

## 目录结构

| 目录                               | 用途                          |
| -------------------------------- | --------------------------- |
| [specs/chart](specs/chart)       | 谱面诊断、规范化、slide / 头材质等谱面行为规格 |
| [specs/muri](specs/muri)         | 无理检测规则与行为规格                 |
| [specs/preview](specs/preview)   | 预览与导出架构规格                   |
| [specs/timeline](specs/timeline) | Timeline 坐标、聚焦、图层顺序等规格      |
| [tests](tests)                   | 与公开规格配套的测试/验收清单             |
| [ops](ops)                       | 调试、日志排障、开源检查和发布检查           |
| [archive](archive)               | 明确标注为历史归档的旧设计说明             |
| [audit](audit)                   | docs 公开发布审计记录               |

## 阅读入口

- 调试与日志：[ops/DEBUG_INDEX.md](ops/DEBUG_INDEX.md)、[ops/OPERATION_LOG_PATTERNS_SPEC.md](ops/OPERATION_LOG_PATTERNS_SPEC.md)
- 发布准备：[ops/OPEN_SOURCE_CHECKLIST.md](ops/OPEN_SOURCE_CHECKLIST.md)、[ops/RELEASE_CHECKLIST.md](ops/RELEASE_CHECKLIST.md)
- 预览/导出架构：[specs/preview/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md](specs/preview/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md)
- 谱面诊断：[specs/chart/CHART_DIAGNOSTICS_AND_NORMALIZATION_SPEC.md](specs/chart/CHART_DIAGNOSTICS_AND_NORMALIZATION_SPEC.md)
- 无理检测：[specs/muri/MURI_DETECTION_SPEC.md](specs/muri/MURI_DETECTION_SPEC.md)
- Timeline 行为：[specs/timeline/TIMELINE_COORDINATE_FOCUS_SPEC.md](specs/timeline/TIMELINE_COORDINATE_FOCUS_SPEC.md)

## 维护规则

- 公开规格和测试清单使用中文；如果只有英文草案，先整理为中文公开版再加入本目录。
- 新增规格时放入对应 `specs/*` 子目录，并在本 README 增加入口。
- 新增测试或验收清单时放入 `tests/`，并在对应规格中互相引用。
- 历史资料只有在明确标注“历史归档”且指向当前事实来源时，才放入 `archive/`。
- 含本机路径、dump、日志证据、未公开素材来源或内部交接上下文的文档不得直接公开。
