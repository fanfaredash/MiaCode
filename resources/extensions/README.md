# MiaCode Extensions / MiaCode 扩展

MiaCode extensions are local extension packages. Each extension is an ordinary folder with a manifest file, optional JavaScript entry file, and optional data files. MiaCode loads extensions from the manifest, applies contribution points, and exposes desktop capabilities through the `miacode.*` and `miacode.api.*` APIs.

MiaCode 扩展是本地扩展包。每个扩展都是一个普通文件夹，包含 manifest 文件、可选的 JavaScript 入口文件以及可选的数据文件。MiaCode 通过 manifest 读取扩展，通过贡献点加载功能，并通过 `miacode.*` 和 `miacode.api.*` API 向扩展开放桌面能力。

Extensions do not receive internal C++ objects such as `MainWindow`, editor widgets, preview widgets, `QWidget`, or QML objects. Instead, extensions call public APIs registered by MiaCode. Extension authors must not modify MiaCode application source code, and must not ask users to rebuild MiaCode when creating an extension package.

扩展不会直接拿到 `MainWindow`、编辑器控件、预览控件、`QWidget` 或 QML 对象等内部 C++ 对象。扩展应调用 MiaCode 注册出来的公开 API。编写扩展时不能修改 MiaCode 本体源码，也不能要求用户为了安装扩展而重新编译 MiaCode。

## Install Location / 安装位置

Install user extensions here:

```text
<MiaCode install root>\extensions
```

用户扩展应放在：

```text
<MiaCode 安装根目录>\extensions
```

Do not place user extensions in `app\extensions`. Use one folder per extension. After copying, use Preferences > Extensions to refresh, enable, disable, and inspect diagnostics.

不要把用户扩展放进 `app\extensions`。一个扩展对应一个文件夹。复制后可在“首选项 > 扩展”中刷新、启用、禁用和查看诊断信息。

## Extension Package Structure / 扩展包结构

```text
my-extension/
  miacode-extension.json
  main.js
  assets/
  i18n/
```

You can also put the manifest in `package.json#miacodeExtension`. The `main` field is only needed when the extension runs JavaScript. Data-only extensions can omit `main`.

也可以把 manifest 写在 `package.json#miacodeExtension` 中。只有需要运行 JavaScript 时才需要 `main` 字段。纯数据扩展可以没有 `main`。

## Manifest Format / Manifest 编写规范

```json
{
  "id": "hello-tools",
  "name": "Hello Tools",
  "version": "0.0.1",
  "publisher": "local",
  "engines": { "miacode": ">=1.0.0" },
  "main": "./main.js",
  "activationEvents": ["onStartupFinished"],
  "permissions": ["ui.message", "editor.edit"],
  "contributes": {
    "commands": [
      { "command": "hello-tools.insert", "title": "Insert Text", "category": "Hello Tools" }
    ],
    "menus": {
      "tools/menu": [
        { "command": "hello-tools.insert" }
      ]
    }
  }
}
```

`id` and `publisher` use lowercase letters, numbers, `.`, `_`, and `-`. Paths are relative to the extension folder. Declare every permission used by privileged APIs. Disabled extensions lose all commands, menus, language packs, diagnostics, decorations, markers, and runtime state.

`id` 和 `publisher` 使用小写字母、数字、`.`、`_`、`-`。路径相对扩展文件夹。扩展调用特权 API 前必须在 `permissions` 中声明权限。扩展被禁用后，其命令、菜单、语言包、诊断、装饰、标记和运行时状态都会消失。

## Contribution Points / 贡献点

`commands` declares command IDs and display titles. JavaScript can bind callbacks with `miacode.commands.registerCommand`.

`commands` 声明命令 ID 和显示标题。JavaScript 可通过 `miacode.commands.registerCommand` 绑定回调。

`menus` places commands in supported UI locations such as `tools/menu` and `menubar/beforeHelp`.

`menus` 将命令挂载到支持的 UI 位置，例如 `tools/menu` 和 `menubar/beforeHelp`。

