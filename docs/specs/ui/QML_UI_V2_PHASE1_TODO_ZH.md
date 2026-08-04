# QML UI v2 一阶段 Todo

> 工作清单，不是最终规格。上下文压缩后以本文件为准继续改；完成或调整条目时同步更新本文件与 `feature-index.md` 中的入口句。
>
> 分支：`feature/qml-ui`（已含 `origin/dev` @ `677a9625` 及之后快进）  
> 入口：默认 v1 QuickShell；`--ui=v2` / `--modern-ui` / `MIACODE_UI_SKIN=v2` → `QmlUiBootstrap`  
> 构建：`build/`（已 ignore）  
> 原型参考：`../MashiroEditor/src/ui`（壳层来源）

## 目标与约束

- v1 / v2 **共存**：禁止再删 QuickShell 再宿主路径（`NativeSurfaceHost` / `StyleBridge` / `QuickShellMain.qml`）。
- v2 终态方向：**纯 QML 壳** + C++ 域服务 + 已有 `QQuickItem`（时间轴/预览）；**主壳**零 `WindowContainer`。
- **例外（一阶段已落地）**：编辑区可对单个 v1 全页做局部 `WindowContainer`（`QmlEditorPageHost`），仅用于 Export / Latency；禁止把整窗 `MainWindow` 再宿主进 v2。
- 共享后端：隐藏 `MainWindow` + `QuickShellController(surfaceHost=nullptr)`。
- 工作区模式（底栏显隐 / 预览画幅 / 导出全屏门闸）以 `MainWindow` 切换为权威，v2 **只读** `QuickShellController` 投影，不在 `WorkbenchState` 另写平行标志。
- 上游纯 v1 壳改动通常不伤 v2；共享 `MainWindow` / Controller / Preview / Timeline API 仍会一起吃到。

## 已完成

- [x] 从 HEAD 恢复被误删的 v1 QuickShell 文件
- [x] fast-forward 对齐 `origin/dev`（含 latency 自定义背景屏蔽等）
- [x] 接入 `src/app/qml_ui/`（Mashiro 壳）+ `QmlUiBootstrap`
- [x] CMake `MiaCode.UI` 模块与皮肤切换入口
- [x] 预览接真 `QuickShellPreviewSurface`；时间轴接真 `TimelineQuickItem`
- [x] 构建目录回到 `build/`（不要再建未 ignore 的 `build-qml-ui/`）
- [x] 关窗对齐 v1：`confirmClose` → `notifyRootCloseAccepted` → `preparePreviewForShutdown`  
      （已删 v2 自管 `closeApproved` / `pendingClose`）
- [x] 导出 / 工具侧栏（v2 风格）+ 编辑区嵌入 v1 `ExportLauncherPage` / `LatencyDetectionPage`
- [x] 对话框类工具走既有弹窗（音视频 / 规范化 / Net）；不嵌主壳
- [x] 内嵌页 Windows 白屏修复（bridge `show()` 顺序对齐 NativeSurfaceHost）
- [x] 导出工作区态接线：`bottomTabsVisible` / `previewCanvasAspectRatio` / `exportPageActive`  
      （手工确认：进导出中心底栏收起、预览进入导出画幅/模式）
- [x] Windows 客户区标题栏：`QmlUiWindowChrome`（仅 v2 bootstrap attach；v1 不动）  
      + 标题栏品牌图复用仓库权威 `resources/icons/app.png`（别名 `icons/app.png`）

## 一阶段待办（按优先级）

### P0 — 正确性 / 契约

- [ ] Document 边界收口：减少 `QmlDocumentModel` 对 `MainWindow` 私有字段的 friend 直写，改为窄公开 API
- [ ] 去掉未使用的 `friend class QmlUiBootstrap`（若仍无用）
- [ ] 确认关窗路径在「脏文档 / 播放中 / 有导出对话框」场景下与 v1 行为一致（手工回归）

### P1 — 让现有 UI 不再说假话

