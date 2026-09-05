# v2 重构后 `dev`（v1）独有提交的价值审计与吸纳方案

> 日期：2026-09-05  
> 结论基线：`origin/dev` @ `5f57a82d`，`origin/feature/qml-ui` @ `dd0f471e`  
> 共同祖先：`61c0ec9f`（2026-08-21）

## 1. 结论先行

`dev` 在 v2 分叉后有 24 个独有提交。它们不能整体 merge，也不应逐个完整 cherry-pick：v2 已有 295 个独有提交，产品 UI、文档模型、命令入口和文件对话框都已换轨。正确做法是按提交意图选择性移植。

本次判断如下：

- **P0，立即吸纳**：烟花首次播放预热、竖直直线 Slide 的 CP 贴图朝向、元数据输入校验、紧凑导出质量策略、严格 20 MB PV 压缩策略。
- **P1，随后吸纳**：触控区三种写谱手势、规范化语法的中性命名、中心数据显示字体、片头卡视觉与导出页入场时间、曲绘/PV 导入、可恢复的 PV 移除、重置摆键、末行后继续滚动。
- **P2，可独立排期**：烟花中心淡出、选区拍数、预览视觉数值预设。
- **条件吸纳**：Net 上传日志只在 Net 产品面恢复时移植；延迟页 crash breadcrumb 只在对应崩溃仍可复现时移植，并改为诊断开关控制。
- **明确不吸纳**：v1 版本号、旧 QuickShell/Widgets 的原生文件对话框与模态置顶补丁、被后续提交重复的中心字体提交、仓库指南精简提交。

必须坚持两条边界：

1. **不得把 `dev` 整体 merge 进 v2。** 这会把已经删除的 Widgets 产品路径、QuickShell 兼容层和旧依赖重新带回。
2. **不得完整 cherry-pick 混合提交。** `c1bcaf6d`、`e2d001b4`、`f80cbaae`、`bbe0801a`、`ad0a56a5` 都包含必须拆开的内容。

## 2. 审计方法与范围

执行过的基线检查：

```bash
git fetch --prune origin
git merge-base origin/dev origin/feature/qml-ui
git rev-list --left-right --count origin/dev...origin/feature/qml-ui
git log --first-parent --reverse --oneline origin/feature/qml-ui..origin/dev
git cherry -v origin/feature/qml-ui origin/dev
```

结果：

- `origin/dev...origin/feature/qml-ui` 的独有提交数为 `24 / 295`。
- 24 个 v1 提交均非 v2 的祖先，`git cherry` 也没有找到已按相同 patch-id 落入 v2 的提交。
- `fc8c8c00` 与 `4e373315` 的稳定 patch-id 相同；前者被 `e2d001b4` 撤销，后者重新应用。因此只把 `4e373315` 视为有效源提交。
- 审计以两个远端提交树为准，不把当前工作区的未提交修改当作已经完成的 v2 能力。当前工作区非 clean，执行本方案时应使用独立 worktree。

判定标签：

| 标签 | 含义 |
| --- | --- |
| 立即吸纳 | 会修复确定性错误、数据丢失、首播卡顿或输出不符合约束的问题 |
| 吸纳但重写 | 用户价值明确，但 v1 落点或实现不适合 v2 |
| 条件吸纳 | 依赖尚未恢复的产品面，或仅服务于仍需确认的诊断问题 |
| 不吸纳 | 已由 v2 等价解决、只属于旧壳、已被后续提交取代，或版本/文档不适用 |

## 3. 全部 24 个提交的明确去向