`languages` contributes language packs. Translation JSON maps `UiText` keys to display strings.

`languages` 贡献语言包。翻译 JSON 使用 `UiText` key 到显示文本的映射。

## Writing JavaScript Extensions / 编写 JS 扩展

MiaCode uses an embedded Qt JavaScript runtime. It supports a CommonJS-style entry:

MiaCode 使用内嵌 Qt JavaScript 运行时。入口采用 CommonJS 风格：

```js
function activate(context) {
  context.subscriptions.push(
    miacode.commands.registerCommand("hello-tools.insert", () => {
      miacode.editor.insertText("Hello from MiaCode extension");
    })
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
```

Node built-ins are not available: `require`, `fs`, `path`, `process`, `Buffer`, and `child_process`. Use MiaCode APIs instead. Handle returned `{ ok, value, error }` objects and log useful errors with `context.log()` or `miacode.logs.append()`.

不支持 Node 内置能力：`require`、`fs`、`path`、`process`、`Buffer`、`child_process`。请使用 MiaCode API。处理 `{ ok, value, error }` 返回对象，并用 `context.log()` 或 `miacode.logs.append()` 记录有用错误。

## Universal API Registry / 通用能力入口

MiaCode exposes a broad API registry so extension authors do not need to wait for a new handwritten JavaScript wrapper for every capability. Prefer typed `miacode.*` groups when they exist. Use `miacode.api.*` for discovery, compatibility checks, new registered capabilities, and public host bridge calls.

MiaCode 提供通用 API Registry。扩展作者不需要为每一种能力等待单独手写的 JS 包装。已有稳定分组 API 时优先使用 `miacode.*`；需要能力发现、兼容性检查、调用新登记能力或调用公开 host bridge method 时，使用 `miacode.api.*`。

```js
const all = miacode.api.list();
const editorApis = miacode.api.describeNamespace("editor");
const hasDecoration = miacode.api.has("editor.addDecoration");
const info = miacode.api.describe("editor.insertText");

miacode.api.call("editor.insertText", { text: "Hello" });
miacode.api.invoke("editor/insertText", { text: "Hello" });

miacode.api.request({
  id: "preview.customOverlayPipeline",
  reason: "Needed for chart review overlay.",
  required: false,
  fallback: "Show diagnostics only."
});
```

Registry entries include `id`, `method`, `permission`, `risk`, `status`, and `description`. `implemented` APIs can be called now. `partial` APIs have a callable entry but are not complete for the full user-visible scenario. `planned` APIs are listed for discovery and structured requests. `blocked` APIs can only be queried.

Registry 条目包含 `id`、`method`、`permission`、`risk`、`status`、`description`。`implemented` 可以直接调用；`partial` 表示入口可调用但还不能覆盖完整用户可见场景；`planned` 用于能力发现和结构化请求；`blocked` 只能查阅。

## Available API / 可用 API

Official convenience groups:

官方便捷调用分组：

```text
miacode.app
miacode.api
miacode.commands
miacode.window
miacode.workspace
miacode.document
miacode.editor
miacode.validation
miacode.diagnostics
miacode.analysis
miacode.timeline
miacode.preview
miacode.export
miacode.resources
miacode.fs
miacode.net
miacode.settings
miacode.extensions
miacode.ui
miacode.tasks
miacode.logs
```

`miacode.api.call(id, params)` and `miacode.api.invoke(method, params)` are the universal fallback. Convenience wrappers and registry calls use the same permission model.

`miacode.api.call(id, params)` 和 `miacode.api.invoke(method, params)` 是通用兜底入口。便捷 API 和 Registry 调用使用同一套权限模型。

## Full Capability Catalog / 完整能力目录

MiaCode opens APIs by internal module instead of adding one function at a time for individual requests. Query live status with `miacode.api.describe(id)` because some APIs are implemented, some are partial, some are planned, and a few are permanently blocked.

MiaCode 按内部模块批量开放 API，而不是按用户单个需求零散添加。请用 `miacode.api.describe(id)` 查询实时状态，因为部分 API 已实现，部分是半实现，部分仍在计划中，少数永久禁止调用。

