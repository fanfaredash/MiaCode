# 文档索引

由 `python3 scripts/governance/docs_index.py --sync` 从文档 frontmatter 生成；请勿手工编辑。

入口与维护规则见 [README](README.md)；可执行规格见 [Spec 目录](tests/SPEC_CATALOG.md)。

## stable-current（6）

| 文档 | Canonical ID |
| --- | --- |
| [延迟 Slide、头材质与无头 Slide 规则](specs/chart/SLIDE_DELAY_AND_HEAD_MATERIAL_SPEC.md) | chart.slide-head-material |
| [无理检测规则与行为规格](specs/muri/MURI_DETECTION_SPEC.md) | muri.detection |
| [当前预览与导出渲染契约](specs/preview/CURRENT_RENDER_EXPORT_CONTRACT_ZH.md) | preview.render-export |
| [Timeline 坐标与聚焦规格](specs/timeline/TIMELINE_COORDINATE_FOCUS_SPEC.md) | timeline.coordinate-focus |
| [当前应用架构与所有权](specs/ui/CURRENT_ARCHITECTURE_ZH.md) | ui.runtime-ownership |
| [QML 到 Session 的直接访问边界](specs/ui/QML_UI_V2_BACKEND_SURFACE_ZH.md) | ui.backend-surface |

## reusable-verification（2）

| 文档 | Canonical ID |
| --- | --- |
| [无理检测测试清单](tests/MURI_DETECTION_TEST_CHECKLIST.md) | verify.muri |
| [Timeline 坐标与聚焦测试清单](tests/TIMELINE_COORDINATE_FOCUS_TEST_CHECKLIST.md) | verify.timeline-focus |

## archive-legacy（16）

| 文档 | Canonical ID |
| --- | --- |
| [Preview Runtime Workflow](archive/PREVIEW_RUNTIME_WORKFLOW.md) | — |
| [Video Export Memory Analysis](archive/VIDEO_EXPORT_MEMORY_ANALYSIS.md) | — |
| [Video Export Subprocess Isolation](archive/VIDEO_EXPORT_SUBPROCESS_ISOLATION.md) | — |
| [Cover Studio 封面导出实现计划](specs/cover_export/COVER_EXPORT_UI_RESEARCH.md) | — |
| [封面图层扩展 & 难度卡自定义字体 · 调研与方案](specs/cover_export/COVER_LAYERS_AND_CARD_FONT_RESEARCH.md) | — |
| [Cover Studio 封面工作台 · UI 重设计规范](specs/cover_export/COVER_STUDIO_UI_REDESIGN_SPEC_ZH.md) | — |
| [Bookmark maidata sync plan](specs/editor/BOOKMARK_MAIDATA_SYNC_PLAN.md) | — |
| [MiaCode Extension System v1](specs/extensions/EXTENSION_SYSTEM_V1.md) | — |
| [GPU 渲染设备策略与 QRhi 导出计划](specs/preview/GPU_RENDERING_DEVICE_AND_QRHI_EXPORT_PLAN.md) | — |
| [PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC](specs/preview/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md) | — |
| [Timeline 图层栈与滑动条堆叠顺序规格](specs/timeline/TIMELINE_LAYER_STACK_AND_SLIDE_ORDER_SPEC.md) | — |
| [QML UI v2 架构重设计](specs/ui/QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md) | — |
| [QML UI v2 功能缺口调研](specs/ui/QML_UI_V2_CAPABILITY_GAP_RESEARCH_ZH.md) | — |
| [QML UI v2 已解决事项归档（2026-09-05）](specs/ui/QML_UI_V2_PHASE1_ARCHIVE_2026-09-05_ZH.md) | — |
| [阶段 0a：移除 v1 QuickShell 外壳与其入口 — 实施计划](specs/ui/plans/2026-08-25-v2-stage0a-remove-v1-shell.md) | — |
| [Timeline Qt Quick + GPU 一致性检查清单](tests/TIMELINE_QTQUICK_GPU_PARITY_CHECKLIST.md) | — |

## working（54）