| v1 提交 | 类别 | 结论 | v2 操作 |
| --- | --- | --- | --- |
| `c1bcaf6d` | 编辑与制谱 | **拆分吸纳** | 移植触控区 `/`、`` ` ``、`,` 三种手势；规范化只吸收中性命名，并兼容旧偏好 token |
| `bb620fef` | 预览特效 | **P2 吸纳** | 可按共享 shader 手工移植，视觉回归后独立提交 |
| `b3063f94` | 预览特效 | **P0 立即吸纳** | 移植预热 marker 身份与零秒负触发特例，保留单测 |
| `045754e9` | 编辑器体验 | **P1 重写** | 不移植 `PlainCodeEditor`；在 `SourceEditor.qml` 的 Flickable 中实现虚拟末尾空间 |
| `fc8c8c00` | HUD 字体 | **不吸纳** | 被 `e2d001b4` 撤销且由 `4e373315` 原样重做 |
| `e2d001b4` | 片头/导出预览 | **P1 拆分吸纳** | 移植片头文字视觉和“首次进入导出从 0/片头头部开始”；不要移植它对中心字体的撤销 |
| `4e373315` | HUD 字体 | **P1 吸纳** | 为中心显示增加独立字体 area，同时更新预览与导出两个 QML model 的选项映射 |
| `6787784b` | Net | **条件吸纳** | Net 页面恢复时重做成有轮转、脱敏、明确失败策略的日志服务 |
| `64c34320` | 元数据校验 | **P0 重写** | 移植纯校验能力；v2 用结构化 issue、原子提交和行内错误，不移植 Widgets delegate |
| `b0112f90` | 预览判定贴图 | **P0 立即吸纳** | 共享场景代码可近似原补丁移植，保留 8→5 / 4→1 双向测试 |
| `cd65638b` | 元数据校验 | **P0 与上项合并** | 补难度为空的诊断与状态同步；不建立 v1 侧栏 attention role |
| `c60e7119` | 发布 | **不吸纳** | v1 的 `1.1.0-beta.16` 不得覆盖 v2 版本 |
| `0b59db2a` | 谱面媒体 | **P1 重写** | 提炼无 UI 的媒体事务服务，QML 经 `UiRequestService` 选文件；不移植 Win32 picker |
| `802d5e80` | 谱面变换 | **P1 重写** | 纯变换落到 `core/chart/transform`，接入 v2 统一命令表和 QML editor transaction |
| `f80cbaae` | 旧壳/文件对话框 | **不吸纳** | v2 已用 QML `FileDialog`，Muri 模式入口也已移到预览面板菜单 |
| `6d7e13e5` | 视频导出 | **P0 立即吸纳** | 移植共享 encoder/runtime policy，重点保留质量档之间的 CRF 差异 |
| `bbe0801a` | 延迟诊断 | **条件拆分吸纳** | v1 SpinBox 调整不适用；若崩溃仍存在，只向 `QmlLatencyModel`/sandbox 移植受控 breadcrumb |
| `c49d9c5f` | PV 压缩 | **P0 立即吸纳** | 建独立策略模块，让单文件和批处理共用严格十进制 20 MB、双 pass 和重试规则 |
| `c27107e5` | 谱面媒体 | **P1 重写** | 吸收“移除 PV”能力，但默认改成可恢复移动/备份，不照搬永久删除 |
| `ad0a56a5` | 编辑器体验 | **P2 重写** | 纯函数解析选区拍数并投影到 QML 状态栏；不得带入 v1 版本号 hunk |
| `dadd1253` | 旧壳/对话框 | **不吸纳** | 只是对旧模态置顶方案的中间回退 |
| `c91dece2` | 旧壳/对话框 | **不吸纳** | 修复隐藏 QWidget + QuickShell 的特有问题，v2 架构不存在此前提 |
| `eb9f855e` | 设置预设 | **P2 重写** | 保留“保存/应用视觉数值预设”的意图，重做命名、存储和两个 QML model 的同步 |
| `5f57a82d` | 文档 | **不吸纳** | 仅修改 v1 仓库指南并生成 backup 文件，对 v2 功能无贡献 |

## 4. 分类实施方案

### 4.1 预览判定特效与首次播放预热

对应提交：`bb620fef`、`b3063f94`、`b0112f90`。

#### 判断

- `b3063f94` 是确定性缺陷修复：零秒位置的预热 marker 因 trigger 为负而被 layer builder 丢弃，实际 PSO/纹理创建会延迟到第一次真实播放。v2 仍保留旧判断，缺陷仍在。
- `b0112f90` 是确定性贴图错误：恰好为 `+90°` 与 `-90°` 的竖直直线 Slide 需要相反 handedness。v2 仍保留旧边界判断。
- `bb620fef` 是纯视觉完善：中心 bloom 在 0.5 秒后应比 spokes 更早淡出。价值存在，但不阻塞正确性，可独立回归。

#### v2 落点

- `src/core/scene/PreviewFireworkWarmupPolicy.h`
- `src/core/scene/PreviewJudgeFireworkLayerState.cpp`
- `src/preview/runtime/PreviewRuntime.cpp`
- `src/preview/quick_scene/shaders/PreviewFireworkMaterial.frag`
- `src/core/scene/PreviewJudgeOverlayShared.{h,cpp}`
- `src/core/scene/PreviewChartReviewLayerState.cpp`
- `src/core/scene/PreviewMaimuriDxJudgeLayerState.cpp`

#### 操作步骤

1. 从 `b3063f94` 引入唯一的 `kFireworkWarmupOffscreenCoordinate` 和 `isFireworkWarmupMarker()`；runtime 的注入、清理和 layer builder 必须共用该身份判断。
2. layer builder 仅允许这个唯一 synthetic marker 绕过 `triggerSecond < 0`，真实负时间 note 仍必须拒绝。
3. 从 `b0112f90` 收敛 `buildJudgeOverlayStraightPlacement()` 的布尔参数，使用 `angle >= -90 && angle < 90`；两个调用者统一走相同规则。
4. `bb620fef` 单独提交。移植 shader 常量和 core fade 计算，不与 warm-up 正确性修复混在同一 commit，便于视觉回退。

#### 验收

- 自动：`preview_firework_warmup_policy_spec` 必须证明 chart second 0 的 synthetic 可绘制、普通负时间 marker 不可绘制。
- 自动：`preview_head_layer_spec` 必须同时覆盖 `8-5` 和 `4-1`，并覆盖 chart review 与 Maimuri DX 两个消费者。
- 手工：冷启动后在 0 秒直接播放含 firework 的谱面，首个 firework 不应触发明显卡顿；录制 60 fps 画面对比中心 bloom 0.5–0.8 秒的淡出。

### 4.2 触控区制谱与规范化语义

对应提交：`c1bcaf6d`。

#### 判断

这个提交必须拆成两个独立变更：

- 触控区手势扩展有直接制谱价值：左键写 `/A1`，`Ctrl+Shift+左键` 写 `` `A1 ``，右键写 `,A1`。
- `Fpd/Hinata` 改名为 `SegmentPreserving/CompactSingleLine` 只改变概念命名，不改变算法。中性命名值得吸收，但 v1 补丁不兼容已保存的 `fpd/hinata` token，不能照搬。