## Extension Scenario Capability Matrix / 扩展场景能力矩阵

This matrix is the planning layer above the function list. A scenario is complete only when it has registration, host execution or rendering, update/remove APIs, events, state queries, permissions, and lifecycle cleanup.

这张矩阵是函数清单之上的规划层。一个场景只有同时具备注册入口、宿主执行或渲染、更新/删除 API、事件、状态查询、权限和生命周期清理，才算完整。

| Scenario | Current Status | Required Capability Groups |
| --- | --- | --- |
| Editor enhancement / 编辑器增强 | Partial | `editor.*`, `document.*`, `providers.*`, `validation.*`, selection/change events |
| Chart analysis / 谱面分析 | Partial | `analysis.*`, `validation.*`, `document.*`, result panels, export report |
| Preview overlay / 预览叠加 | Partial | `preview.addOverlay`, `preview.updateOverlay`, `preview.removeOverlay`, `preview.renderOverlayLayer`, `preview.hitTestOverlay`, preview events |
| Bottom panel tool / 底部面板工具 | Partial | `ui.registerBottomTabView`, `ui.renderBottomTabView`, `ui.refreshViews`, `ui.unregisterView`, declarative UI schema |
| Sidebar tool / 侧边栏工具 | Partial | `ui.registerSidebarView`, `ui.renderSidebarView`, `ui.refreshViews`, `ui.unregisterView`, declarative UI schema |
| Preferences page / 首选项页面 | Partial | `ui.registerPreferencesPage`, `ui.renderPreferencesPage`, `settings.*`, lifecycle cleanup |
| Toolbar extension / 工具栏扩展 | Partial | `ui.registerToolbarButton`, `ui.renderToolbarButton`, command bridge, enabled/visible state |
| Import and export / 导入导出 | Partial | `workspace.*`, `filesystem.*`, `export.*`, progress, cancellation, output diagnostics |
| Media processing / 媒体处理 | Planned | `media.*`, `resources.*`, `filesystem.*`, progress, compatibility checks |
| Settings and theme / 设置与主题 | Partial | `settings.*`, `theme.*`, change events, user confirmation for writes |
| Language pack / 语言包 | Implemented | `contributes.languages`, translation JSON, enable/disable cleanup |
| Automation and batch tasks / 自动化与批处理 | Partial | `tasks.*`, `commands.*`, `workspace.*`, progress, cancellation |
| Debug and diagnostics / 调试与诊断 | Partial | `logs.*`, `diagnostics.*`, `capabilities.*`, diagnostic bundles |
| Extension management / 扩展管理 | Partial | `extensions.*`, permissions, diagnostics, install/update/package |
| Expert local automation / 专家本地自动化 | Partial | `shell.*`, `process.*`, `filesystem.unsafe`, `network.unsafe`, extreme-risk prompts |

Priority planned items:

优先 planned 项：

```text
ui.renderDeclarativeView
ui.renderBottomTabView
ui.renderSidebarView
ui.renderPreferencesPage
ui.renderToolbarButton
ui.refreshViews
ui.unregisterView
preview.renderOverlayLayer
preview.updateOverlay
preview.removeOverlay
preview.getOverlays
preview.hitTestOverlay
```

1. App And Runtime / 应用与运行环境

```text
app.getVersion, app.getBuildInfo, app.getCommitHash, app.getPlatform,
app.getArchitecture, app.getInstallRoot, app.getExecutablePath,
app.getExtensionsRoot, app.getLogsRoot, app.getConfigRoot, app.getTempRoot,
app.getLocale, app.getTheme, app.getDpiScale, app.isPortableMode,
app.openPreferences, app.openAboutDialog, app.reloadExtensions,
app.restartRequired, app.requestRestart, app.quit
```

2. Capability Discovery / 能力发现与 API 请求

```text
capabilities.list, capabilities.has, capabilities.describe,
capabilities.describeNamespace, capabilities.getPermissions,
capabilities.getRiskLevel, capabilities.request,
capabilities.invokePublicMethod, capabilities.getMissing,
capabilities.exportRequestReport
```