| 文档 | Canonical ID |
| --- | --- |
| [预览音频错位审查（问题 3 / 问题 4）](audit/AUDIO_CLOCK_DESYNC_AUDIT_ZH.md) | — |
| [分支代码审计报告 — `codex/windows-idle-freeze-diagnostics`](audit/BRANCH_AUDIT_WINDOWS_IDLE_FREEZE_DIAGNOSTICS_ZH.md) | — |
| [切换谱面资源释放专题核查](audit/CHART_SWITCH_RESOURCE_RELEASE_AUDIT_ZH.md) | — |
| [docs 公开发布审计](audit/DOCS_PUBLICATION_AUDIT.md) | — |
| [导出页「区间导出无法正常播放」静态审查](audit/EXPORT_PAGE_PLAYBACK_WEDGE_STATIC_REVIEW_ZH.md) | — |
| [代码审计：多语言处理分布 & UI 组件复用（2026-07-07）](audit/I18N_AND_UI_COMPONENT_AUDIT_ZH.md) | — |
| [i18n 收敛第二阶段方案：统一到键值路径 + Muri 详情多语言（2026-07-07）](audit/I18N_KEY_MIGRATION_PLAN_ZH.md) | — |
| [OBS 推流下预览播放卡顿审查（问题 2）+ 与空闲冻结的关联重构](audit/OBS_CONTENTION_PLAYBACK_STUTTER_AUDIT_ZH.md) | — |
| [部分谱面预览自动暂停：初步诊断与交接](audit/PREVIEW_AUTO_PAUSE_INITIAL_DIAGNOSIS_ZH.md) | — |
| [PV 首播画面掉帧：复核结论与修复方案](audit/PREVIEW_FIRST_PLAY_RENDER_STALL_FIX_PLAN_ZH.md) | — |
| [PV 首播画面掉帧审计与交接报告](audit/PREVIEW_FIRST_PLAY_RENDER_STALL_HANDOFF_AUDIT_ZH.md) | — |
| [MiaCode 0.5.0-beta9 至当前版本预览帧率回退审计](audit/PREVIEW_FPS_REGRESSION_AUDIT_BETA9_TO_CURRENT_ZH.md) | — |
| [预览播放卡顿审计（PV 中段起播 / 启动后首次播放）](audit/PREVIEW_PLAYBACK_STUTTER_AUDIT_ZH.md) | — |
| [QML 运行期意外退出排查记录](audit/QML_RUNTIME_CRASH_AUDIT_ZH.md) | — |
| [`feature/qml-ui` 相对 `dev` 的功能差距与补完清单（初版）](audit/QML_UI_V2_DEV_GAP_AND_COMPLETION_PLAN_ZH.md) | — |
| [v2 文档机制审计与重新设计](audit/QML_UI_V2_DOCUMENT_MODEL_AUDIT_AND_REDESIGN_ZH.md) | — |
| [QML UI v2 执行、修复与人工验收审计](audit/QML_UI_V2_EXECUTION_AND_ACCEPTANCE_AUDIT_ZH.md) | — |
| [QML UI v2 阶段 1 与阶段 2 Implementation Plan](audit/QML_UI_V2_PHASE1_PHASE2_EXECUTION_PLAN_ZH.md) | — |
| [Spec 与开发指引整理结果](audit/SPEC_AND_GUIDE_GOVERNANCE_RESULT_ZH.md) | — |
| [v2 重构后 `dev`（v1）独有提交的价值审计与吸纳方案](audit/V1_DEV_POST_V2_PORT_PLAN_2026-09-05_ZH.md) | — |
| [Windows 空闲卡死审计复核 + 复现验证方案](audit/WINDOWS_IDLE_FREEZE_AUDIT_REVIEW_ZH.md) | — |
| [Windows 空闲卡死调研报告：`0.5.2-beta3` → beta2 故障快照](audit/WINDOWS_IDLE_FREEZE_POST_V1_0_0_AUDIT_ZH.md) | — |
| [谱面诊断与规范化规格](specs/chart/CHART_DIAGNOSTICS_AND_NORMALIZATION_SPEC.md) | — |
| [谱面整理分音段策略规格](specs/chart/CHART_NORMALIZATION_SEGMENT_POLICY.md) | — |
| [书签侧边栏与 simai 内嵌存储重设计方案](specs/editor/BOOKMARK_REDESIGN_SPEC.md) | — |
| [QML UI v2 当前 Todolist](specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md) | — |
| [QML v2 中文文案 vs v1 全量比对表](specs/ui/QML_UI_V2_TEXT_PARITY_ZH.md) | — |
| [MiaCode 菜单选中指示符设计规范](specs/ui/UI_MENU_SELECTION_INDICATOR_SPEC_ZH.md) | — |
| [Preference, Bookmark, and Touch Authoring Implementation Plan](superpowers/plans/2026-07-20-preference-bookmark-touch-authoring-implementation.md) | — |
| [Touch Authoring Toggle and Bookmark Marker Implementation Plan](superpowers/plans/2026-07-20-touch-authoring-toggle-and-bookmark-marker-implementation.md) | — |
| [Windows Idle Freeze Diagnostics Implementation Plan](superpowers/plans/2026-08-04-windows-idle-freeze-diagnostics.md) | — |
| [Preview Audio Device Reanchor Implementation Plan](superpowers/plans/2026-08-05-preview-audio-device-reanchor.md) | — |
| [Preview Audio Worker, Device Auto-Pause, And Log Pruning Implementation Plan](superpowers/plans/2026-08-08-preview-audio-worker-autopause-and-log-pruning.md) | — |
| [Export Range Interaction Correction Implementation Plan](superpowers/plans/2026-08-30-export-range-interaction-correction.md) | — |
| [Tab Order and Export Range Implementation Plan](superpowers/plans/2026-08-30-tab-order-and-export-range.md) | — |
| [Cover Export Follow-up Fixes Implementation Plan](superpowers/plans/2026-08-31-cover-export-followup-fixes.md) | — |
| [Cover Export Interaction Fixes Implementation Plan](superpowers/plans/2026-08-31-cover-export-interaction-fixes.md) | — |
| [封面导出谱面帧 v1 复刻实施计划](superpowers/plans/2026-08-31-cover-export-v1-chart-frame-reimplementation.md) | — |
| [Stage 4 MediaTools Ownership Implementation Plan](superpowers/plans/2026-09-01-stage4-media-tools-ownership.md) | — |
| [Spec 与开发指引整理实施计划](superpowers/plans/2026-09-06-spec-and-guide-governance.md) | — |
| [Preference Persistence, Bookmark Underline, and Touch Authoring Design](superpowers/specs/2026-07-20-preference-bookmark-touch-authoring-design.md) | — |
| [Touch Authoring Toggle and Bookmark Marker Design](superpowers/specs/2026-07-20-touch-authoring-toggle-and-bookmark-marker-design.md) | — |
| [Windows Idle Freeze Diagnostics Design](superpowers/specs/2026-08-04-windows-idle-freeze-diagnostics-design.md) | — |
| [Preview Audio Device Reanchor Design](superpowers/specs/2026-08-05-preview-audio-device-reanchor-design.md) | — |
| [Preview Audio Device Auto-Pause Design](superpowers/specs/2026-08-06-preview-audio-device-autopause-design.md) | — |
| [Preview Audio Worker, Device Auto-Pause, And Log Pruning Design](superpowers/specs/2026-08-08-preview-audio-worker-autopause-and-log-pruning-design.md) | — |
| [Remove Watchdog GUI Stack Capture](superpowers/specs/2026-08-09-remove-watchdog-gui-stack-capture-design.md) | — |
| [PV Memory Diagnostics Design](superpowers/specs/2026-08-10-pv-memory-diagnostics-design.md) | — |
| [Export Range Selector Design](superpowers/specs/2026-08-30-export-range-selector-design.md) | — |
| [封面导出后续问题修复设计](superpowers/specs/2026-08-31-cover-export-followup-fixes-design.md) | — |
| [封面导出交互修复设计](superpowers/specs/2026-08-31-cover-export-interaction-fixes-design.md) | — |
| [封面导出谱面帧 v1 复刻设计](superpowers/specs/2026-08-31-cover-export-v1-chart-frame-design.md) | — |
| [阶段 4：MediaTools 非 Widget 所有权迁移设计](superpowers/specs/2026-09-01-stage4-media-tools-ownership-design.md) | — |
| [MiaCode 规格与指引技能治理重整设计](superpowers/specs/2026-09-05-spec-and-guide-governance-design.md) | — |