#### v2 落点

- `src/core/scene/TouchPadAuthoringState.h`
- `src/editor/TouchPadAuthoringEdit.{h,cpp}`
- `src/preview/quick_scene/PreviewQuickSceneRoot.cpp`
- `src/preview/runtime/PreviewRuntime.{h,cpp}`
- `src/app/runtime/SessionBootstrap.cpp`
- `src/app/qml_ui/QmlEditorController.{h,cpp}`
- `src/core/chart/transform/ChartNormalization.{h,cpp}`
- `src/app/qml_ui/QmlDocumentModel.cpp`
- `src/app/qml_ui/editor/NormalizeOptionsDialog.qml`

#### 操作步骤

1. 把 touch authoring 信号从 `bool useBacktickSeparator` 改为显式 `QChar separator`；链路上不得再把三态压回布尔值。
2. `touchPadAuthoringSeparator(button, modifiers)` 只认可 `/`、`` ` ``、`,` 三个输出；异常值在 editor policy 层回退 `/`。
3. 右键逗号插入保持 v1 语义：即使当前 beat 为空也可向下一拍写入 `,A1`。补空 beat、尾部空格、注释边界、反向 selection 测试。
4. 规范化枚举可改为中性名，但读取偏好时必须同时接受：
   - `fpd` 与 `segment_preserving` → `SegmentPreserving`
   - `hinata` 与 `compact_single_line` → `CompactSingleLine`
5. 新写入只使用中性 token；QML 下拉文案必须为“分段保留 / 单行紧凑”，不能继续显示当前的“v1 / v2”。

#### 验收

- 自动：`touch_pad_authoring_state_spec`、`touch_pad_authoring_edit_spec`、`qml_editor_controller_spec`。
- 自动：`chart_batch_transform_spec` 同时用新旧 preference token 构造 options，输出必须一致。
- 手工：暂停预览时分别用三种手势写入同一位置，undo/redo 每次只产生一个 editor transaction，timeline 与预览刷新一次且位置不跳。

### 4.3 编辑器末尾滚动、重置摆键与选区拍数

对应提交：`045754e9`、`802d5e80`、`ad0a56a5`。

#### 判断

三项都有用户价值，但 v1 实现依赖已被 v2 删除的 `PlainCodeEditor/QTextEdit`，都必须按 QML editor 重写。

`ad0a56a5` 的逗号/`{N}` 扫描还不够稳：它可能把注释内逗号计入，也没有形成可复用的纯解析契约。因此不能把大段正则逻辑直接塞进 QML。

#### v2 落点

- `src/app/qml_ui/editor/SourceEditor.qml`
- `src/app/qml_ui/ChartTransformCommands.h`
- `src/app/qml_ui/QmlDocumentModel.{h,cpp}`
- `src/core/chart/transform/ChartBatchTransform.{h,Selection.cpp}`
- 新建建议：`src/core/chart/selection/ChartSelectionBeatSummary.{h,cpp}`
- `src/app/qml_ui/preferences/QmlPreferencesModel.*`
- `src/app/qml_ui/QmlUiSettings.*`
- `resources/shortcuts.json`

#### 操作步骤

1. **末行后滚动**：给 `editorScroll` 增加只影响 `contentHeight` 的虚拟尾部高度，值为 `max(0, viewportHeight - codeLineHeight)`；不得修改 `TextArea.text`、QTextDocument 或 undo history。关闭偏好时虚拟高度为 0。
2. 保留 `SourceEditor.qml` 当前 `bottomPadding: 0`，不要用 padding 模拟，因为当前文件已明确记录 TextArea clip 会把 padding 变成文字遮罩。虚拟空间应属于 Flickable 的内容范围。
3. **重置摆键**：把 v1 的 `transformCompleteElementsInSelection(..., resetToTap)` 思路移到 `ChartBatchTransform.Selection.cpp`，公开 `resetTapNotesInSelection()`；不要把它放回 editor 类。
4. 在 `ChartTransformCommands.h` 增加 `transform.reset_tap_notes`，让 `QmlDocumentModel::transformChartSelection()` 复用“一键清空”相同的 whole-text/range 路径，结果继续作为一个 QML editor transaction 应用。
5. 默认快捷键不要直接采用 v1 的 `Ctrl+W`，因为它通常表示关闭标签。先通过 `ShortcutRegistry` 冲突检查选一个空闲组合；若产品坚持 `Ctrl+W`，必须明确关闭标签改用什么。
6. **选区拍数**：建立 C++ 纯函数，使用现有 Simai comment scan/segment policy 忽略控制注释和普通注释，计算每个有效 subdivision 下的逗号数，返回 `{totalCommaCount, parts:[{count, denominator}], exact}`。
7. 由 `SourceEditor.qml` 在 selection 变化时调用后端摘要，并在 editor header/status 投影显示；混合分音显示总逗号数，tooltip 展示 `a/N + b/M`。偏好默认开，可关闭。

#### 验收

- 末尾滚动：最后一行可到视口顶部附近；resize、开关偏好前后文本、dirty、undo step 数完全不变。
- 重置摆键：覆盖普通 tap、break、EX、touch、hold、slide、星号复合 slide、空拍、注释、跨 `{N}`、仅部分选中、正反向 selection。
- 拍数：覆盖 selection 前已有 `{N}`、selection 内切换多次 `{N}`、CRLF、`||` 注释内逗号、空 selection、仅半个 token。
- 自动目标：`chart_batch_transform_spec`、`qml_shortcut_binding_spec`、`qml_editor_controller_spec`，并为 beat summary 新增独立 spec。

### 4.4 元数据与难度字段校验

对应提交：`64c34320`、`cd65638b`。

#### 判断

这是 P0。v2 的完整“字段源码”编辑已有原子 `replaceSource()` 和结构化 issue，但表单中的“其他 &xx 字段”仍直接走 `parseUnmanagedFields()`；形如 `&missing_equals` 的输入会在失焦提交时被静默丢弃。难度为空也没有并入 v2 的 validation projection。

v1 的校验意图正确，但 Widgets 的 sidebar attention dot、`QTextEdit::ExtraSelection` 和重复插入的 UiText key 都不能移植。

#### v2 落点

- `src/core/chart/document/SimaiDocument.{h,cpp}`
- `src/app/v2/ChartWorkspace.{h,cpp}`
- `src/app/qml_ui/QmlDocumentModel.{h,cpp}`
- `src/app/qml_ui/editor/EditorPane.qml`
- `src/app/qml_ui/QmlDocumentProjection.*`
- `src/tools/chart_document/SimaiDocumentSpec.cpp`
- `src/tools/v2/ChartWorkspaceSpec.cpp`
- `src/tools/qml_ui/QmlDocumentProjectionSpec.cpp`

#### 操作步骤

1. 引入纯函数 `invalidPropertyLineNumbers()`，但返回结构化 issue（行、列、结束列、错误码）优于只返回行号。
2. 给 `ChartWorkspace` 增加“验证并提交 extra fields”的事务接口。失败时返回 rejected + issues，且不得改变 `document_`、`sourceText_`、revision、dirty 或 save point。
3. `QmlDocumentModel` 保留失败的 attempted text，方式与 `metadataSourceAttemptText_` 一致；QML 表单显示用户原输入和行内错误，不能回弹后把错误文本吃掉。
4. `EditorPane.qml` 的 extra field 提交改用显式命令/结果，不再依赖 write-only property setter 表达失败。
5. 将以下完整性问题投影到 v2 UI：title 为空、artist 为空、designer 为空、曲绘缺失、当前难度 level 为空、extra field 非 `&key=value`。
6. “缺 level”加入现有 syntax/validation 列表时使用独立 error code；不要伪造谱面正文行号跳转。UI 可定位到难度 header。
7. 去重本地化 key，只新增一次每种语言；文案应描述“未提交，原值仍保留”，避免让用户误以为文件已被修改。

#### 验收

- 输入 `&missing_equals`、`&=empty_key`、缩进后的坏字段时，表单保留输入、展示精确行列、文档与 dirty 不变化。
- `&dummy=`、空 value、CRLF、合法缩进字段可提交。
- level 为空时 validation error count 增加；补上 level 后立即消失且不会残留旧 revision 的错误。
- 打开一个本来就缺 title/cover 的文件只显示 attention，不应因为“检查”本身把文档标脏。

### 4.5 中心字体、片头卡与导出预览入场

对应提交：`fc8c8c00`、`e2d001b4`、`4e373315`。

#### 判断

- `fc8c8c00` 不单独处理；有效净结果来自 `4e373315`。
- v2 已有 HUD 分区字体 UI，但只列 ChartInfo、Timestamp、ObjectStats、DebugInfo，中心数据显示仍借 Timestamp 字体。因此中心字体是明确缺口。
- `e2d001b4` 把首次进入导出页改为从 chart 0 开始，若启用片头再移动到负时间片头头部。这比沿用编辑页 playhead 更符合 WYSIWYG 导出试听；同页重建仍保留位置。
- 片头模板颜色、字号与 weight 是可见视觉修正，应移植，但必须做截图对比。

#### v2 落点

- `src/core/scene/PreviewHudState.{h,cpp}`
- `src/preview/quick_scene/PreviewQuickHudLayer.cpp`
- `src/app/qml_ui/export/QmlExportSession.cpp`
- `src/app/qml_ui/preview/QmlPreviewSettingsModel.cpp`
- `src/app/qml_ui/export/ExportVideoPage.qml`
- `src/app/qml_ui/preview/PreviewSettingsDialog.qml`
- `src/app/runtime/document/DocumentPages.cpp`
- `src/app/runtime/export/ExportSnapshot.cpp`
- `src/intro/qml/MaimaiBannerCard.qml`
- `src/intro/templates/maimai_banner.json`

#### 操作步骤

1. 增加 `PreviewHudFontArea::CenterDisplay` 和 `center_display` 持久化 key。
2. 不要继续让 QML combo index 直接 `static_cast` 为 enum。建立一份共享 `HudFontAreaChoice` 映射，导出页和预览设置页从同一数组生成 `{label, sample, areaId}`，读写按 `areaId`。
3. `paintCenterDisplay()` 的 title/value 都通过 `previewHudTimestampFontForArea(CenterDisplay, ...)` 获取字体。
4. `DocumentPages.cpp` 首次进入 export 时设置 seed `0.0`；`ExportSnapshot.cpp` 仅消费一次。保持“同一 export 页内部重建保留位置”的既有分支。
5. 从 `e2d001b4` 只提取片头模板视觉 hunk，绝不提取它撤销中心字体的 hunk。
6. 新增 intro card 固定输入截图基准，至少覆盖中英文长谱师名、BPM、4:3 与 16:9。

#### 验收

- 预览设置和导出设置均出现“中心显示”，为它选字体不会改变 Timestamp 字体，重启后仍保持。
- 未启用片头：从难度页 90 秒位置首次进入导出，试听位于 0 秒。
- 启用片头：首次进入位于负时间头部；在 export 页修改非时间设置后不应再次跳回头部。
- 自动目标：`qml_export_font_contract_spec`、`qml_export_video_page_spec`、`video_export_intro_mode_spec`，并为预览设置 model 增加 area mapping 测试。

### 4.6 曲绘/PV 导入与可恢复移除

对应提交：`0b59db2a`、`c27107e5`。

#### 判断

功能值得吸纳，但 v1 把文件选择、Win32 `OPENFILENAMEW`、预览资源释放、复制事务和 QWidget 提示揉在一个文件中。v2 已有 `UiRequestService` 和 QML `FileDialog`，必须建立独立服务。

`c27107e5` 的“Delete PV”会永久删除多个候选文件，且文案明确不可撤销。v2 应吸收“移除 PV”能力，但默认采用可恢复备份，不能照搬永久删除。

#### v2 落点

- 新建建议：`src/app/v2/ChartMediaService.{h,cpp}`
- 可复用/重构：`src/common/ChartMediaImport.h` → 建议拆为 `.h/.cpp`
- `src/app/qml_ui/QmlDocumentModel.*` 或独立 `QmlChartMediaModel.*`
- `src/app/qml_ui/editor/EditorPane.qml`
- `src/app/v2/UiRequestService.*`
- 预览媒体刷新经现有 `PreviewSurface`/document bridge，不得重新触达 Widgets UI。

#### 操作步骤

1. 服务输入为 chart file path、source path、kind；输出为结构化 `Result{ok, changed, target, backups, warnings, errorCode}`。
2. 图片只接受可由 `QImageReader` 解码的 jpg/jpeg/png；视频当前只接受 mp4。目标保持 `bg.<ext>` / `pv.mp4`。
3. 导入事务顺序：验证 → 请求覆盖确认 → 释放当前媒体句柄 → 把所有冲突候选移动到临时名 → 原子复制目标 → 把临时名落成 `_bak[_N]` → 更新 `&video=pv.mp4` → 刷新 preview/timeline/export。
4. 任一步失败都必须回滚已移动候选；不能留下 `.miacode-import-backup-*` 临时文件。把 v1 header-only 文件移为 `.cpp`，避免文件事务实现散落进每个消费者。
5. QML 只负责发起选择、确认和显示结果；Windows 不再使用 `GetOpenFileNameW`。
6. “移除 PV”默认把 `bg.mp4`、`pv.mp4` 和位于当前 chart 目录内的 resolved override 移为时间戳备份；全部移动成功后再清空 `&video`。部分失败则回滚。
7. 如果还需要永久删除，放在二级危险操作中，并要求第二次明确确认；它不是本次 parity 的必要条件。

#### 验收

- 导入同名文件、不同扩展曲绘、chart 目录内源文件、只读目标、磁盘写失败、预览正持有 PV、大小写不同的 Windows 文件名。
- 导入成功后 preview、export、ZIP 打包都解析到新文件，旧文件可在备份中恢复。
- 移除失败不清 `&video`；成功后不再播放旧 PV，undo 文档字段或恢复备份都有明确路径。
- 为 `ChartMediaService` 增加临时目录测试，不依赖 GUI。

### 4.7 紧凑视频导出质量

对应提交：`6d7e13e5`。

#### 判断

P0。v2 的 encoder 与 runtime policy 仍是旧值。v1 修复了两个实际问题：

1. size preset 的 CRF 不应覆盖 Fast/HighQuality 本身的质量差，而应作为 base CRF 的 adjustment。
2. 非标准 size preset 对 libx264、mpeg4、openh264 都应真正施加 bitrate 约束。

同时提高 Compact 的 bitrate 范围，并把 UltraCompact 固定在更可控的 4 Mbps 峰值，属于输出质量/体积契约。

#### 操作步骤

1. `VideoExportSizePolicy::x264Crf` 改为 `x264CrfAdjustment`，增加 `effectiveVideoExportX264Crf(sizePreset, baseCrf)`。
2. `chooseX264TuningPlan()` 先按 Fast/HighQuality 算 base CRF，再叠加 size adjustment；环境 override 仍具有最高优先级。
3. 对非 Standard 的 libx264 同时追加 bitrate args；mpeg4 直接使用 bitrate plan；openh264 只在 Standard + HighQuality 下做 1.10 放大。
4. 采用提交中的 Compact/UltraCompact 数值，但在 commit message 中明确这是用户可见输出策略变化。
5. 不把这项与 PV 压缩混为一谈：导出 size preset 是成片策略，PV 压缩是上传资源的 20 MB 硬约束。

#### 验收

- `video_export_runtime_policy_spec` 覆盖 Standard/Compact/Ultra 两个质量档的 CRF 矩阵。
- 为 `chooseVideoEncoder()` 增加参数测试：每种可用 codec 在非 Standard 下都含 bitrate/maxrate/bufsize 的预期参数。
- 真实导出固定 60 秒测试谱，记录分辨率、fps、codec、输出体积和 VMAF/SSIM 或至少帧截图；与移植前结果并列保存，不只验证“能导出”。

### 4.8 严格 20 MB PV 压缩策略

对应提交：`c49d9c5f`。

#### 判断

P0。当前 v2 使用 `20 * 1024 * 1024`，且单 pass 估算允许输出刚好等于阈值；如果外部服务约束是十进制 `< 20,000,000 bytes`，当前实现会产生本地认为合格、上传端拒绝的文件。v1 的独立策略、双 pass 和 oversize retry 应吸纳。

#### v2 落点

- 新增 `src/tools/media/PvCompressionPolicy.{h,cpp}`
- `src/app/runtime/media/MediaTools.cpp`
- `src/tools/media/PvBatchCompressionWorker.cpp`
- `src/app/qml_ui/media/QmlMediaToolsModel.cpp`
- `CMakeLists.txt`

#### 操作步骤

1. 定义硬上限 `20'000'000`、工作目标 `19'500'000`，合格条件必须是 `output < hardLimit && output < original`。
2. 策略模块负责 bitrate 计算、按实际输出大小修正第二次计划、生成两 pass 参数；单文件和批处理不得各自保留一套公式。
3. 保持源时间戳/帧率：`-fps_mode passthrough`，不得加入 `-r`。
4. 按 v1 产品意图移除 PV 音轨：`-an`。UI 确认文案必须明确这一点。
5. pass log 使用自动清理的临时目录；取消、ffmpeg 失败、第二次仍超限都删除 temp 并保留原文件/备份。
6. 把策略测试从 `NetClientSpec` 独立为 `pv_compression_policy_spec`；Net 页面是否恢复不应决定核心压缩策略是否有测试。
7. 所有 size gate 从 `<= oldLimit` 统一改成 `< hardLimit` 的一致语义。

#### 验收

- 边界：`19,999,999` 接受，`20,000,000` 拒绝；原文件恰好 20 MB 必须进入压缩。
- 180 秒计划 bitrate、oversize retry 降码率、pass 1 输出到 null device、pass 2 输出 mp4、无 `-r/-c:a`。
- 批处理与单文件对同一个 fixture 产生相同策略；取消/失败后原 PV 和 backup 可用。

### 4.9 Net 上传持久日志

对应提交：`6787784b`。

#### 判断

条件吸纳。v2 已通过 `fd1291c6` 暂时移除 Net 产品面，当前 Net 代码只在 `net_client_spec` 中保持可编译，且 `Qt6::Network` 被刻意排除在 MiaCode 产品目标之外。现在把日志接进主程序没有用户入口，也会破坏依赖边界。

即使恢复 Net，也不应原样移植：v1 固定向 `net-upload.log` 无限 append，并写入 URL、目录、服务器返回 details；缺少轮转、脱敏和保留期限。

#### 恢复 Net 时的操作步骤

1. 先恢复 Net 的产品决策和 QML 页面，再把 upload worker 纳入产品 target；不要为了日志单独引入 `Qt6::Network`。
2. 建立结构化上传事件 `{batchId, jobId, stage, outcome, retryAfter, errorCode}`，默认不记录密码、cookie、authorization、query token 和完整响应 body。
3. 日志按 batch 或日期轮转，设置总大小/保留天数；UI 提供“打开日志位置”和本批日志路径。
4. 日志目录不可写时，不应让整个上传批次直接失败；继续上传并在 UI 显示“日志不可用”警告，除非产品明确把审计日志定义为强制合规项。
5. 对取消、登录失败、429 retry、单谱失败、全部成功各写一条终态事件。

#### 验收

- 日志内搜索不到密码、Authorization、Cookie 和敏感 query。
- 日志写失败不改变上传结果；轮转不会无限增长。
- `net_client_spec` 覆盖事件序列，QML 页覆盖路径显示和打开位置。

### 4.10 延迟页输入与 crash breadcrumb

对应提交：`bbe0801a`。

#### 判断

条件拆分吸纳。

- v1 的 `QDoubleSpinBox::NoButtons` 不适用；v2 已使用 `AppTextField`，等价输入目标已满足。
- v1 在 Widgets 页切换、检测算法和 sandbox 安装的每个阶段强制写日志并同步 flush。这能定位硬崩溃，但会把一次临时事故的高成本日志永久化。
- v2 的入口已改为 `LatencyPage.qml` → `QmlLatencyModel` → `LatencyEngine/LatencySandboxController`，旧 `MainWindow::switchToLatencyField` breadcrumb 没有迁移价值。

#### 若崩溃仍可复现，执行以下步骤

1. 把通用 `appendLatencyDiagnosticPhase()` 放在 latency 域，入口改接 `QmlLatencyModel::{enter,detectBpm,detectOffset}` 与 sandbox 的 install/setup/teardown 边界。
2. 以 `MIACODE_LATENCY_CRASH_DIAG=1` 或 `--debug` 控制详细阶段日志；正常运行只记录 begin/end/outcome，不在每步 `flushAsyncLogWriter(500)`。
3. 仅 startup beacon 使用同步落盘，并在诊断文档记录路径、PID 和最后阶段解释。
4. 日志带 document revision、track stamp、decoder、sample count、内存快照，不记录本地完整媒体路径时至少做用户目录脱敏。
5. 定位并修复崩溃后，把详细 phase 降级或移除，只保留能证明修复的回归测试。

#### 验收

- 人为在 decode/analysis/sandbox 安装各阶段注入失败，最后一条 breadcrumb 能准确落在失败边界。
- 诊断开关关闭时无同步 flush 性能损耗。
- 进入/离开延迟页、反复检测、切 decoder、无 track 的路径均有明确终态且不残留 sandbox。

### 4.11 预览视觉数值预设

对应提交：`eb9f855e`。

#### 判断

P2，吸纳但重写。用户价值是让预览设置与导出设置共享一组常用视觉数值：外部亮度、内部亮度、画面方形缩放。v1 使用了“设置软件默认音频 / 恢复项目默认”这组不相关文案，且把 JSON helper 放在 UI header 中，不适合直接移植。

#### v2 落点

- 新建建议：`src/app/v2/PreviewVisualPresetStore.{h,cpp}`
- `src/app/qml_ui/preview/QmlPreviewSettingsModel.*`
- `src/app/qml_ui/export/QmlExportSession.*`
- `src/app/qml_ui/preview/PreviewSettingsDialog.qml`
- `src/app/qml_ui/export/ExportVideoPage.qml`

#### 操作步骤

1. 定义 `PreviewVisualPreset{backgroundBrightnessOuter, backgroundBrightnessInner, layoutSquareScale}`，所有读入值经过统一 clamp。
2. 明确只有一个“本机预设”，按钮文案使用“保存为本机视觉预设 / 应用本机视觉预设”，不要借用音频默认文案。
3. 预设存入应用级 preferences，不写项目 sidecar，不自动覆盖当前项目；只有用户点击“应用”时改变当前 session/task。
4. Preview model 与 Export model 通过同一个 store 读写；应用后必须触发 live preview 和 export audition 更新，但不自动启动导出。
5. 若未来需要多预设，再扩为带名称的列表；本次不要为了一个提交引入完整预设管理器。

#### 验收

- 在预览页保存、导出页应用，以及反向操作都得到相同三个值。
- 重启后本机预设存在；切换项目不自动污染项目设置。
- 非法 JSON、缺字段、越界值回退默认或 clamp，不能产生 NaN。

### 4.12 旧 QuickShell/Widgets 文件对话框、发布号与仓库指南

对应提交：`f80cbaae`、`dadd1253`、`c91dece2`、`c60e7119`、`5f57a82d`。

#### 判断

全部不吸纳。

- `f80cbaae` 的 Windows 原生 picker 是为“可见 QML QuickShell + 隐藏 QWidget backend”修复 HWND ownership；v2 文件选择来自 QML `FileDialog`，不具备该前提。它删除 transport 上的 Muri 按钮，而 v2 已把模式入口放到 `PreviewPane.qml`/`PreviewRenderModeMenu.qml`，目标已经等价满足。
- `dadd1253` 是中间 revert；`c91dece2` 又恢复旧 DialogStackingGuard 并绕开 native QFileDialog。v2 产品源已删除 Widgets dialog guard，移植会逆转清理。
- `c60e7119` 是 v1 `1.1.0-beta.16`；`ad0a56a5` 还夹带 `beta.17`。两者均不得改写 v2 版本。
- `5f57a82d` 只改两份仓库指南并提交 `.backup-20260902`，既无产品代码，也不是 v2 当前结构的可信说明。

#### 操作

把这些 hash 加入本次迁移的显式 denylist。代码审查中若出现以下内容应直接退回：

- `OPENFILENAMEW` / `GetOpenFileNameW`
- `applicationDialogTransientParent` / `DialogStackingGuard`
- 新增产品目标 `Qt6::Widgets` 或为 Net 日志新增 `Qt6::Network`
- `MIACODE_VERSION_PRERELEASE` 回退到 `beta.16/beta.17`
- `.backup-20260902`

## 5. 推荐落地批次与依赖顺序

### 批次 A：共享正确性修复（P0，先做）

1. `b3063f94` 烟花零秒预热。
2. `b0112f90` CP handedness。
3. `6d7e13e5` 紧凑导出质量。
4. `c49d9c5f` 严格 PV 压缩策略。
5. `64c34320 + cd65638b` 的 v2 元数据校验重写。

这五项互相基本独立，但提交仍应一项一 commit。批次完成后先跑定向 specs，再跑全部 CTest。

### 批次 B：编辑生产力（P1）

1. `c1bcaf6d` 触控区三态 separator。
2. `c1bcaf6d` 规范化中性命名与旧 token 兼容。
3. `802d5e80` 重置摆键。
4. `045754e9` 末行后滚动。
5. `ad0a56a5` 选区拍数（可延后到 P2）。

先做纯函数/协议，再接 QML；不要先在 QML 写一套临时算法。

### 批次 C：媒体与导出体验（P1）

1. `4e373315` 中心显示字体。
2. `e2d001b4` 片头卡视觉与 export entry seed。
3. `0b59db2a` 媒体导入服务与 QML 入口。
4. `c27107e5` 可恢复 PV 移除。
5. `bb620fef` 烟花中心淡出可在本批次末独立进入。

### 批次 D：可选项（P2/条件）

1. `eb9f855e` 本机视觉预设。
2. `bbe0801a` 受控延迟 crash diagnostics，仅在问题仍开放时。
3. `6787784b` Net 日志，仅在 Net 产品面恢复时。

## 6. 可执行的分支与提交操作

当前 checkout 有未提交修改，不应原地施工。使用独立 worktree：

```bash
git fetch --prune origin
git worktree add ../MiaCode-v1-port \
  -b codex/v1-dev-value-port origin/feature/qml-ui