- [ ] 打通 `syntaxIssues`：`runValidateSimaiSilently` 结果灌进 model，驱动底栏「检查」与高亮 diagnostics
- [x] 设置齿轮：去掉演示用 `ViewSettingsPopup`，改为调用 v1 `MainWindow::onPreferences()`
- [x] QQC 共享皮肤：`AppTextField` / `AppTextArea` / `AppButton` / `AppComboBox` / `AppSlider` / `AppMenu*`（几何对齐 v1 dialog*，色用 `Theme`）
- [x] 悬停/选中高亮收口：`HoverChrome` + `NavRow`（仅 v2 `qml_ui`，不碰 v1）；`nav`/`hover` 默认 inset；`AppSwitch`；IconButton `glyph`
- [ ] 禁用菜单/按钮：能接则接（音频/预览设置 → 现有对话框）；短期接不上的改为隐藏，少留灰色死控件

### P2 — 专属壳能力巩固（相对 v1 已有差异，需保持可用）

v2 相对 QuickShell v1 的专属面（完成度仍低，但不要回退）：

- 纯 QML 深色 Theme、自绘标题栏 / Caption、内嵌菜单 + 共享表单控件皮肤（`App*`）
- Activity Bar、`<720` 紧凑覆盖层（设置齿轮复用 v1 首选项）
- QML 多标签编辑器 + 元数据表单/源码双视图
- QML 谱面字段侧栏 / 难度列表、底栏时间轴/检查 Tab

待办：

- [ ] 编辑器能力对齐声明或补齐：IME / 半角 / 书签 / 查找（至少文档写清「暂不支持」）
- [ ] 预览传输条是否升级到 v1 `QuickShellPreviewTransport` 能力（负时域、精密 scrub）——单独产品取舍后再做
- [ ] 全屏预览策略：保持工作区覆盖层，或对齐 v1 OS 全屏（一阶段可维持现状并写明）

### P3 — 业务页（长期）

- [ ] 长期：Export 面板拆「任务 API」与「QML UI」，去掉编辑区局部 `WindowContainer` 宿主税

## 关键路径速查

| 角色 | 路径 |
|------|------|
| 皮肤切换 | `src/app/main.cpp` → `resolveUiSkin()` |
| v2 bootstrap | `src/app/qml_ui/QmlUiBootstrap.*` |
| 契约根 | `src/app/qml_ui/QmlApplicationContext.*` |
| 文档桥 | `src/app/qml_ui/QmlDocumentModel.*` |
| 预览桥 | `src/app/qml_ui/QmlPreviewModel.*` |
| 壳 QML | `src/app/qml_ui/Main.qml`, `workspace/Workbench.qml` |
| 编辑区 v1 页宿主 | `src/app/qml_ui/QmlEditorPageHost.*`（Export/Latency） |
| v2 Windows 客户区标题栏 | `src/app/qml_ui/QmlUiWindowChrome.*`（仅 `QmlUiBootstrap` attach） |
| 导出/工具侧栏 | `sidebar/ExportSidebarPage.qml`, `ToolsSidebarPage.qml` |
| 工作区态消费 | `WorkspaceSplitView.qml` / `PreviewPane.qml` ← `shellController.*` |
| v1 壳（勿删） | `src/app/quick_shell/` |
| 开发索引入口 | `.agents/skills/miacode-dev-guide/references/feature-index.md` |

## 本地命令

```powershell
cmake --build build --target MiaCode -j 8
.\build\MiaCode.exe
.\build\MiaCode.exe --ui=v2
```

## 建议下一刀（维护性优先）

1. **P1 假开关 / 死控件**：工具栏音频/预览设置接对话框或隐藏。  
2. **P1 `syntaxIssues`**：底栏「检查」从占位变成可用。  
3. **P0 Document friend 收口**：降耦合，方便之后继续加 v2 能力不越界。  
4. 产品拍板后再动 P2 传输条 / 全屏策略；Export QML 重写仍放长期。

## 更新规则

1. 每完成一项：勾选本文件对应 checkbox。  
2. 架构/入口变化：同步 `feature-index.md` / `SKILL.md` 中 v2 相关句。  
3. 不要把本清单当成「可以删 v1」的许可证；退役 v1 只能在 v2 功能面明显齐备且另开决策后进行。