3. Extension Management / 扩展管理

```text
extensions.list, extensions.get, extensions.getCurrent,
extensions.getDiagnostics, extensions.getPermissions,
extensions.getGrantedPermissions, extensions.openFolder,
extensions.openLogs, extensions.reload, extensions.enable,
extensions.disable, extensions.revokePermissions,
extensions.installFromFolder, extensions.installFromZip,
extensions.remove, extensions.update, extensions.validateManifest,
extensions.pack
```

4. Commands, Menus, And UI View Hosts / 命令、菜单与 UI 视图宿主

```text
commands.list, commands.describe, commands.register,
commands.unregister, commands.execute, commands.executeInternal,
commands.getHistory, menus.listContributionPoints, menus.getItems,
menus.refresh, menus.registerItem, menus.unregisterItem,
menus.setItemEnabled, menus.setItemVisible,
ui.registerSidebarView, ui.registerBottomTabView,
ui.registerPreferencesPage, ui.registerToolbarButton,
ui.getContributions, ui.getViews, ui.unregisterView,
ui.refreshViews, ui.renderDeclarativeView, ui.renderSidebarView,
ui.renderBottomTabView, ui.renderPreferencesPage,
ui.renderToolbarButton
```

5. Window And Interaction / 窗口与交互

```text
window.showInformationMessage, window.showWarningMessage,
window.showErrorMessage, window.showInputBox, window.showQuickPick,
window.showOpenDialog, window.showSaveDialog,
window.showSelectFolderDialog, window.createStatusBarItem,
window.setStatusBarMessage, window.clearStatusBarMessage,
window.setProgress, window.clearProgress, window.openExternalUrl,
window.focusEditor, window.focusPreview, window.focusTimeline,
window.focusValidationPanel, window.focusMetadataPanel
```

6. Workspace And File State / 工作区与文件状态

```text
workspace.getActiveDocument, workspace.getCurrentFilePath,
workspace.getChartFolder, workspace.getChartMetadata,
workspace.updateChartMetadata, workspace.getMediaFiles,
workspace.getRecentFiles, workspace.getDirtyState, workspace.isDirty,
workspace.save, workspace.saveAs, workspace.reloadFromDisk,
workspace.closeDocument, workspace.scanChartFolders,
workspace.getProjectData, workspace.setProjectData
```

7. Document And Chart Text / 文档与谱面文本

```text
document.getText, document.setText, document.applyTextEdits,
document.format, document.getDifficulties, document.getActiveDifficulty,
document.setActiveDifficulty, document.createDifficulty,
document.deleteDifficulty, document.renameDifficulty,
document.replaceActiveDifficultyText, document.getParsedNoteMarkers,
document.getTimingMetadata, document.getMetadata, document.updateMetadata
```

8. Editor / 编辑器

```text
editor.getSelection, editor.getCursor, editor.setSelection,
editor.insertText, editor.replaceSelection, editor.replaceRange,
editor.getLine, editor.getCurrentLine, editor.getCurrentToken,
editor.addDecoration, editor.clearDecorations, editor.showHover,
editor.addGutterIcon, editor.clearGutterIcons, editor.fold,
editor.unfold, editor.registerHoverProvider,
editor.registerCompletionProvider, editor.registerCodeActionProvider
```

9. Validation And Diagnostics / 校验与诊断

```text
validation.run, validation.getLastResult, validation.addDiagnostics,
validation.clearDiagnostics, diagnostics.validateDocument,
diagnostics.add, diagnostics.clear, diagnostics.getByOwner
```

10. Analysis / 分析

```text
analysis.runMuriAnalysis, analysis.getLastMuriResult,
analysis.runChartStats, analysis.getChartStats,
analysis.runTimingAnalysis, analysis.getTimingAnalysis,
analysis.registerAnalyzer, analysis.unregisterAnalyzer
```

11. Timeline / 时间线