cd ../MiaCode-v1-port
git status --short --branch
```

不要执行：

```bash
git merge origin/dev
git cherry-pick c1bcaf6d^..5f57a82d
```

对能复用的共享底层补丁，可只取指定路径并三方应用，然后人工审查：

```bash
git diff b3063f94^ b3063f94 -- \
  src/core/scene/PreviewFireworkWarmupPolicy.h \
  src/core/scene/PreviewJudgeFireworkLayerState.cpp \
  src/preview/runtime/PreviewRuntime.cpp \
  src/tools/preview/PreviewFireworkWarmupPolicySpec.cpp | git apply -3

git diff b0112f90^ b0112f90 -- \
  src/core/scene/PreviewJudgeOverlayShared.h \
  src/core/scene/PreviewJudgeOverlayShared.cpp \
  src/core/scene/PreviewChartReviewLayerState.cpp \
  src/core/scene/PreviewMaimuriDxJudgeLayerState.cpp \
  src/tools/preview/PreviewHeadLayerSpec.cpp | git apply -3

git diff bb620fef^ bb620fef -- \
  src/preview/quick_scene/shaders/PreviewFireworkMaterial.frag | git apply -3
```

即使 `git apply -3` 无冲突，也必须检查 staged diff；这些命令不是自动批准补丁：

```bash
git diff --check
git diff --stat
git diff
```

其余提交按第 4 节重实现，不使用完整 cherry-pick。建议 commit 粒度示例：

```text
fix(preview): warm the firework layer at chart start
fix(preview): correct vertical slide CP handedness
fix(export): preserve quality tiers in compact encodes
fix(media): enforce the decimal 20 MB PV limit
fix(metadata): reject malformed extra fields atomically
feat(editor): add three-way touch authoring separators
feat(editor): reset selected beats to single taps
feat(metadata): import chart artwork and PV through v2 services
```

## 7. 构建与验证门槛

先按仓库现有方式配置带 dev tools 的 Release build，然后至少构建这些目标：

```bash
cmake --build <build-dir> --config Release --target \
  preview_firework_warmup_policy_spec \
  preview_head_layer_spec \
  touch_pad_authoring_state_spec \
  touch_pad_authoring_edit_spec \
  chart_batch_transform_spec \
  simai_document_spec \
  chart_workspace_spec \
  qml_document_projection_spec \
  qml_editor_controller_spec \
  qml_shortcut_binding_spec \
  qml_export_font_contract_spec \
  qml_export_video_page_spec \
  video_export_runtime_policy_spec \
  net_client_spec
