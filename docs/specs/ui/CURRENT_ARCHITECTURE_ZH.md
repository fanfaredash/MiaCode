---
lifecycle: stable-current
owner: src/app
canonical_id: ui.runtime-ownership
last_verified: 2026-09-06
code_anchors: ["src/app/main.cpp", "src/app/ui/Bootstrap.cpp", "src/app/services/ApplicationServices.h", "src/app/runtime/SessionBootstrap.cpp", "src/app/runtime/Session.h"]
---

# 当前应用架构与所有权

本规范描述当前代码的模块边界。阶段目标、拆分进度与旧实现保留在原设计和 Git 历史；这些材料不能覆盖当前运行路径。

## 前端、文档与运行时

产品前端由 `src/app/ui/` 的 QML 页面和 C++ model 构成，启动入口为 `Bootstrap`。
QML engine 创建根窗口，`runtime::Session` 是 QObject 装配对象，拥有运行时宿主并附着根窗口。
没有隐藏的 QMainWindow 产品窗口。

`ApplicationServices` 持有 `ChartWorkspace`、文件服务、分析服务、编辑同步、UI 请求、任务进度、
通知和预览外观状态，并保存宿主安装的 typed slots。它不实现各领域 engine。

| 所有者 | 契约 |
| --- | --- |
| ChartWorkspace | 完整文档、difficulty、revision、保存点与 dirty 真相 |
| ChartWorkspaceFileService | 文档读写、编码与原子保存 |
| AnalysisService | revision 对应的分析快照；异步过期结果不能投影回新文档 |
| EditorSyncController | 编辑器上下文、导航与跟随同步 |
| PlaybackCoordinator | 播放控制、播放快照与音频时钟权威 |
| PreviewHost / TimelineHost | 各自的 surface 入口、投影及命令校验 |
| DocumentSessionHost | 文档会话、文件工作流与页面路由 |
| VideoExportHost / MediaJobsHost | 视频导出与媒体处理引擎 |
| SettingsHost / EditorHost / ValidationHost / StageMediaHost / ShellHost | 偏好、编辑器持久状态、校验展示、舞台媒体、窗口生命周期 |

具体安装关系以 `src/app/runtime/SessionBootstrap.cpp` 和 `ApplicationServices` 为准。
`src/app/runtime/ASSEMBLY.md` 还包含未完成的拆分计划；兼容 adapter 与 RuntimeContext 过渡存储
仍存在，不能把目标架构写成已完成的物理隔离。

## 跨边界数据

- QML model 提交命令并投影服务状态，不创建第二个文档或重新推导 dirty。
- 异步分析与导航携带身份和 revision；播放/走带使用相应 generation、revision、sequence 校验。
- Preview、Timeline 和编辑器的跟随由共享播放/同步契约协调，不各自推进第二个权威时钟。
- 前端通过 engine/port 调用领域行为；仅 bootstrap 的窗口生命周期允许直接接触 Session。
- 关闭窗口或替换任务时按 owner 释放 engine、回调和资源；服务槽位不能留下悬空指针。

## 复用入口

QML 控件、弹层和表单优先复用 `src/app/ui/components/`；主题在 `theme/Theme.qml`，
文案经 Qt Linguist（`translations/*.ts` → 构建目录中的 `.qm` → `/i18n` 内嵌资源），QML 用 `qsTrId`，C++ 用 `qtTrId`；运行期由 `LocaleService` 装载与热切换。偏好持久化在 `PreferenceDocument`。
共享请求/进度复用 UiRequestService 和 JobProgressService；文件与资源解析复用现有领域服务。

仓库地图见 [开发 skill](../../../.agents/skills/miacode-dev-guide/SKILL.md)。
渲染与导出见 [当前渲染契约](../preview/CURRENT_RENDER_EXPORT_CONTRACT_ZH.md)。

## 验证边界

`application_services_spec`、`chart_workspace_spec`、`runtime_context_boundary_spec`、
`playback_storage_boundary_spec` 保护装配和存储边界；QML 生命周期、文档与播放规格见
[Spec 目录](../../tests/SPEC_CATALOG.md)。这些定向规格不能替代实际 UI 与设备验收。
