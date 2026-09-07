# MiaCode 文档

从 [文档索引](INDEX.md) 查当前规范、验收清单和历史资料；从 [Spec 目录](tests/SPEC_CATALOG.md)
查可执行契约的 owner、target 和验证方式。目录名不决定文档是否仍然有效，frontmatter 的生命周期才是分类依据。

## 常用入口

- [当前应用架构](specs/ui/CURRENT_ARCHITECTURE_ZH.md)、[QML/Session 边界](specs/ui/QML_UI_V2_BACKEND_SURFACE_ZH.md)
- [当前预览与导出](specs/preview/CURRENT_RENDER_EXPORT_CONTRACT_ZH.md)
- [Slide 与头材质](specs/chart/SLIDE_DELAY_AND_HEAD_MATERIAL_SPEC.md)、[无理检测](specs/muri/MURI_DETECTION_SPEC.md)
- [Timeline 坐标与聚焦](specs/timeline/TIMELINE_COORDINATE_FOCUS_SPEC.md)
- [调试索引](ops/DEBUG_INDEX.md)、[日志模式](ops/OPERATION_LOG_PATTERNS_SPEC.md)、[发布检查](ops/RELEASE_CHECKLIST.md)
- [仓库开发 skill](../.agents/skills/miacode-dev-guide/SKILL.md)、[贡献指南](../CONTRIBUTING.md)

## 分类与维护

| lifecycle | 用途 | 必填元数据 |
| --- | --- | --- |
| `stable-current` | 已与当前代码核对的规范 | owner、canonical_id、last_verified、code_anchors |
| `reusable-verification` | 可重复验收清单，未勾选不代表已通过 | owner、canonical_id、test_targets |
| `archive-legacy` | 历史设计和退役实现 | lifecycle；正文注明历史性质并指向当前入口 |
| `working` | 计划、研究、审计，或混有待复核旧描述的材料 | lifecycle |

新增当前规范放 `specs/<domain>/`，验收清单放 `tests/`。旧文件先标明状态，不为整理目录破坏既有链接。
`canonical_id` 是稳定的契约标识，文件重命名时保留；当前规范/验收清单间不得重复。
`owner` 是仓库相对模块路径；`code_anchors` 使用真实文件或目录，不维护易漂移的行号。
`last_verified` 记录源代码复核日期，不等于全平台测试通过日期。具体测试结果另行记录。

元数据使用简单 frontmatter：单行字符串，列表使用 JSON 数组（不需要 YAML 依赖）。例如：

```yaml
---
lifecycle: stable-current
owner: src/app
canonical_id: ui.runtime-ownership
last_verified: 2026-09-06
code_anchors: ["src/app/v2/ApplicationServices.h", "src/app/runtime/Session.h"]
---
```

验收清单的 `test_targets` 列出已注册 spec target；纯手动清单填写 `[]`。
历史/工作记录不强制补齐 owner、日期或失效代码锚点，避免制造虚假的复核记录。

## 生成与检查

工具要求 Python 3.9+、Git 和仓库要求的 CMake；索引与镜像工具只依赖 Python 标准库。

```sh
python3 scripts/governance/docs_index.py --sync
python3 scripts/governance/sync_guides.py --sync
cmake -P cmake/devtools/SpecCatalog.cmake

python3 scripts/governance/docs_index.py --check
python3 scripts/governance/sync_guides.py --check
cmake -DMIACODE_SPEC_CATALOG_CHECK=ON -P cmake/devtools/SpecCatalog.cmake
```

文档工具检查 `specs/`、`tests/`、`archive/`、`audit/`、`superpowers/plans/` 和
`superpowers/specs/` 中所有 Git 跟踪或未忽略的 Markdown；新增公开文件缺少生命周期会失败。
当前文档检查 canonical ID、owner、代码锚点和本地文件链接；历史路径不当作当前事实验证。
`INDEX.md` 与 `SPEC_CATALOG.md` 各由自己的生成工具检查，不另建手写清单或豁免表。
`ops/` 保留各自的运维索引与检查规则，不在本次生命周期迁移范围。

公开规范/验收清单使用中文；生成表格中的源码标识保留原样。内部交接、日志、dump、素材来源和
未脱敏记录留在 Git 忽略的本地位置（如 `docs/_private/`），不要为生成索引将其公开。
