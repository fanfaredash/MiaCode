# MiaCode Extensions / MiaCode 扩展

MiaCode extensions are local folders loaded from the install-root `extensions` directory. v1 is intentionally small: public APIs must be real, callable, documented, and easy to validate.

MiaCode 扩展是放在安装根目录 `extensions` 下的本地文件夹。v1 刻意收缩为小闭环：公开 API 必须真实可调用、文档清楚、可以验证。

`planned` APIs are direction records only and are not callable. Experimental raw APIs are discoverable and permission-gated, but unstable.

`planned` 只表示计划方向，不能调用。experimental raw API 可以被发现并受权限控制，但不稳定。

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
- `blocked`: reserved historical status; current raw/unsafe namespaces are implemented experimental raw descriptors. / 历史保留状态；当前 raw/unsafe 命名空间以 implemented experimental raw 描述符呈现。

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

## Real Events and DevTools / 真实交互与 DevTools

Event APIs keep the JavaScript callback in the embedded runtime. They are not
descriptor-only placeholders. When a public host API changes document text,
saves the document, seeks the timeline, or changes preview playback state, the
host dispatches the matching event back into the extension that registered it.

事件 API 会把 JavaScript callback 保存在嵌入式运行时里，不只是登记一个
descriptor。当 public host API 修改文本、保存文档、跳转时间轴或改变预览播放
状态时，宿主会把对应事件派发回已注册的扩展。

```js
function activate(context) {
  context.subscriptions.push(
    miacode.events.onDidChangeText((event) => {
      context.log(`changed by ${event.sourceMethod}: ${event.textLength}`);
    }),
    miacode.timeline.onDidSeek((event) => {
      context.log(`seeked to ${event.second}`);
    }),
    miacode.preview.onDidChangeState((event) => {
      context.log(`preview changed by ${event.sourceMethod}`);
    })
  );
}
```

The callback runs with the registering extension's identity. Any API call made
inside the callback is still checked against that extension's manifest
permissions.

callback 会使用注册它的扩展身份运行。callback 内部再次调用 API 时，仍然按该
扩展 manifest 里的权限声明校验。

`miacode.devtools` is a stable diagnostic facade, not raw renderer access:

`miacode.devtools` 是稳定诊断 facade，不是 raw renderer 访问口：

```js
const snapshot = miacode.devtools.snapshot().value;
const diagnosis = miacode.devtools.diagnose("preview.seek").value;
const recentCalls = miacode.devtools.recentCalls().value;
```

- `snapshot()` returns API descriptors, Open Bridge descriptors, experimental
  raw markers, extension diagnostics, registered event callback count, UI
  contribution state, and recent host calls.
- `diagnose(idOrMethod)` explains the route, required permission, whether the
  current extension declares that permission, and whether the empty block hooks
  would reject it.
- `recentCalls()` returns the recent host API call ring buffer. It is capped and
  stores a compact parameter preview, not raw native pointers.

- `snapshot()` 返回 API 描述、Open Bridge 描述、experimental raw 标记、扩展诊断、
  已注册事件 callback 数量、UI contribution 状态和最近 host 调用。
- `diagnose(idOrMethod)` 解释路由、所需权限、当前扩展是否声明该权限，以及空的
  block hook 是否会拒绝它。
- `recentCalls()` 返回最近 host API 调用环形记录。它有数量上限，只保存压缩后的
  参数预览，不保存 raw native pointer。

## Open Bridge / 内部开放对象

`miacode.open` exposes controlled internal facade objects. It is more open than the stable wrapper API, but it still does not expose raw C++ objects, raw Qt widgets, raw renderers, or arbitrary QObject reflection.

```js
const objects = miacode.open.list().value;
const preview = miacode.open.describe("preview").value;
const state = miacode.open.call("preview", "getState").value;

miacode.open.call("document", "edit", {
  ops: [{ path: "/metadata/title", value: "New Title" }]
});
```

The stable SDK methods are convenience wrappers over the same controlled host surface. Prefer `miacode.preview.getState()`, `miacode.timeline.seek(12.5)`, `miacode.document.edit(...)`, or `miacode.ui.registerPetOverlay(...)` for common work; use `miacode.open.call(...)` when you need dynamic discovery or a newly exposed facade method.

Every described method reports `status`. `open.call(...)` only runs methods whose status is `implemented`; planned methods must be requested with `miacode.api.request(...)`. Method descriptors also report their `permission`, `hostMethod` or `command` route, and description, so `describe` should not advertise an implemented method that cannot be called.

