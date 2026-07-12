# MiaCode Extensions / MiaCode 扩展

MiaCode extensions are local folders loaded from the install-root `extensions` directory. v1 is intentionally small: public APIs must be real, callable, documented, and easy to validate.

MiaCode 扩展是放在安装根目录 `extensions` 下的本地文件夹。v1 刻意收缩为小闭环：公开 API 必须真实可调用、文档清楚、可以验证。

`planned` APIs are direction records only and are not callable. `blocked` APIs are rejected even if an extension declares their permissions.

`planned` 只表示计划方向，不能调用。`blocked` 即使在 manifest 里声明权限也会被拒绝。

## Install Location / 安装位置

```text
<MiaCode install root>\extensions
```

Use one folder per extension. After copying an extension, open Preferences > Extensions and refresh.

每个扩展使用一个独立文件夹。复制进去后，打开 Preferences > Extensions 并刷新。

## Manifest / 清单文件

Extensions may use `miacode-extension.json`, or `package.json` with a `miacodeExtension` object. Required fields are `id`, `name`, `version`, `publisher`, and `engines.miacode`. `main` is required only when JavaScript runs.

扩展可以使用 `miacode-extension.json`，也可以在 `package.json` 里放 `miacodeExtension` 对象。必填字段是 `id`、`name`、`version`、`publisher`、`engines.miacode`。只有需要运行 JavaScript 时才必须提供 `main`。

```json
{
  "id": "hello-tools",
  "name": "Hello Tools",
  "version": "0.0.1",
  "publisher": "local",
  "engines": { "miacode": ">=1.0.0" },
  "main": "./extension.js",
  "activationEvents": ["onStartupFinished"],
  "permissions": ["ui.message", "workspace.read"],
  "contributes": {
    "commands": [
      { "command": "hello-tools.say", "title": "Say Hello", "category": "Hello Tools" }
    ],
    "menus": {
      "tools/menu": [
        { "command": "hello-tools.say" }
      ]
    }
  }
}
```

Data-only language pack / 纯数据语言包:

```json
{
  "id": "sample-language",
  "name": "Sample Language",
  "version": "0.0.1",
  "publisher": "local",
  "engines": { "miacode": ">=1.0.0" },
  "contributes": {
    "languages": [
      { "id": "sample", "label": "Sample", "translations": "./i18n/sample.json" }
    ]
  }
}
```

## JavaScript Runtime / JavaScript 运行时

MiaCode uses embedded Qt `QJSEngine`, not Node.js. `require`, npm packages, `fs`, `path`, `process`, `Buffer`, and `child_process` are unavailable. Use `miacode.*` APIs.

MiaCode 使用内置 Qt `QJSEngine`，不是 Node.js。不能使用 `require`、npm 包、`fs`、`path`、`process`、`Buffer`、`child_process`。请使用 `miacode.*` API。

```js
function activate(context) {
  context.log("hello-tools activated");
  miacode.commands.registerCommand("hello-tools.say", () => {
    const doc = miacode.workspace.getActiveDocument();
    miacode.window.showInformationMessage(
      `Active document has ${doc.text.length} characters.`
    );
  });
}

function deactivate() {}

module.exports = { activate, deactivate };
```

## API Status / API 状态

Registry entries have one of three statuses:

Registry 条目只有三种状态：

- `implemented`: callable v1 API. / 已完成，v1 可调用。
- `planned`: recorded direction, not callable in v1. / 计划方向，v1 不可调用。
- `blocked`: rejected in the ordinary v1 extension host. / 普通 v1 扩展宿主中禁止调用。

`miacode.api.call(id, params)` and `miacode.api.invoke(method, params)` only execute `implemented` APIs. Use `miacode.api.request(...)` to record missing needs.

`miacode.api.call(id, params)` 和 `miacode.api.invoke(method, params)` 只会执行 `implemented` API。缺少能力时，用 `miacode.api.request(...)` 记录需求。

```js
const all = miacode.api.list();
const previewApis = miacode.api.describeNamespace("preview");
const canOverlay = miacode.api.has("preview.addOverlay");

miacode.api.request({
  id: "events.document.onDidChangeText",
  reason: "Needed to update a live helper panel.",
  fallback: "Use a manual command."
});
```

## v1 Implemented Surface / v1 已实现公开范围

Commands / 命令:

```text
miacode.commands.registerCommand
miacode.commands.executeCommand
miacode.commands.executeInternal
miacode.commands.getCommands
```

Window and app basics / 窗口与应用基础:

```text
miacode.app.getInfo
miacode.app.openPreferences
miacode.app.reloadExtensions
miacode.window.showInformationMessage
miacode.window.showWarningMessage
miacode.window.showErrorMessage
miacode.window.showInputBox
miacode.window.showQuickPick
miacode.window.createStatusBarItem
```

Document/editor / 文档与编辑器:

```text
miacode.workspace.getActiveDocument
miacode.workspace.applyDocumentEdit
miacode.workspace.getChartMetadata
miacode.workspace.updateChartMetadata
miacode.workspace.getChartFolder
miacode.workspace.getMediaFiles
miacode.workspace.save
miacode.workspace.saveAs
miacode.document.query
miacode.document.edit
miacode.document.getDifficulties
miacode.document.getActiveDifficulty
miacode.document.setActiveDifficulty
miacode.document.replaceActiveDifficultyText
miacode.document.getParsedNoteMarkers
miacode.document.getTimingMetadata
miacode.document.applyTextEdits
miacode.document.format
miacode.document.createDifficulty
miacode.document.deleteDifficulty
miacode.document.renameDifficulty
miacode.editor.getSelection
miacode.editor.getCursor
miacode.editor.setSelection
miacode.editor.getLine
miacode.editor.getCurrentLine
miacode.editor.getCurrentToken
miacode.editor.insertText
miacode.editor.replaceSelection
miacode.editor.replaceRange
miacode.editor.addDecoration
miacode.editor.clearDecorations
```

Validation, timeline, preview, UI / 校验、时间线、预览、界面:

```text
miacode.validation.run
miacode.validation.getLastResult
miacode.validation.addDiagnostics
miacode.validation.clearDiagnostics
miacode.diagnostics.validateDocument
miacode.timeline.getSnapshot
miacode.timeline.getCurrentSecond
miacode.timeline.seek
miacode.timeline.addMarker
miacode.timeline.clearMarkers
miacode.timeline.addBand
miacode.timeline.addVerticalLine
miacode.timeline.clearVisuals
miacode.preview.getState
miacode.preview.play
miacode.preview.pause
miacode.preview.stop
miacode.preview.seek
miacode.preview.setSpeed
miacode.preview.addOverlay
miacode.preview.updateOverlay
miacode.preview.removeOverlay
miacode.preview.clearOverlays
miacode.preview.getOverlays
miacode.preview.renderOverlayLayer
miacode.preview.hitTestOverlay
miacode.ui.registerBottomTabView
miacode.ui.registerToolbarButton
miacode.ui.getContributions
miacode.ui.getViews
miacode.ui.unregisterView
miacode.ui.refreshViews
miacode.ui.renderDeclarativeView
miacode.ui.renderBottomTabView
miacode.ui.renderToolbarButton
```

Logs and extension management / 日志与扩展管理:

```text
miacode.logs.append
miacode.logs.getPath
miacode.logs.open
miacode.extensions.all
miacode.extensions.get
miacode.extensions.enable
miacode.extensions.disable
miacode.extensions.installFromFolder
miacode.extensions.remove
```

## Planned But Not Callable In v1 / 已计划但 v1 不可调用

Events, editor providers, export hooks/templates/providers, native sidebar slots, native Preferences pages, media APIs, theme APIs, backup APIs, and shortcut APIs are planned. They should be requested with `miacode.api.request(...)` instead of treated as callable.

事件回调、编辑器 provider、导出 hook/template/provider、原生侧边栏槽位、原生 Preferences 页面、media API、theme API、backup API、shortcut API 都只是 planned。需要这些能力时，请用 `miacode.api.request(...)` 记录需求，不要当作可调用 API 使用。

## Blocked In The Ordinary v1 Host / 普通 v1 宿主中禁止

These are rejected even if listed in `permissions`:

下面这些即使写进 `permissions` 也会被拒绝：

```text
shell.execute
process.spawn
native.*
internal.raw
renderer.raw
export.raw
security.*
updates.*
```

Future trusted/dev/experimental builds may revisit some blocked capabilities, but ordinary user extensions must not rely on them.

未来 trusted/dev/experimental 构建可以重新讨论部分 blocked 能力，但普通用户扩展不能依赖它们。

## Permissions / 权限

Runtime permission checks are simple:

运行时权限规则很简单：

1. If an API needs a permission, the manifest must declare it.
2. If the API is in the blocked list above, the call is denied.
3. Otherwise the call is allowed.

中文对应：

1. API 需要权限时，manifest 必须声明。
2. API 在上面的 blocked 列表里时，调用会被拒绝。
3. 其他情况直接允许。

Risk words such as low, medium, or high may appear in docs/UI to explain impact, but they do not control runtime allow/deny and do not trigger user prompts. Installing an extension is the trust decision.

`low`、`medium`、`high` 这类风险词最多用于文档或 UI 说明影响，不决定运行时 allow/deny，也不会触发用户确认弹窗。用户安装扩展本身就是信任决策。

## Validation / 校验

Use the validator before installing:

安装前先运行 validator：

```powershell
node tools/extensions/validate-extension.mjs path\to\extension
```

Then install the folder under `<install root>\extensions`, refresh Preferences > Extensions, and test enable/disable. Refreshing extensions should not change the current chart text, dirty state, file path, or active difficulty unless the extension explicitly calls an editing API.

然后把扩展文件夹放到 `<install root>\extensions`，刷新 Preferences > Extensions，并测试启用/禁用。刷新扩展不应该改变当前谱面文本、dirty 状态、文件路径或当前难度，除非扩展明确调用了编辑 API。