```text
timeline.getSnapshot, timeline.getCurrentSecond, timeline.seek,
timeline.addMarker, timeline.clearMarkers, timeline.addBand,
timeline.addVerticalLine, timeline.clearVisuals,
timeline.registerMarkerClickCommand, timeline.getVisibleRange,
timeline.setVisibleRange, timeline.getZoom, timeline.setZoom
```

12. Preview And Playback / 预览与播放

```text
preview.play, preview.pause, preview.stop, preview.seek,
preview.getState, preview.setSpeed, preview.addOverlay,
preview.updateOverlay, preview.removeOverlay, preview.clearOverlays,
preview.getOverlays, preview.renderOverlayLayer,
preview.hitTestOverlay, preview.captureFrame,
preview.getAudioState, preview.setVolume, preview.setMuted,
preview.reloadMedia
```

13. Export / 导出

```text
export.getPresets, export.registerPreset, export.unregisterPreset,
export.startVideoExport, export.startCoverExport,
export.startBatchExport, export.getRunningJobs, export.cancelJob,
export.registerBeforeExportHook, export.registerAfterExportHook,
export.registerCoverTemplate, export.registerBatchJobProvider
```

14. Media / 媒体

```text
media.getMediaInfo, media.getTrackPath, media.setTrackPath,
media.getCoverPath, media.setCoverPath, media.getBackgroundPath,
media.setBackgroundPath, media.probeMedia, media.generateWaveform,
media.transcodeAudio, media.transcodeVideo
```

15. File System / 文件系统

```text
filesystem.readText, filesystem.writeText, filesystem.exists,
filesystem.listDir, filesystem.createDir, filesystem.delete,
filesystem.copy, filesystem.move, filesystem.readBinary,
filesystem.writeBinary, filesystem.watch, filesystem.unwatch,
filesystem.readAnyPathWithPrompt, filesystem.writeAnyPathWithPrompt
```

16. Network / 网络

```text
network.fetch, network.download, network.upload, network.head,
network.getJson, network.postJson, network.streamDownload,
network.cancelRequest, network.fetchLocalhostWithPrompt,
network.fetchPrivateNetworkWithPrompt
```

17. Settings / 设置

```text
settings.get, settings.set, settings.reset, settings.listKeys,
settings.onDidChange, settings.getExtensionConfig,
settings.setExtensionConfig, settings.import, settings.export,
settings.setSecuritySensitive
```

18. Theme And Appearance / 主题与外观

```text
theme.getCurrent, theme.setCurrent, theme.listAvailable,
theme.registerTheme, theme.unregisterTheme, theme.getColor,
theme.setColorOverride, theme.clearColorOverride
```

19. Shortcuts / 快捷键

```text
shortcuts.list, shortcuts.getKeybinding, shortcuts.setKeybinding,
shortcuts.resetKeybinding, shortcuts.registerCommandBinding,
shortcuts.unregisterCommandBinding
```

20. Clipboard And Native Dialogs / 剪贴板与系统对话框

```text
clipboard.readText, clipboard.writeText, clipboard.readImage,
clipboard.writeImage, nativeDialogs.openFile, nativeDialogs.saveFile,
nativeDialogs.selectFolder
```

21. Logs And Diagnostic Bundles / 日志与诊断包

```text
logs.readRecent, logs.writeExtensionLog, logs.openFolder,
logs.exportDiagnosticBundle, logs.clearExtensionLogs
```

22. Backup And Restore / 备份与恢复

```text
backup.createSnapshot, backup.listSnapshots, backup.restoreSnapshot,
backup.deleteSnapshot, backup.exportSnapshot, backup.importSnapshot
```

23. Experimental Object Proxies / 实验性内部对象代理

```text
objects.list, objects.describe, objects.call, objects.on,
objects.inspect, internal.inspectMainWindow,
internal.inspectDocumentModel, internal.inspectTimelineState,
internal.inspectPreviewState, internal.inspectExportState
```

24. Expert And Dangerous APIs / 专家模式与危险 API