`open.call(...)` requires layered permissions: `open.call`, the object permission such as `open.document` or `open.preview`, and the method permission such as `workspace.read`, `document.edit`, or `preview.control`. Discovery calls require `open.inspect`.

Use `miacode.open.forbiddenTargets()` as the legacy discovery name for experimental raw targets. These entries are now exposed in `open.list()` with `stability: "experimentalRaw"`, `experimentalRaw: true`, and `rawAccess: true`, including raw `MainWindow`, `QWidget`, `QQuickItem`, `QSGNode`, `PreviewRuntime`, `SimaiDocument`, `renderer.raw`, `internal.raw`, security, and update internals.

```js
const rawTargets = miacode.open.forbiddenTargets().value;
const rendererRaw = miacode.open.describe("renderer.raw").value;
miacode.open.call("renderer.raw", "inspect");
miacode.open.call("renderer.raw", "callUnsafe", { op: "debug-probe" });
```

Experimental raw targets require `open.call` plus `experimental.invoke`. They are intentionally marked experimental and should be treated as unstable internal bindings.

### Open Bridge Marker Notes / Open Bridge 标记说明

Open Bridge descriptors intentionally separate "risk marking" from "runtime denial".

Open Bridge 的描述符刻意区分“风险标记”和“运行时拒绝”。

| Field / 字段 | Meaning / 含义 | Does it block calls? / 是否会拒绝调用 |
| --- | --- | --- |
| `stability: "open"` | Stable facade object. / 稳定 facade 对象。 | No. / 否。 |
| `stability: "experimentalRaw"` | Raw/internal/unstable target. / raw、内部、不稳定目标。 | No. / 否。 |
| `experimentalRaw: true` | Warns that the target is experimental. / 提示这是实验目标。 | No. / 否。 |
| `rawAccess: true` | Warns that the target is raw/internal level. / 提示这是 raw 或内部级别。 | No. / 否。 |
| `rawCppObjectsExposed: true` | Marks raw C++/Qt/internal exposure intent. / 标记 raw C++/Qt/internal 暴露意图。 | No. / 否。 |
| `forbidden: false` | Legacy name compatibility; not a denial. / 兼容旧名称，不表示拒绝。 | No. / 否。 |

Current runtime policy:

当前运行时策略：

```text
experimentalRaw marker != block
rawAccess marker != block
rawCppObjectsExposed marker != block
forbiddenTargets() legacy name != block
```

Hard denial is controlled in source by `isPermanentlyBlockedApiMethod(...)`,
`isBlockedPermission(...)`, and `extensionIsForbiddenOpenTarget(...)`. In the
current build, raw targets are not rejected by these checks merely because they
are marked experimental raw.

硬拒绝由源码中的 `isPermanentlyBlockedApiMethod(...)`、
`isBlockedPermission(...)` 和 `extensionIsForbiddenOpenTarget(...)` 控制。在当前构建中，
raw target 不会仅仅因为带有 experimental raw 标记就被拒绝。

Typical experimental raw discovery:

典型 experimental raw 发现方式：

```js
const all = miacode.open.list().value;
const rawTargets = all.filter((item) => item.experimentalRaw);

for (const target of rawTargets) {
  context.log(`${target.id}: ${target.stability}`);
}
```

Typical experimental raw call:

典型 experimental raw 调用：

```js
const raw = miacode.open.describe("renderer.raw").value;

if (raw.experimentalRaw && raw.rawAccess) {
  miacode.open.call("renderer.raw", "inspect");
  miacode.open.call("renderer.raw", "callUnsafe", {
    op: "debug-probe",
    payload: { reason: "local experiment" }
  });
}
```

Permission example:

权限示例：

```json
{
  "permissions": [
    "open.inspect",
    "open.call",
    "experimental.invoke"
  ]
}
```

Raw namespaces still need their namespace permission when called through
direct public registry methods. Examples include `renderer.raw`, `internal.raw`,
`export.raw`, `security.override`, `updates.modify`, `shell.execute`, and
`process.manage`.

raw 命名空间在通过直接 public registry method 调用时仍需要对应命名空间权限。例如
`renderer.raw`、`internal.raw`、`export.raw`、`security.override`、
`updates.modify`、`shell.execute` 和 `process.manage`。

Important implementation boundary:

重要实现边界：

- If `open.call(...)` returns a normal `ok` response, the channel is open.
- `shell.execute` and `process.spawn` are implemented experimental raw APIs;
  they start detached host processes after the extension declares the required
  raw permission.
- If another raw namespace returns `accepted: true`, the experimental raw bridge
  accepted the call descriptor and is intentionally marking the result unstable.
