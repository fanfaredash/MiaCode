# QML UI v2 一阶段 Todo

> 工作清单，不是最终规格。上下文压缩后以本文件为准继续改；完成或调整条目时同步更新本文件与 `feature-index.md` 中的入口句。
>
> 分支：`feature/qml-ui`  
> 入口：默认 v1 QuickShell；`--ui=v2` / `--modern-ui` / `MIACODE_UI_SKIN=v2` → `QmlUiBootstrap`  
> 构建：`build/`（已 ignore）；本机当前常用 MinGW + Qt 6.11  
> 原型参考：`../MashiroEditor/src/ui`（壳层来源）

## 目标与约束

- v1 / v2 **共存**：禁止再删 QuickShell 再宿主路径（`NativeSurfaceHost` / `StyleBridge` / `QuickShellMain.qml`）。
- v2 终态方向：**纯 QML 壳** + C++ 域服务 + 已有 `QQuickItem`（时间轴/预览）；主壳零 `WindowContainer`。
- 共享后端：隐藏 `MainWindow` + `QuickShellController(surfaceHost=nullptr)`。
- 上游纯 v1 壳改动通常不伤 v2；共享 `MainWindow` / Controller / Preview / Timeline API 仍会一起吃到。

## 已完成

- [x] 从 HEAD 恢复被误删的 v1 QuickShell 文件
- [x] fast-forward 对齐 `origin/dev`
- [x] 接入 `src/app/qml_ui/`（Mashiro 壳）+ `QmlUiBootstrap`
- [x] CMake `MiaCode.UI` 模块与皮肤切换入口
- [x] 预览接真 `QuickShellPreviewSurface`；时间轴接真 `TimelineQuickItem`
- [x] 构建目录回到 `build/`（不要再建未 ignore 的 `build-qml-ui/`）
- [x] 关窗对齐 v1：`confirmClose` → `notifyRootCloseAccepted` → `preparePreviewForShutdown`  
      （已删 v2 自管 `closeApproved` / `pendingClose`）

## 一阶段待办（按优先级）

### P0 — 正确性 / 契约

- [ ] Document 边界收口：减少 `QmlDocumentModel` 对 `MainWindow` 私有字段的 friend 直写，改为窄公开 API
- [ ] 去掉未使用的 `friend class QmlUiBootstrap`（若仍无用）
- [ ] 确认关窗路径在「脏文档 / 播放中 / 有导出对话框」场景下与 v1 行为一致（手工回归）

### P1 — 让现有 UI 不再说假话

- [ ] 打通 `syntaxIssues`：`runValidateSimaiSilently` 结果灌进 model，驱动底栏「检查」与高亮 diagnostics
- [ ] 视图设置：`timelineLength` / `timelineAutoCenter` 接到 `TimelineQuickItem`（或从 `ViewSettingsPopup` 移除，避免假开关）
- [ ] 修正标题栏图标资源（QML 写 `app.png`，模块只有 `app-icon.svg`）
- [ ] 禁用菜单/按钮：能接则接（音频/预览设置 → 现有对话框）；短期接不上的改为隐藏，少留灰色死控件

### P2 — 专属壳能力巩固（相对 v1 已有差异，需保持可用）

v2 相对 QuickShell v1 的专属面（完成度仍低，但不要回退）：

- 纯 QML 深色 Theme、自绘标题栏 / Caption、内嵌菜单
- Activity Bar、视图设置弹层、`<720` 紧凑覆盖层
- QML 多标签编辑器 + 元数据表单/源码双视图
- QML 谱面字段侧栏 / 难度列表、底栏时间轴/检查 Tab

待办：

- [ ] 编辑器能力对齐声明或补齐：IME / 半角 / 书签 / 查找（至少文档写清「暂不支持」）
- [ ] 预览传输条是否升级到 v1 `QuickShellPreviewTransport` 能力（负时域、精密 scrub）——单独产品取舍后再做
- [ ] 全屏预览策略：保持工作区覆盖层，或对齐 v1 OS 全屏（一阶段可维持现状并写明）

### P3 — 业务页最小桥（仍禁止 WindowContainer 嵌主壳）

- [ ] 导出：侧栏去掉占位；最小方案可先弹出既有 Export 对话框 / 页面
- [ ] 工具：latency 等入口同样先对话框，不嵌回主壳
- [ ] 长期：Export 面板拆「任务 API」与「QML UI」，避免再引入再宿主税

## 关键路径速查

| 角色 | 路径 |
|------|------|
| 皮肤切换 | `src/app/main.cpp` → `resolveUiSkin()` |
| v2 bootstrap | `src/app/qml_ui/QmlUiBootstrap.*` |
| 契约根 | `src/app/qml_ui/QmlApplicationContext.*` |
| 文档桥 | `src/app/qml_ui/QmlDocumentModel.*` |
| 预览桥 | `src/app/qml_ui/QmlPreviewModel.*` |
| 壳 QML | `src/app/qml_ui/Main.qml`, `workspace/Workbench.qml` |
| v1 壳（勿删） | `src/app/quick_shell/` |
| 开发索引入口 | `.agents/skills/miacode-dev-guide/references/feature-index.md` |

## 本地命令

```powershell
cmake --build build --target MiaCode -j 8
.\build\MiaCode.exe
.\build\MiaCode.exe --ui=v2
```

## 更新规则

1. 每完成一项：勾选本文件对应 checkbox。  
2. 架构/入口变化：同步 `feature-index.md` / `SKILL.md` 中 v2 相关句。  
3. 不要把本清单当成「可以删 v1」的许可证；退役 v1 只能在 v2 功能面明显齐备且另开决策后进行。