```text
shell.execute, process.spawn, process.kill, process.openFileWithSystem,
process.revealInFileExplorer, process.openUrlInBrowser, process.inject,
native.loadDll, native.callFunction, native.loadQtPlugin,
native.loadQmlPlugin, internal.getMainWindowPointer,
internal.getQObjectPointer, internal.getTimelineViewPointer,
internal.getPreviewRendererPointer, internal.getDocumentRawPointer,
internal.getQmlEngine, internal.evalQml, internal.evalCppExpression,
renderer.getRawFrameBuffer, renderer.writeRawFrameBuffer,
renderer.getGraphicsDevice, renderer.getSwapchain, renderer.injectShader,
export.getWorkerRawState, export.modifyWorkerCommand,
export.overrideFfmpegArgsRaw
```

The APIs below are permanently blocked. They are queryable for transparency but `miacode.api.call()` and `miacode.api.invoke()` reject them.

以下 API 永久禁止调用。它们可以查阅，但 `miacode.api.call()` 和 `miacode.api.invoke()` 会拒绝执行。

```text
security.disablePermissionChecks
security.grantAllPermissions
security.modifyTrustedExtensions
security.installUnsignedSilently
updates.replaceExecutable
updates.patchApplicationFiles
updates.autoInstallWithoutPrompt
```

## Permissions / 权限

Manifest permissions are real gates. If an extension calls an API without declaring the required permission, MiaCode rejects the call and records diagnostics. High-risk and extreme-risk APIs are shown prominently in the extension page and may ask the user for confirmation.

Manifest 权限是真实门禁。扩展未声明所需权限却调用 API 时，MiaCode 会拒绝调用并记录诊断。高风险和极高风险 API 会在扩展页醒目标注，并可能要求用户确认。

## Examples / 示例

Command extension:

命令扩展示例：

```js
function activate(context) {
  context.subscriptions.push(
    miacode.commands.registerCommand("hello-tools.say", () => {
      miacode.window.showInformationMessage("Hello MiaCode");
    })
  );
}

module.exports = { activate };
```

Registry call:

Registry 调用示例：

```js
function activate() {
  const result = miacode.api.call("timeline.addMarker", {
    second: 12.5,
    label: "Review point",
    ownerId: "hello-tools"
  });
  if (!result.ok) {
    miacode.logs.append("extensions", result.error || "timeline marker failed");
  }
}

module.exports = { activate };
```

Language contribution:

语言贡献示例：

```json
{
  "contributes": {
    "languages": [
      {
        "id": "example-lang",
        "label": "Example Language",
        "translations": "./i18n/example.json"
      }
    ]
  }
}
```

## Validation And Testing / 校验与测试

Validate the manifest, install the folder under `<install root>\extensions`, refresh extensions, test enable/disable, inspect diagnostics, and verify that extension refresh does not change the current chart text, dirty state, file path, or active difficulty unless the extension explicitly calls an editing API.

请校验 manifest，把扩展文件夹安装到 `<MiaCode 安装根目录>\extensions`，刷新扩展，测试启用/禁用，查看诊断，并确认刷新扩展不会改变当前谱面文本、dirty 状态、文件路径或当前难度，除非扩展主动调用编辑 API。

## Prompt For AI / 给 AI 的提示词

Chinese prompt:

中文提示词：

```text
请为 MiaCode 制作一个本地扩展包。不要修改 MiaCode 本体源码，不要要求用户重新编译 MiaCode。扩展必须通过 miacode-extension.json、contributes、permissions、main.js 和 miacode.* / miacode.api.* 公开 API 工作。请输出完整文件夹结构、manifest、JS 入口代码、所需权限说明、安装位置和测试步骤。不要把某一种语言或功能写成硬编码特例。
```

English prompt:

英文提示词：

```text
Create a local MiaCode extension package. Do not modify MiaCode application source code and do not require users to rebuild MiaCode. The extension must work through miacode-extension.json, contribution points, permissions, main.js, and public miacode.* / miacode.api.* APIs. Output the complete folder structure, manifest, JavaScript entry code, permission explanation, install location, and test steps. Do not hard-code a specific language or feature as a special case.
```
