---
name: miacode-dev-guide
description: Use when working in MiaCode to locate module owners, reuse QML controls or shared services, change chart/playback/export behavior, or find build and specification entry points.
---

# MiaCode 开发指引

MiaCode 是 Qt 6 / C++ / QML 的 simai 谱面编辑、预览和导出工具。
先找功能 owner，再读相关代码与规格；代码是事实来源，设计提案和历史记录只供参考。

## 按任务阅读

- 找目录、功能入口、可复用组件 → [仓库与复用地图](references/architecture-and-layout.md)
- 修改谱面、时间、播放、渲染或导出语义 → [跨模块同步](references/cross-chain-linkage.md)
- 构建、Spec、调试、资源与文档维护 → [开发与验证](references/build-and-tools.md)
- UI 布局或裁剪问题 → 使用仓库的 `qt-ui-layout-pitfalls` skill。

## 长期边界

- `src/app/ui/` 是产品前端；`src/app/services/ChartWorkspace.h` 定义文档、revision 和保存点的所有权。
- `src/app/services/ApplicationServices.h` 持有共享服务与 typed slots；`src/app/runtime/` 的 Session 装配运行时宿主，前端通过服务/端口调用。
- 谱面数据与场景数学分别归 `src/core/chart/`、`src/core/scene/`；不要把 UI 或 GPU 依赖引入这些层。
- 预览与导出复用进程内 Qt Quick/QSG 场景；音频设备、QML engine、异步任务的生命周期由各自 owner 管理。
- 先复用已有控件、主题、文案、路径解析和领域 helper，再考虑新增抽象。

## 维护方式

唯一维护源是 `.agents/skills/miacode-dev-guide/`，另两份由
`python3 scripts/governance/sync_guides.py --sync` 生成；提交前运行 `--check`。
只在模块归属、公共复用入口或跨模块契约改变时更新本 skill。
不要加入逐函数索引、行号、完整参数/flag 清单、UI 像素值或阶段完成记录。
具体行为放代码/Spec，公开规范从 `docs/INDEX.md` 查找。