- This is not a permission block. It means the raw bridge is open, while any
  concrete native object binding stays host-owned.

- 如果 `open.call(...)` 返回正常 `ok`，说明通道已开放。
- `shell.execute` 和 `process.spawn` 是已实现的 experimental raw API；扩展声明对应 raw 权限后会启动 detached 宿主进程。
- 如果其他 raw 命名空间返回 `accepted: true`，说明 experimental raw bridge 已接收调用描述，并明确把结果标记为不稳定。
- 这不是权限拒绝，而是 raw bridge 已开放；具体 native 对象绑定仍由宿主拥有。

## Controlled Pet Overlay / 受控桌宠覆盖层

`miacode.ui.registerPetOverlay(...)` adds a declarative preview overlay without exposing raw renderer, QML item, QWidget, or scene-graph objects. Image paths and sprite frame paths must resolve inside the extension directory; the host rejects missing files and path traversal.

```js
miacode.ui.registerPetOverlay({
  id: "sample-pet",
  frames: ["assets/pet-1.png", "assets/pet-2.png"],
  sprite: { fps: 6 },
  anchor: "bottomRight",
  size: 96,
  opacity: 0.92,
  draggable: true,
  onClickCommand: "sample.petClicked"
});
```

Supported v1 fields include `image`/`src`, `frames`, `sprite.fps`, `sprite.frameDurationMs`, `position`, `anchor`, `width`, `height`, `size`, `margin`, `opacity`, `draggable`, `onClickCommand`, and `onDragEndCommand`.

## v1 Implemented Surface / v1 已实现公开范围

Commands / 命令:

```text
miacode.commands.registerCommand
miacode.commands.executeCommand
miacode.commands.executeInternal
miacode.commands.getCommands
```

Open Bridge / 内部开放对象:

```text
miacode.open.list
miacode.open.describe
miacode.open.call
miacode.open.forbiddenTargets
miacode.open.describeForbiddenTarget
```

DevTools / 诊断工具:

```text
miacode.devtools.snapshot
miacode.devtools.diagnose
miacode.devtools.recentCalls
```

Window and app basics / 窗口与应用基础:

```text
miacode.app.getInfo
miacode.app.openPreferences
miacode.app.openAboutDialog
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
miacode.editor.undo
miacode.editor.redo
miacode.editor.cut
miacode.editor.copy
miacode.editor.paste
miacode.editor.selectAll
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
miacode.ui.registerSidebarView
miacode.ui.registerPreferencesPage
miacode.ui.registerToolbarButton
miacode.ui.registerPetOverlay
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
miacode.media.getInfo
miacode.media.list
miacode.theme.getCurrent
miacode.theme.listAvailable
miacode.theme.getColor
miacode.backup.list
miacode.backup.createBackup
miacode.backup.readBackup
miacode.backup.removeBackup
miacode.shortcuts.list
miacode.shortcuts.getKeybinding
miacode.shortcuts.registerShortcut
miacode.extensions.all
miacode.extensions.get
miacode.extensions.enable
miacode.extensions.disable
miacode.extensions.installFromFolder
miacode.extensions.remove
```

## Planned But Not Callable In v1 / 已计划但 v1 不可调用

There are currently no registry entries left in `planned`. Event/provider/export hook registration, sidebar-style views, Preferences-style views, media, theme, backup, and shortcut facades are callable v1 APIs.

当前 registry 中已经没有 `planned` 条目。事件/provider/export hook 注册、侧边栏风格视图、Preferences 风格视图、media、theme、backup 和 shortcut facade 都是可调用 v1 API。

## Experimental Raw Namespaces / 实验 raw 命名空间

These are discoverable as experimental raw targets and require explicit permissions:

下面这些会作为 experimental raw target 暴露，并要求显式权限：

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

These capabilities are not hard-blocked by permission alone. `shell.execute` and `process.spawn` start detached processes; the other raw namespaces route through the controlled experimental raw bridge and are unstable. Treat them as local/trusted development surface.

这些能力不再因为权限名被硬拒绝，但多数操作依赖宿主绑定且不稳定，应视为本地/可信开发接口。

## Permissions / 权限

Runtime permission checks are simple:

运行时权限规则很简单：

1. If an API needs a permission, the manifest must declare it.
2. Experimental raw APIs must declare their raw/experimental permission, and their descriptors carry `experimentalRaw` metadata.
3. Otherwise the call is allowed.

中文对应：

1. API 需要权限时，manifest 必须声明。
2. experimental raw API 必须声明 raw/experimental 权限；API 描述符会带有 `experimentalRaw` 元数据。
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