```

定向测试通过后运行全量：

```bash
ctest --test-dir <build-dir> -C Release --output-on-failure
```

平台手工回归最少覆盖：

- Windows：冷启动 firework、硬件/软件编码各一次、PV 文件被预览持有时导入/移除、中文路径、QML 文件选择器。
- macOS：触控板/鼠标三种 touch authoring、文件权限失败、VideoToolbox 导出不受 size policy 改动破坏。
- 通用：元数据坏字段不丢失、难度 level 为空的 validation、导出片头负时间、中心字体持久化、末行滚动不污染 undo。

每个批次合入前还应执行：

```bash
git diff --check origin/feature/qml-ui...HEAD
git log --oneline origin/feature/qml-ui..HEAD
```

## 8. 完成定义

本迁移不能以“24 个 hash 都出现过”为完成标准。完成必须满足：

- 第 3 节每个提交都有最终状态：已吸纳、条件未触发或明确拒绝；没有“待判断”。
- 所有 P0 条目有自动测试，且共享核心逻辑没有复制到两套 UI/worker。
- v2 产品 target 没有重新引入 Widgets/QuickShell/Net 依赖。
- 混合提交只吸收被批准的 hunk，版本号、旧对话框和仓库指南没有跟入。
- Windows/macOS 的对应手工回归记录了结果；视觉项有前后截图或录屏，不用主观口头结论代替。
- 最终 PR 描述按本文件的 12 个类别列出“吸纳 / 改写 / 拒绝”结果，便于以后再次比较 `dev` 时从新共同基线继续，而不是重审这 24 个提交。
